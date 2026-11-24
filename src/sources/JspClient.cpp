#include "JspClient.h"

#include "JspProtocol.h"

#include <QDateTime>
#include <QTimer>
#include <QUrl>

#include <memory>

namespace cj {

struct JspClient::Attempt
{
    QString sourceId;
    QString path;
    QString key;
    QStringList order;     // bases to try, in order (one base appears twice)
    int next = 0;          // the next index of `order` to start
    int pending = 0;
    bool finished = false;
    QString lastError;
    JspClient::Done done;
};

JspClient::JspClient(QObject* parent)
    : QObject(parent)
{
}

JsonFetcher* JspClient::fetcherFor(const QString& sourceId)
{
    JsonFetcher*& f = m_fetchers[sourceId];
    if (!f) {
        f = new JsonFetcher(this);
        f->setTimeout(kTimeoutMs);
        f->setDeadline(kTimeoutMs);
        f->setAttempts(1);   // the next mirror is the second try
    }
    return f;
}

void JspClient::forget(const QString& sourceId)
{
    if (JsonFetcher* f = m_fetchers.take(sourceId)) {
        f->abortAll();
        f->deleteLater();
    }
    m_preferred.remove(sourceId);
    for (auto it = m_cache.begin(); it != m_cache.end();)
        it = it.key().startsWith(sourceId + QLatin1Char('\n')) ? m_cache.erase(it) : std::next(it);
}

void JspClient::get(const QString& sourceId, const QStringList& bases, const QString& path, Done done)
{
    const QString key = sourceId + QLatin1Char('\n') + path;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (const auto it = m_cache.constFind(key); it != m_cache.constEnd() && it->freshUntil > now) {
        const QJsonObject data = it->data;
        QTimer::singleShot(0, this, [done, data] { done(data, {}, false); });
        return;
    }
    auto attempt = std::make_shared<Attempt>();
    attempt->sourceId = sourceId;
    attempt->path = path;
    attempt->key = key;
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
    start(attempt);
}

void JspClient::start(const std::shared_ptr<Attempt>& attempt)
{
    if (attempt->finished || attempt->next >= attempt->order.size())
        return;
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

    auto finish = [this, attempt](const QJsonObject& data, const QString& error, bool stale) {
        if (attempt->finished)
            return;
        attempt->finished = true;
        attempt->done(data, error, stale);
    };
    fetcherFor(attempt->sourceId)->get(QUrl(base + attempt->path), [this, attempt, base, finish](const QJsonObject& json, const QString& error) {
        --attempt->pending;
        if (attempt->finished)
            return;
        QString problem = error;
        if (error.isEmpty()) {
            const jsp::Envelope e = jsp::parseEnvelope(json);
            if (e.ok) {
                m_preferred.insert(attempt->sourceId, base);
                if (m_cache.size() >= kMaxEntries)
                    m_cache.clear();
                m_cache.insert(attempt->key, Entry{e.data, QDateTime::currentMSecsSinceEpoch() + qint64(e.ttl) * 1000});
                finish(e.data, {}, false);
                return;
            }
            if (jsp::isDefinitive(e)) {
                finish({}, jsp::describe(e), false);
                return;
            }
            problem = jsp::describe(e);
        }
        attempt->lastError = problem;
        if (attempt->next < attempt->order.size()) {
            start(attempt);   // do not wait for the timer: this one failed
            return;
        }
        if (attempt->pending > 0)
            return;           // the other request may still answer
        if (const auto it = m_cache.constFind(attempt->key); it != m_cache.constEnd()) {
            finish(it->data, {}, true);   // an older good copy beats an error
            return;
        }
        finish({}, attempt->lastError, false);
    });
}

} // namespace cj
