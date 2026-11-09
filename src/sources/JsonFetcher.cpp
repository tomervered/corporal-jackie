#include "JsonFetcher.h"

#include "AppInfo.h"
#include "Logging.h"

#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>

#include <memory>

namespace cj {

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
    if (m_inFlight >= kMaxInFlight) {
        qCWarning(lcSources).noquote() << "GET" << logging::redactUrl(url) << "refused:" << m_inFlight << "requests waiting";
        QMetaObject::invokeMethod(this, [done = std::move(done)] { done({}, QStringLiteral("too many requests")); },
                                  Qt::QueuedConnection);
        return;
    }
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("%1/%2").arg(QString::fromLatin1(app::kName).remove(QLatin1Char(' ')),
                                                  QString::fromLatin1(app::kVersion)));
    request.setRawHeader("Accept", "application/json");
    request.setTransferTimeout(m_timeoutMs);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setMaximumRedirectsAllowed(kMaxRedirects);
    // Add-ons are strangers: no cookies kept from one, sent to another.
    request.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);

    QNetworkReply* reply = m_net->get(request);
    ++m_inFlight;
    // The body is read as it arrives, already decompressed, so the size cap
    // holds for a small gzip that would inflate to gigabytes.
    auto body = std::make_shared<QByteArray>();
    auto failure = std::make_shared<QString>();
    const qint64 maxBytes = m_maxBytes;
    const auto stop = [reply, failure](const QString& why) {
        if (failure->isEmpty()) {
            *failure = why;
            reply->abort();
        }
    };
    connect(reply, &QNetworkReply::readyRead, this, [reply, body, maxBytes, stop] {
        body->append(reply->read(maxBytes + 1 - body->size()));
        if (body->size() > maxBytes)
            stop(QStringLiteral("the answer was too large"));
    });
    connect(reply, &QNetworkReply::metaDataChanged, this, [reply, maxBytes, stop] {
        bool ok = false;
        const qint64 length = reply->header(QNetworkRequest::ContentLengthHeader).toLongLong(&ok);
        if (ok && length > maxBytes)
            stop(QStringLiteral("the answer was too large"));
    });
    // The transfer timeout only notices silence; this bounds a server that
    // keeps trickling bytes.
    auto* deadline = new QTimer(reply);
    deadline->setSingleShot(true);
    connect(deadline, &QTimer::timeout, reply, [stop] { stop(QStringLiteral("timed out")); });
    deadline->start(m_deadlineMs);

    connect(reply, &QNetworkReply::finished, this, [this, reply, body, failure, url, maxBytes, done = std::move(done)]() mutable {
        reply->deleteLater();
        --m_inFlight;
        if (failure->isEmpty() && reply->isReadable())
            body->append(reply->read(maxBytes + 1 - body->size()));
        if (failure->isEmpty() && body->size() > maxBytes)
            *failure = QStringLiteral("the answer was too large");
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        QString error = *failure;  // ours: too large, or past the deadline
        if (error.isEmpty()) {
            if (reply->property("cjAborted").toBool())
                error = QStringLiteral("cancelled");
            else if (reply->error() == QNetworkReply::OperationCanceledError
                     || reply->error() == QNetworkReply::TimeoutError)
                error = QStringLiteral("timed out");  // a transfer timeout, as a cancel or a timeout by Qt version
            else if (status >= 400)
                error = QStringLiteral("HTTP %1").arg(status);
            else if (reply->error() != QNetworkReply::NoError)
                error = reply->errorString();
        }
        if (!error.isEmpty()) {
            qCInfo(lcSources).noquote() << "GET" << logging::redactUrl(url) << "failed:" << error;
            done({}, error);
            return;
        }
        m_mailbox.run(
            this,
            [body = std::move(*body)] {
                QJsonParseError parseError{};
                const QJsonDocument doc = QJsonDocument::fromJson(body, &parseError);
                return std::make_pair(doc.object(), parseError.error == QJsonParseError::NoError && doc.isObject());
            },
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

void JsonFetcher::abortAll()
{
    for (QNetworkReply* reply : m_net->findChildren<QNetworkReply*>())
        if (reply->isRunning()) {
            reply->setProperty("cjAborted", true);
            reply->abort();
        }
}

} // namespace cj
