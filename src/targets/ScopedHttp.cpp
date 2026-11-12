#include "ScopedHttp.h"

#include "Logging.h"

#include <QMetaObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QRegularExpression>
#include <QThread>

#include <mpv/client.h>
#include <mpv/stream_cb.h>

#include <chrono>
#include <condition_variable>
#include <cstring>
#include <vector>

namespace cj {

namespace {

constexpr qsizetype kBufferBytes = 4 * 1024 * 1024;  // read ahead of mpv, per stream
constexpr qint64 kChunk = 256 * 1024;
const QString kPrefix = QStringLiteral("cj");
std::atomic<int> g_stallTimeoutMs{30000};

bool isWeb(const QUrl& url)
{
    const QString scheme = url.scheme().toLower();
    return (scheme == QLatin1String("http") || scheme == QLatin1String("https")) && !url.host().isEmpty();
}

// The bytes of a header line must not be able to start another header or
// request: no control characters, and no colon in the name.
bool safeHeader(const QString& name, const QString& value)
{
    if (name.isEmpty())
        return false;
    for (QChar c : name)
        if (c.unicode() <= 0x20 || c.unicode() >= 0x7f || c == QLatin1Char(':'))
            return false;
    for (QChar c : value)
        if ((c.unicode() < 0x20 && c != QLatin1Char('\t')) || c.unicode() == 0x7f)
            return false;
    return true;
}

bool isUserAgent(const QString& name) { return name.compare(QLatin1String("User-Agent"), Qt::CaseInsensitive) == 0; }

// Credentials written into a link (user:password@) become an Authorization
// header, so they get the same scoping as every other credential and the
// link itself no longer carries them.
QMap<QString, QString> credentialsOf(const QUrl& url, QMap<QString, QString> headers)
{
    for (auto it = headers.begin(); it != headers.end();) {
        if (!safeHeader(it.key(), it.value())) {
            qCWarning(lcTargets).noquote() << "Dropping a request header with control characters:" << it.key().left(40);
            it = headers.erase(it);
        } else {
            ++it;
        }
    }
    if (!url.userName().isEmpty() || !url.password().isEmpty()) {
        bool hasAuthorization = false;
        for (auto it = headers.cbegin(); it != headers.cend(); ++it)
            hasAuthorization |= it.key().compare(QLatin1String("Authorization"), Qt::CaseInsensitive) == 0;
        if (!hasAuthorization) {
            const QByteArray pair = (url.userName(QUrl::FullyDecoded) + QLatin1Char(':') + url.password(QUrl::FullyDecoded)).toUtf8();
            headers.insert(QStringLiteral("Authorization"), QStringLiteral("Basic ") + QString::fromLatin1(pair.toBase64()));
        }
    }
    return headers;
}

QUrl withoutUserInfo(QUrl url)
{
    url.setUserInfo(QString());
    return url;
}

} // namespace

HttpOrigin originOf(const QUrl& url)
{
    const QString scheme = url.scheme().toLower();
    const int fallback = scheme == QLatin1String("https") ? 443 : scheme == QLatin1String("http") ? 80 : -1;
    return {scheme, url.host().toLower(), url.port(fallback)};
}

// One stream mpv has open. mpv's thread reads; the network thread fills.
struct ScopedHttp::Stream
{
    QObject* context = nullptr;  // the network thread
    std::shared_ptr<std::atomic<bool>> aborted;
    QUrl url;                    // the real link
    HttpOrigin origin;
    QMap<QString, QString> headers;
    qint64 maxBytes = -1;

    std::mutex mutex;
    std::condition_variable wake;
    // Guarded by mutex:
    QByteArray buffer;           // bytes from the server, not yet read...
    qsizetype readAt = 0;        // ...from here on
    qint64 position = 0;         // file offset of buffer[readAt]
    qint64 size = -1;
    bool seekable = false;
    bool responded = false;      // the current request has its answer
    bool ended = false;          // ...and has delivered all it will
    bool pullQueued = false;
    bool cancelled = false;
    QString error;
    quint64 request = 0;         // the current request; older ones are ignored
    // Network thread only:
    QPointer<QNetworkReply> reply;
    qint64 skip = 0;             // bytes to drop (a server that ignored Range)
    bool replyFinished = false;

    bool stopped() const { return cancelled || aborted->load(); }
};

namespace {

using StreamPtr = std::shared_ptr<ScopedHttp::Stream>;

QNetworkAccessManager* networkOf(QObject* context)
{
    auto* nam = context->findChild<QNetworkAccessManager*>();
    if (!nam)
        nam = new QNetworkAccessManager(context);
    return nam;
}

void dropReply(ScopedHttp::Stream& s)
{
    if (QNetworkReply* r = s.reply.data()) {
        QObject::disconnect(r, nullptr, nullptr, nullptr);
        r->abort();
        r->deleteLater();
    }
    s.reply = nullptr;
}

// Fails the request if it is still the current one. Caller holds the lock.
void failLocked(ScopedHttp::Stream& s, const QString& why)
{
    if (s.error.isEmpty())
        s.error = why;
    s.ended = true;
    s.wake.notify_all();
}

// Moves what the server sent into the stream's buffer, up to its size.
void pull(const StreamPtr& s, quint64 id)
{
    std::lock_guard lock(s->mutex);
    s->pullQueued = false;
    QNetworkReply* reply = s->reply.data();
    if (s->request != id || !reply)
        return;
    if (s->readAt > 0 && s->readAt >= s->buffer.size() / 2) {
        s->buffer.remove(0, s->readAt);
        s->readAt = 0;
    }
    qint64 room = kBufferBytes - (s->buffer.size() - s->readAt);
    while (room > 0 && reply->bytesAvailable() > 0) {
        QByteArray chunk = reply->read(std::min(room, kChunk));
        if (chunk.isEmpty())
            break;
        if (s->skip > 0) {
            const qint64 drop = std::min<qint64>(s->skip, chunk.size());
            chunk.remove(0, drop);
            s->skip -= drop;
        }
        s->buffer += chunk;
        room -= chunk.size();
    }
    if (s->maxBytes >= 0 && s->position + (s->buffer.size() - s->readAt) > s->maxBytes) {
        dropReply(*s);
        failLocked(*s, QStringLiteral("larger than %1 bytes").arg(s->maxBytes));
        return;
    }
    if (s->replyFinished && reply->bytesAvailable() == 0) {
        if (reply->error() != QNetworkReply::NoError)
            failLocked(*s, reply->errorString());
        s->ended = true;
    }
    s->wake.notify_all();
}

void startRequest(const StreamPtr& s, quint64 id, const QUrl& hop, int hops, bool credentials, qint64 offset);

// The answer's status and headers are in: follow a redirect, or take the
// answer's size and range.
void onHeaders(const StreamPtr& s, QNetworkReply* reply, quint64 id, const QUrl& hop, int hops, bool credentials,
               qint64 offset)
{
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status == 0)
        return;
    std::unique_lock lock(s->mutex);
    if (s->request != id || s->reply != reply)
        return;
    // Once per answer: finished() calls this again, and a second pass over a
    // 200 would set skip again after pull() has already dropped those bytes.
    if (s->responded)
        return;
    if (status >= 300 && status < 400) {
        QUrl target = reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl();
        if (target.isEmpty())
            target = QUrl(QString::fromUtf8(reply->rawHeader("Location")));
        dropReply(*s);
        const QUrl next = hop.resolved(target);
        if (target.isEmpty() || !isWeb(next))
            return failLocked(*s, QStringLiteral("HTTP %1 to a link that is not a web link").arg(status));
        if (hops >= ScopedHttp::kMaxRedirects)
            return failLocked(*s, QStringLiteral("too many redirects"));
        // The credentials go no further once a hop leaves their origin.
        const bool keep = credentials && originOf(next) == s->origin;
        if (credentials && !keep && !s->headers.isEmpty())
            qCInfo(lcTargets).noquote() << "Redirect to" << logging::redactUrl(next)
                                        << "leaves the stream's server; its credentials stay behind";
        lock.unlock();
        // A user:password in a redirect is not sent either.
        startRequest(s, id, withoutUserInfo(next), hops + 1, keep, offset);
        return;
    }
    if (status != 200 && status != 206)
        return failLocked(*s, QStringLiteral("HTTP %1").arg(status));

    qint64 total = -1;
    bool ranged = false;
    if (status == 206) {
        static const QRegularExpression range(QStringLiteral(R"(bytes\s+(\d+)-(\d+)/(\d+|\*))"));
        const auto m = range.match(QString::fromLatin1(reply->rawHeader("Content-Range")));
        if (!m.hasMatch() || m.captured(1).toLongLong() != offset)
            return failLocked(*s, QStringLiteral("the server answered a different range"));
        if (m.captured(3) != QLatin1String("*"))
            total = m.captured(3).toLongLong();
        ranged = true;
    } else {
        bool ok = false;
        const qint64 length = reply->header(QNetworkRequest::ContentLengthHeader).toLongLong(&ok);
        if (ok)
            total = length;  // a 200 is the whole file, wherever the read was
        ranged = reply->rawHeader("Accept-Ranges").toLower().contains("bytes");
        s->skip = offset;  // Range ignored: the whole file came back
    }
    if (s->maxBytes >= 0 && total > s->maxBytes)
        return failLocked(*s, QStringLiteral("larger than %1 bytes").arg(s->maxBytes));
    if (total >= 0)
        s->size = total;
    s->seekable = ranged && s->size >= 0;
    s->responded = true;
    s->wake.notify_all();
}

void startRequest(const StreamPtr& s, quint64 id, const QUrl& hop, int hops, bool credentials, qint64 offset)
{
    QNetworkRequest request(hop);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    // No cookie jar and no remembered logins: the only credentials sent are
    // the ones below, and only while credentials is true.
    request.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::AuthenticationReuseAttribute, QNetworkRequest::Manual);
    QByteArray userAgent = "libmpv";  // what mpv itself would send
    if (credentials) {
        for (auto it = s->headers.cbegin(); it != s->headers.cend(); ++it) {
            if (isUserAgent(it.key()))
                userAgent = it.value().toUtf8();
            else
                request.setRawHeader(it.key().toUtf8(), it.value().toUtf8());
        }
    }
    request.setRawHeader("User-Agent", userAgent);
    request.setRawHeader("Accept-Encoding", "identity");  // byte offsets must be the file's
    if (offset > 0)
        request.setRawHeader("Range", "bytes=" + QByteArray::number(offset) + '-');

    QNetworkReply* reply = networkOf(s->context)->get(request);
    reply->setReadBufferSize(kBufferBytes);
    {
        std::lock_guard lock(s->mutex);
        if (s->request != id || s->stopped()) {
            reply->abort();
            reply->deleteLater();
            return;
        }
        s->reply = reply;
        s->skip = 0;
        s->replyFinished = false;
    }
    const std::weak_ptr<ScopedHttp::Stream> weak = s;
    QObject::connect(reply, &QNetworkReply::metaDataChanged, reply, [=] {
        if (auto p = weak.lock())
            onHeaders(p, reply, id, hop, hops, credentials, offset);
    });
    QObject::connect(reply, &QNetworkReply::readyRead, reply, [=] {
        if (auto p = weak.lock())
            pull(p, id);
    });
    QObject::connect(reply, &QNetworkReply::finished, reply, [=] {
        auto p = weak.lock();
        if (!p)
            return;
        onHeaders(p, reply, id, hop, hops, credentials, offset);  // no-op unless it was missed
        {
            std::lock_guard lock(p->mutex);
            if (p->request != id || p->reply != reply)
                return;
            p->replyFinished = true;
            if (!p->responded && reply->error() != QNetworkReply::NoError)
                return failLocked(*p, reply->errorString());
        }
        pull(p, id);
    });
}

// Starts a request at the stream's position. Caller holds the lock.
void restartLocked(const StreamPtr& s)
{
    const quint64 id = ++s->request;
    s->buffer.clear();
    s->readAt = 0;
    s->responded = false;
    s->ended = false;
    s->pullQueued = false;
    s->error.clear();
    const qint64 offset = s->position;
    QMetaObject::invokeMethod(s->context, [s, id, offset] {
        dropReply(*s);
        startRequest(s, id, s->url, 0, true, offset);
    }, Qt::QueuedConnection);
}

// mpv's callbacks. The cookie is a heap StreamPtr.
StreamPtr& streamOf(void* cookie) { return *static_cast<StreamPtr*>(cookie); }

int64_t readFn(void* cookie, char* buf, uint64_t nbytes)
{
    const StreamPtr& s = streamOf(cookie);
    std::unique_lock lock(s->mutex);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(ScopedHttp::stallTimeoutMs());
    for (;;) {
        if (s->stopped())
            return -1;
        const qsizetype available = s->buffer.size() - s->readAt;
        if (available > 0) {
            const qsizetype n = qsizetype(std::min<uint64_t>(uint64_t(available), nbytes));
            std::memcpy(buf, s->buffer.constData() + s->readAt, size_t(n));
            s->readAt += n;
            s->position += n;
            if (available - n < kBufferBytes / 2 && !s->pullQueued) {
                s->pullQueued = true;
                const quint64 id = s->request;
                QMetaObject::invokeMethod(s->context, [s, id] { pull(s, id); }, Qt::QueuedConnection);
            }
            return n;
        }
        if (!s->error.isEmpty())
            return -1;
        if (s->ended)
            return 0;
        if (s->wake.wait_until(lock, deadline) == std::cv_status::timeout && s->buffer.size() == s->readAt
            && !s->ended && s->error.isEmpty()) {
            qCWarning(lcTargets).noquote() << "No data from" << logging::redactUrl(s->url) << "for"
                                           << ScopedHttp::stallTimeoutMs() << "ms; giving up";
            s->error = QStringLiteral("stalled");
            return -1;
        }
    }
}

int64_t seekFn(void* cookie, int64_t offset)
{
    const StreamPtr& s = streamOf(cookie);
    std::lock_guard lock(s->mutex);
    if (offset == s->position)
        return offset;
    if (!s->seekable || offset < 0 || (s->size >= 0 && offset > s->size))
        return MPV_ERROR_UNSUPPORTED;
    const qint64 ahead = offset - s->position;
    if (ahead > 0 && ahead <= s->buffer.size() - s->readAt) {
        s->readAt += ahead;
        s->position = offset;
        return offset;
    }
    s->position = offset;
    if (offset == s->size) {
        // At the end: nothing to ask for.
        ++s->request;
        s->buffer.clear();
        s->readAt = 0;
        s->error.clear();
        s->ended = true;
        QMetaObject::invokeMethod(s->context, [s] { dropReply(*s); }, Qt::QueuedConnection);
        return offset;
    }
    restartLocked(s);
    return offset;
}

int64_t sizeFn(void* cookie)
{
    const StreamPtr& s = streamOf(cookie);
    std::lock_guard lock(s->mutex);
    return s->size >= 0 ? int64_t(s->size) : int64_t(MPV_ERROR_UNSUPPORTED);
}

void cancelFn(void* cookie)
{
    const StreamPtr& s = streamOf(cookie);
    std::lock_guard lock(s->mutex);
    s->cancelled = true;
    s->wake.notify_all();
}

void closeFn(void* cookie)
{
    auto* holder = static_cast<StreamPtr*>(cookie);
    StreamPtr s = *holder;
    delete holder;
    {
        std::lock_guard lock(s->mutex);
        ++s->request;  // late answers are ignored
    }
    QMetaObject::invokeMethod(s->context, [s] { dropReply(*s); }, Qt::QueuedConnection);
}

} // namespace

ScopedHttp::ScopedHttp()
    : m_aborted(std::make_shared<std::atomic<bool>>(false))
{
    m_thread = new QThread;
    m_thread->setObjectName(QStringLiteral("cj-http"));
    m_context = new QObject;
    m_context->moveToThread(m_thread);
    m_thread->start();
}

ScopedHttp::~ScopedHttp()
{
    abort();
    QObject* context = m_context;
    QMetaObject::invokeMethod(m_context, [context] { qDeleteAll(context->children()); },
                              Qt::BlockingQueuedConnection);
    m_thread->quit();
    m_thread->wait();
    delete m_context;
    delete m_thread;
}

int ScopedHttp::stallTimeoutMs() { return g_stallTimeoutMs.load(); }
void ScopedHttp::setStallTimeoutMs(int ms) { g_stallTimeoutMs.store(ms); }

bool ScopedHttp::registerWith(mpv_handle* mpv)
{
    for (const char* scheme : {"cjhttp", "cjhttps"}) {
        if (const int err = mpv_stream_cb_add_ro(mpv, scheme, this, &ScopedHttp::openCallback); err < 0) {
            qCCritical(lcTargets) << "Could not add" << scheme << "to mpv:" << mpv_error_string(err);
            return false;
        }
    }
    return true;
}

void ScopedHttp::reset()
{
    std::lock_guard lock(m_mutex);
    m_videoOrigin = {};
    m_videoHeaders.clear();
    m_routes.clear();
}

void ScopedHttp::abort()
{
    m_aborted->store(true);
    std::vector<std::weak_ptr<Stream>> streams;
    {
        std::lock_guard lock(m_mutex);
        streams = m_streams;
    }
    for (const auto& weak : streams) {
        if (auto s = weak.lock()) {
            std::lock_guard lock(s->mutex);
            s->wake.notify_all();
        }
    }
}

QUrl ScopedHttp::toProtocol(const QUrl& url) const
{
    QUrl routed = url;
    routed.setScheme(kPrefix + url.scheme().toLower());
    return routed;
}

QUrl ScopedHttp::routeVideo(const QUrl& url, const QMap<QString, QString>& headers)
{
    if (!isWeb(url))
        return url;
    const QMap<QString, QString> credentials = credentialsOf(url, headers);
    const QUrl real = withoutUserInfo(url);
    std::lock_guard lock(m_mutex);
    m_videoOrigin = originOf(real);
    m_videoHeaders = credentials;
    if (credentials.isEmpty())
        return url;
    const QUrl routed = toProtocol(real);
    m_routes.insert(routed.toString(QUrl::FullyEncoded), {real, credentials, -1});
    return routed;
}

QUrl ScopedHttp::routeSubtitle(const QUrl& url, qint64 maxBytes)
{
    if (!isWeb(url))
        return url;
    const QUrl real = withoutUserInfo(url);
    std::lock_guard lock(m_mutex);
    QMap<QString, QString> credentials = credentialsOf(url, {});
    if (credentials.isEmpty() && originOf(real) == m_videoOrigin)
        credentials = m_videoHeaders;
    const QUrl routed = toProtocol(real);
    m_routes.insert(routed.toString(QUrl::FullyEncoded), {real, credentials, maxBytes});
    return routed;
}

int ScopedHttp::openCallback(void* self, char* uri, mpv_stream_cb_info* info)
{
    return static_cast<ScopedHttp*>(self)->open(uri, info);
}

int ScopedHttp::open(const char* uri, mpv_stream_cb_info* info)
{
    const QUrl routed(QString::fromUtf8(uri));
    Route route;
    {
        std::lock_guard lock(m_mutex);
        const auto it = m_routes.constFind(routed.toString(QUrl::FullyEncoded));
        if (it != m_routes.cend()) {
            route = *it;
        } else {
            // A link mpv made itself, such as an HLS segment named relative
            // to its playlist: the video's credentials if it is on the
            // video's origin, none otherwise.
            route.url = routed;
            route.url.setScheme(routed.scheme().mid(kPrefix.size()));
            route.url = withoutUserInfo(route.url);
            if (originOf(route.url) == m_videoOrigin)
                route.headers = m_videoHeaders;
        }
    }
    if (!isWeb(route.url))
        return MPV_ERROR_LOADING_FAILED;

    auto s = std::make_shared<Stream>();
    s->context = m_context;
    s->aborted = m_aborted;
    s->url = route.url;
    s->origin = originOf(route.url);
    s->headers = route.headers;
    s->maxBytes = route.maxBytes;

    {
        std::lock_guard lock(m_mutex);
        std::erase_if(m_streams, [](const auto& w) { return w.expired(); });
        m_streams.push_back(s);
    }

    std::unique_lock lock(s->mutex);
    restartLocked(s);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(stallTimeoutMs());
    while (!s->responded && s->error.isEmpty() && !s->stopped()) {
        if (s->wake.wait_until(lock, deadline) == std::cv_status::timeout) {
            s->error = QStringLiteral("no answer");
            break;
        }
    }
    if (!s->responded) {
        if (!s->stopped())
            qCWarning(lcTargets).noquote() << "Could not open" << logging::redactUrl(s->url) << ":" << s->error;
        ++s->request;
        QMetaObject::invokeMethod(m_context, [s] { dropReply(*s); }, Qt::QueuedConnection);
        return MPV_ERROR_LOADING_FAILED;
    }
    lock.unlock();

    info->cookie = new StreamPtr(s);
    info->read_fn = readFn;
    info->seek_fn = seekFn;
    info->size_fn = sizeFn;
    info->close_fn = closeFn;
    info->cancel_fn = cancelFn;
    return 0;
}

} // namespace cj
