#include "JsonFetcher.h"

#include "AppInfo.h"
#include "Logging.h"

#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>

#include <algorithm>
#include <memory>

namespace cj {

namespace {

const QString kTooLarge = QStringLiteral("the answer was too large");

// What one request has collected so far. Shared by the handlers of that
// request, which all run on the UI thread.
struct Transfer
{
    QNetworkReply* reply = nullptr;
    qint64 maxBytes = 0;
    QByteArray body;
    QString failure;  // our own reason for giving up, in words; empty if none yet

    // Gives up on the request. Only the first reason counts.
    void giveUp(const QString& why)
    {
        if (!failure.isEmpty())
            return;
        failure = why;
        reply->abort();
    }
};

// QIODevice::read(n) reserves n bytes up front, so asking for the whole cap
// (8 MB) on every chunk briefly held megabytes per request. Read only what
// has arrived, still at most one byte past the cap.
void readSome(QNetworkReply& reply, QByteArray& body, qint64 maxBytes)
{
    const qint64 room = maxBytes + 1 - body.size();
    const qint64 n = std::min(reply.bytesAvailable(), room);
    if (n > 0)
        body.append(reply.read(n));
}

// The request every fetch starts from: who we are, that we want JSON, and
// no cookies. Add-ons are strangers: none kept from one, none sent to another.
QNetworkRequest makeJsonRequest(const QUrl& url, int transferTimeoutMs)
{
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("%1/%2").arg(QString::fromLatin1(app::kName).remove(QLatin1Char(' ')),
                                                  QString::fromLatin1(app::kVersion)));
    request.setRawHeader("Accept", "application/json");
    request.setTransferTimeout(transferTimeoutMs);
    request.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
    return request;
}

// Makes the request stop being read when the answer is too big (the body is
// read as it arrives, already decompressed, so the cap holds for a small
// gzip that would inflate to gigabytes) or takes too long overall.
void limitTransfer(QObject* owner, const std::shared_ptr<Transfer>& transfer, int deadlineMs)
{
    QNetworkReply* reply = transfer->reply;
    QObject::connect(reply, &QNetworkReply::readyRead, owner, [transfer] {
        readSome(*transfer->reply, transfer->body, transfer->maxBytes);
        if (transfer->body.size() > transfer->maxBytes)
            transfer->giveUp(kTooLarge);
    });
    // A server that says up front how big the answer is can be refused at once.
    QObject::connect(reply, &QNetworkReply::metaDataChanged, owner, [transfer] {
        bool known = false;
        const qint64 length = transfer->reply->header(QNetworkRequest::ContentLengthHeader).toLongLong(&known);
        if (known && length > transfer->maxBytes)
            transfer->giveUp(kTooLarge);
    });
    // The transfer timeout only notices silence; this bounds a server that
    // keeps trickling bytes.
    auto* deadline = new QTimer(reply);
    deadline->setSingleShot(true);
    QObject::connect(deadline, &QTimer::timeout, reply, [transfer] { transfer->giveUp(QStringLiteral("timed out")); });
    deadline->start(deadlineMs);
}

// Called when the reply has finished: takes the last bytes, and notes if
// the answer ended up too large.
void collectLastBytes(Transfer& transfer)
{
    if (transfer.failure.isEmpty() && transfer.reply->isReadable())
        readSome(*transfer.reply, transfer.body, transfer.maxBytes);
    if (transfer.failure.isEmpty() && transfer.body.size() > transfer.maxBytes)
        transfer.failure = kTooLarge;
}

bool wasCancelledByUs(const QNetworkReply& reply)
{
    return reply.property("cjAborted").toBool();
}

// Nothing arrived for the whole transfer timeout. Qt reports that as a
// cancel or as a timeout, depending on its version.
bool hitTransferTimeout(const QNetworkReply& reply)
{
    return reply.error() == QNetworkReply::OperationCanceledError || reply.error() == QNetworkReply::TimeoutError;
}

// Why the request stopped early, in words, or "" if it ran to the end.
QString whyStoppedEarly(const Transfer& transfer)
{
    if (!transfer.failure.isEmpty())
        return transfer.failure;  // too large, or past the deadline
    if (wasCancelledByUs(*transfer.reply))
        return QStringLiteral("cancelled");
    if (hitTransferTimeout(*transfer.reply))
        return QStringLiteral("timed out");
    return {};
}

// Reads the body as a JSON object. Runs on a pool thread.
std::pair<QJsonObject, bool> parseJsonObject(const QByteArray& body)
{
    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(body, &parseError);
    return {doc.object(), parseError.error == QJsonParseError::NoError && doc.isObject()};
}

} // namespace

JsonFetcher::JsonFetcher(QObject* parent)
    : QObject(parent)
    , m_net(new QNetworkAccessManager(this))
{
}

JsonFetcher::~JsonFetcher()
{
    // Replies are children of m_net; deleting them must not call back into us.
    for (QNetworkReply* reply : m_net->findChildren<QNetworkReply*>())
        reply->disconnect(this);
}

void JsonFetcher::get(const QUrl& url, Done done)
{
    fetch(url, std::move(done), m_attempts);
}

// Sends one request. `attemptsLeft` counts this one: a request that timed
// out is sent again while it is above 1.
void JsonFetcher::fetch(const QUrl& url, Done done, int attemptsLeft)
{
    if (m_inFlight >= kMaxInFlight) {
        qCWarning(lcSources).noquote() << "GET" << logging::redactUrl(url) << "refused:" << m_inFlight << "requests waiting";
        QMetaObject::invokeMethod(this, [done = std::move(done)] { done({}, QStringLiteral("too many requests")); },
                                  Qt::QueuedConnection);
        return;
    }
    QNetworkRequest request = makeJsonRequest(url, m_timeoutMs);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setMaximumRedirectsAllowed(kMaxRedirects);

    auto transfer = std::make_shared<Transfer>();
    transfer->reply = m_net->get(request);
    transfer->maxBytes = m_maxBytes;
    ++m_inFlight;
    limitTransfer(this, transfer, m_deadlineMs);

    connect(transfer->reply, &QNetworkReply::finished, this,
            [this, transfer, url, attemptsLeft, done = std::move(done)]() mutable {
        QNetworkReply* reply = transfer->reply;
        reply->deleteLater();
        --m_inFlight;
        collectLastBytes(*transfer);

        // Nothing arrived for the whole transfer timeout: the network may have
        // stalled for a moment (a wake from sleep, a VPN coming up), so ask once more.
        const bool timedOut = transfer->failure.isEmpty() && !wasCancelledByUs(*reply) && hitTransferTimeout(*reply);
        if (timedOut && attemptsLeft > 1) {
            qCInfo(lcSources).noquote() << "GET" << logging::redactUrl(url) << "timed out; trying once more";
            fetch(url, std::move(done), attemptsLeft - 1);
            return;
        }

        QString error = whyStoppedEarly(*transfer);
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (error.isEmpty() && status >= 400)
            error = QStringLiteral("HTTP %1").arg(status);
        else if (error.isEmpty() && reply->error() != QNetworkReply::NoError)
            error = reply->errorString();
        if (!error.isEmpty()) {
            qCInfo(lcSources).noquote() << "GET" << logging::redactUrl(url) << "failed:" << error;
            done({}, error);
            return;
        }

        m_mailbox.run(
            this, [body = std::move(transfer->body)] { return parseJsonObject(body); },
            [url, done = std::move(done)](std::pair<QJsonObject, bool> parsed) {
                if (!parsed.second) {
                    qCInfo(lcSources).noquote() << "GET" << logging::redactUrl(url) << "did not return a JSON object";
                    done({}, QStringLiteral("the answer was not JSON"));
                    return;
                }
                done(parsed.first, {});
            });
    });
}

// Why a request is not even sent, or "" if it can go.
QString JsonFetcher::refusalReason(const QUrl& url, int budgetMs, const UrlCheck& allowed) const
{
    if (budgetMs <= 0)
        return QStringLiteral("timed out");
    if (m_inFlight >= kMaxInFlight)
        return QStringLiteral("too many requests");
    if (allowed && !allowed(url))
        return QStringLiteral("address not allowed");
    return {};
}

// Like fetch(), but hands back the HTTP status and error bodies too, never
// retries, and follows redirects only to addresses `allowed` accepts.
quint64 JsonFetcher::getReply(const QUrl& url, int budgetMs, UrlCheck allowed, ReplyDone done)
{
    const quint64 id = ++m_nextId;
    if (const QString refusal = refusalReason(url, budgetMs, allowed); !refusal.isEmpty()) {
        QMetaObject::invokeMethod(this, [done = std::move(done), refusal] {
            Reply r;
            r.error = refusal;
            done(r);
        }, Qt::QueuedConnection);
        return id;
    }
    QNetworkRequest request = makeJsonRequest(url, budgetMs);
    // Each hop waits for our word (redirectAllowed), so it is held to the same address rules as the first.
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::UserVerifiedRedirectPolicy);

    auto transfer = std::make_shared<Transfer>();
    transfer->reply = m_net->get(request);
    transfer->maxBytes = m_maxBytes;
    ++m_inFlight;
    m_byId.insert(id, transfer->reply);
    limitTransfer(this, transfer, budgetMs);

    auto hops = std::make_shared<int>(0);
    connect(transfer->reply, &QNetworkReply::redirected, this, [transfer, hops, allowed](const QUrl& target) {
        if (++*hops > kMaxRedirects) {
            transfer->giveUp(QStringLiteral("too many redirects"));
            return;
        }
        if (allowed && !allowed(target)) {
            transfer->giveUp(QStringLiteral("redirect not allowed"));
            return;
        }
        emit transfer->reply->redirectAllowed();
    });

    connect(transfer->reply, &QNetworkReply::finished, this, [this, transfer, id, done = std::move(done)]() mutable {
        QNetworkReply* reply = transfer->reply;
        reply->deleteLater();
        --m_inFlight;
        m_byId.remove(id);
        collectLastBytes(*transfer);

        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        QString error = whyStoppedEarly(*transfer);
        if (error.isEmpty() && status == 0)
            error = reply->errorString().isEmpty() ? QStringLiteral("no answer") : reply->errorString();
        if (!error.isEmpty()) {
            Reply r;
            r.status = status;
            r.error = error;
            done(r);
            return;
        }

        m_mailbox.run(
            this, [body = std::move(transfer->body)] { return parseJsonObject(body); },
            [status, done = std::move(done)](std::pair<QJsonObject, bool> parsed) {
                Reply r;
                r.status = status;
                r.json = parsed.first;
                r.isJson = parsed.second;
                done(r);
            });
    });
    return id;
}

void JsonFetcher::cancel(quint64 id)
{
    if (auto* reply = qobject_cast<QNetworkReply*>(m_byId.value(id).data())) {
        if (reply->isRunning()) {
            reply->setProperty("cjAborted", true);
            reply->abort();
        }
    }
}

void JsonFetcher::abortAll()
{
    for (QNetworkReply* reply : m_net->findChildren<QNetworkReply*>())
        if (reply->isRunning()) {
            reply->setProperty("cjAborted", true);
            reply->abort();
        }
}

} // namespace cj
