#include "JspClient.h"

#include "JspProtocol.h"

#include <QDateTime>
#include <QTimer>
#include <QUrl>

#include <iterator>
#include <memory>
#include <utility>

namespace cj {

namespace {

// Catalogs and titles may be shown from an old copy when the source is down.
// Streams and subtitles may not: their links can be signed and expire.
bool staleAllowed(const QString& path)
{
    return path.startsWith(QLatin1String("/catalog/")) || path.startsWith(QLatin1String("/meta/"))
        || path.startsWith(QLatin1String("/search"));
}

QString coolKey(const QString& sourceId, const QString& base)
{
    return sourceId + QLatin1Char('\n') + base;
}

} // namespace

struct JspClient::Attempt
{
    QString sourceId;
    QString path;
    QString key;
    QStringList order;     // bases to try, in order (one base appears twice when there is no mirror)
    int next = 0;          // the next index of `order` to start
    int pending = 0;
    bool finished = false;
    bool allowStale = false;
    int generation = 0;
    int budgetMs = 0;
    QElapsedTimer clock;   // monotonic: started with the request
    QString lastError;
    QPointer<JsonFetcher> fetcher;
    QVector<quint64> ids;  // the tries in flight, to cancel when the request is over
    JspClient::Done done;
};

JspClient::JspClient(QObject* parent)
    : QObject(parent)
{
}

JsonFetcher* JspClient::fetcherFor(const QString& sourceId)
{
    JsonFetcher*& f = m_fetchers[sourceId];
    if (!f)
        f = new JsonFetcher(this);
    return f;
}

void JspClient::forget(const QString& sourceId)
{
    ++m_generation[sourceId];
    if (JsonFetcher* f = m_fetchers.take(sourceId)) {
        // Not inside this call: the callers are often in the middle of changing the add-on list.
        QMetaObject::invokeMethod(f, [f] {
            f->abortAll();
            f->deleteLater();
        }, Qt::QueuedConnection);
    }
    m_preferred.remove(sourceId);
    const QString prefix = sourceId + QLatin1Char('\n');
    for (auto it = m_cache.begin(); it != m_cache.end();)
        it = it.key().startsWith(prefix) ? m_cache.erase(it) : std::next(it);
    for (auto it = m_cooldown.begin(); it != m_cooldown.end();)
        it = it.key().startsWith(prefix) ? m_cooldown.erase(it) : std::next(it);
}

void JspClient::get(const QString& sourceId, const QStringList& bases, const QString& path, Done done)
{
    const QString key = sourceId + QLatin1Char('\n') + path;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (const auto it = m_cache.constFind(key); it != m_cache.constEnd() && it->freshUntil > now) {
        const QJsonObject data = it->data;
        // Delivered on the next turn: a source removed or changed until then gets "cancelled", like a network answer.
        const int generation = m_generation.value(sourceId);
        QTimer::singleShot(0, this, [this, done, data, sourceId, generation] {
            if (generation != m_generation.value(sourceId))
                done({}, QStringLiteral("cancelled"), false);
            else
                done(data, {}, false);
        });
        return;
    }
    auto attempt = std::make_shared<Attempt>();
    attempt->sourceId = sourceId;
    attempt->path = path;
    attempt->key = key;
    attempt->allowStale = staleAllowed(path);
    attempt->generation = m_generation.value(sourceId);
    attempt->budgetMs = m_budgetMs;
    attempt->done = std::move(done);
    QStringList order;
    const QString preferred = m_preferred.value(sourceId);
    if (!preferred.isEmpty() && bases.contains(preferred))
        order << preferred;
    for (const QString& b : bases)
        if (!order.contains(b))
            order << b;
    if (order.isEmpty()) {
        QTimer::singleShot(0, this, [attempt] { attempt->done({}, QStringLiteral("no address"), false); });
        return;
    }
    if (order.size() == 1)
        order << order.first();   // no mirror: the same address is asked once more
    attempt->order = order;
    attempt->clock.start();
    // The one deadline of this request, whatever the tries are doing.
    QTimer::singleShot(attempt->budgetMs, this, [this, attempt] {
        if (!attempt->finished)
            fail(attempt, QStringLiteral("timed out"));
    });
    // Never answers inside get(): callers do not expect it.
    QTimer::singleShot(0, this, [this, attempt] { start(attempt); });
}

void JspClient::complete(const std::shared_ptr<Attempt>& attempt, const QJsonObject& data, const QString& error, bool stale)
{
    if (attempt->finished)
        return;
    attempt->finished = true;   // first: the callbacks that abort() runs must find it over
    if (attempt->fetcher) {
        for (const quint64 id : std::as_const(attempt->ids))
            attempt->fetcher->cancel(id);
    }
    attempt->ids.clear();
    attempt->done(data, error, stale);
}

void JspClient::fail(const std::shared_ptr<Attempt>& attempt, const QString& error)
{
    if (attempt->finished)
        return;
    if (attempt->allowStale && attempt->generation == m_generation.value(attempt->sourceId)) {
        if (const auto it = m_cache.constFind(attempt->key);
            it != m_cache.constEnd() && it->keepUntil > QDateTime::currentMSecsSinceEpoch()) {
            complete(attempt, it->data, {}, true);   // an older good copy beats an error
            return;
        }
    }
    complete(attempt, {}, error, false);
}

void JspClient::start(const std::shared_ptr<Attempt>& attempt)
{
    if (attempt->finished)
        return;
    if (attempt->generation != m_generation.value(attempt->sourceId)) {
        complete(attempt, {}, QStringLiteral("cancelled"), false);
        return;
    }
    // An address that asked for quiet (429) is not asked again until then.
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    while (attempt->next < attempt->order.size()) {
        const qint64 until = m_cooldown.value(coolKey(attempt->sourceId, attempt->order.at(attempt->next)));
        if (until <= now)
            break;
        attempt->lastError = QStringLiteral("rate_limited: try again in %1 s").arg(int((until - now + 999) / 1000));
        ++attempt->next;
    }
    if (attempt->next >= attempt->order.size()) {
        if (attempt->pending == 0)
            fail(attempt, attempt->lastError.isEmpty() ? QStringLiteral("unavailable") : attempt->lastError);
        return;   // otherwise a try in flight may still answer
    }
    const int remaining = attempt->budgetMs - int(attempt->clock.elapsed());
    if (remaining <= 0) {
        fail(attempt, QStringLiteral("timed out"));
        return;
    }
    const QString base = attempt->order.at(attempt->next++);
    ++attempt->pending;
    // The next request goes out if this one is slow, unless another one
    // has started since (this one failed fast and was already replaced).
    const int index = attempt->next;
    if (index < attempt->order.size())
        QTimer::singleShot(m_hedgeMs, this, [this, attempt, index] {
            if (!attempt->finished && attempt->next == index)
                start(attempt);
        });

    JsonFetcher* fetcher = fetcherFor(attempt->sourceId);
    attempt->fetcher = fetcher;
    const quint64 id = fetcher->getReply(QUrl(base + attempt->path), remaining, &jsp::allowedUrl,
                                         [this, attempt, base](const JsonFetcher::Reply& reply) { handle(attempt, base, reply); });
    attempt->ids << id;
}

void JspClient::handle(const std::shared_ptr<Attempt>& attempt, const QString& base, const JsonFetcher::Reply& reply)
{
    --attempt->pending;
    if (attempt->finished)
        return;
    if (attempt->generation != m_generation.value(attempt->sourceId)) {
        complete(attempt, {}, QStringLiteral("cancelled"), false);   // the source changed meanwhile: save nothing
        return;
    }
    QString problem = reply.error;
    if (problem.isEmpty()) {
        jsp::Envelope e;
        if (reply.isJson) {
            e = jsp::parseEnvelope(reply.json);
        } else {
            e.code = QStringLiteral("unavailable");   // an HTML error page, or anything else that is not ours
            e.message = reply.status >= 400 ? QStringLiteral("HTTP %1").arg(reply.status) : QStringLiteral("not a Jackie answer");
        }
        const bool httpOk = reply.status >= 200 && reply.status < 300;
        if (e.ok && httpOk && !jsp::validData(attempt->path, e.data)) {
            // Says ok but the data is not what the resource promises: not cached, and another mirror is asked.
            e.ok = false;
            e.code = QStringLiteral("unavailable");
            e.message = QStringLiteral("malformed answer");
        }
        if (e.ok && httpOk) {
            const qint64 now = QDateTime::currentMSecsSinceEpoch();
            m_preferred.insert(attempt->sourceId, base);
            if (m_cache.size() >= kMaxEntries)
                m_cache.clear();
            const qint64 fresh = now + qint64(e.ttl) * 1000;
            m_cache.insert(attempt->key, Entry{e.data, fresh, fresh + kStaleKeepMs});
            complete(attempt, e.data, {}, false);
            return;
        }
        if (e.ok) {   // an answer that says ok under an error status is not trusted
            e.ok = false;
            e.code = QStringLiteral("unavailable");
            e.message = QStringLiteral("HTTP %1").arg(reply.status);
        }
        if (jsp::isDefinitive(e)) {
            complete(attempt, {}, jsp::describe(e), false);
            return;
        }
        if (e.code == QLatin1String("rate_limited")) {
            const int seconds = qBound(1, e.retryAfter > 0 ? e.retryAfter : kDefaultCooldownS, kMaxCooldownS);
            m_cooldown.insert(coolKey(attempt->sourceId, base), QDateTime::currentMSecsSinceEpoch() + qint64(seconds) * 1000);
        }
        problem = jsp::describe(e);
    }
    attempt->lastError = problem;
    start(attempt);   // do not wait for the hedge timer: this one failed
}

} // namespace cj
