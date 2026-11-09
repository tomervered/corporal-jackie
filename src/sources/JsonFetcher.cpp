#include "JsonFetcher.h"

#include "AppInfo.h"
#include "Logging.h"

#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

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
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("%1/%2").arg(QString::fromLatin1(app::kName).remove(QLatin1Char(' ')),
                                                  QString::fromLatin1(app::kVersion)));
    request.setRawHeader("Accept", "application/json");
    request.setTransferTimeout(m_timeoutMs);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);

    QNetworkReply* reply = m_net->get(request);
    ++m_inFlight;
    auto tooLarge = std::make_shared<bool>(false);
    const qint64 maxBytes = m_maxBytes;

    connect(reply, &QNetworkReply::downloadProgress, this, [reply, tooLarge, maxBytes](qint64 received, qint64 total) {
        if (!*tooLarge && (received > maxBytes || total > maxBytes)) {
            *tooLarge = true;
            reply->abort();
        }
    });

    connect(reply, &QNetworkReply::finished, this, [this, reply, tooLarge, url, done = std::move(done)]() mutable {
        reply->deleteLater();
        --m_inFlight;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        QString error;
        if (*tooLarge)
            error = QStringLiteral("the answer was too large");
        else if (reply->property("cjAborted").toBool())
            error = QStringLiteral("cancelled");
        else if (reply->error() == QNetworkReply::OperationCanceledError
                 || reply->error() == QNetworkReply::TimeoutError)
            error = QStringLiteral("timed out");  // a transfer timeout, as a cancel or a timeout by Qt version
        else if (status >= 400)
            error = QStringLiteral("HTTP %1").arg(status);
        else if (reply->error() != QNetworkReply::NoError)
            error = reply->errorString();
        if (!error.isEmpty()) {
            qCInfo(lcSources).noquote() << "GET" << logging::redactUrl(url) << "failed:" << error;
            done({}, error);
            return;
        }
        QByteArray body = reply->readAll();
        m_mailbox.run(
            this,
            [body = std::move(body)] {
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
