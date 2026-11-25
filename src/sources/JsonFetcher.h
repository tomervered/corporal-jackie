#pragma once
// GETs a JSON object over HTTP(S), bounded: every request has a timeout and
// a size limit, and the JSON is parsed off the UI thread. Used by the
// Stremio adapter; one instance per adapter.

#include "Mailbox.h"

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QUrl>

#include <functional>

class QNetworkAccessManager;

namespace cj {

class JsonFetcher : public QObject
{
    Q_OBJECT

public:
    static constexpr int kDefaultTimeoutMs = 15000;        // with no bytes arriving
    static constexpr int kDefaultDeadlineMs = 45000;       // for the whole answer
    static constexpr qint64 kDefaultMaxBytes = 8 * 1024 * 1024;  // after decompression
    static constexpr int kMaxInFlight = 64;                // more fail at once
    static constexpr int kMaxRedirects = 5;
    static constexpr int kAttempts = 2;                    // a request that timed out is sent once more

    explicit JsonFetcher(QObject* parent = nullptr);
    ~JsonFetcher() override;

    void setTimeout(int ms) { m_timeoutMs = ms; }
    void setDeadline(int ms) { m_deadlineMs = ms; }
    // How many times a request that timed out is sent (1: never again).
    void setAttempts(int n) { m_attempts = n; }
    void setMaxBytes(qint64 bytes) { m_maxBytes = bytes; }

    // Answers exactly once, on this object's thread, unless the fetcher is
    // deleted first: with the object, or with a short error in words
    // ("timed out", "HTTP 404", "not JSON", ...).
    using Done = std::function<void(const QJsonObject& json, const QString& error)>;
    void get(const QUrl& url, Done done);

    // What an HTTP answer carried, for callers that read error bodies too
    // (Jackie sources send a typed error envelope with a 4xx/5xx status).
    struct Reply
    {
        int status = 0;         // the HTTP status; 0 when no HTTP answer came
        QJsonObject json;       // the body, when it was a JSON object (size-bounded)
        bool isJson = false;
        QString error;          // a failure of the transfer itself, in words; empty when an answer arrived
    };
    using ReplyDone = std::function<void(const Reply&)>;
    using UrlCheck = std::function<bool(const QUrl&)>;
    // One request, no retry, finished within budgetMs in all (silence or a
    // trickle alike). Redirects are followed by hand and each target has to
    // pass `allowed` (also the first URL). Returns an id for cancel().
    quint64 getReply(const QUrl& url, int budgetMs, UrlCheck allowed, ReplyDone done);
    // Aborts that one request; its callback gets error "cancelled". Others keep running.
    void cancel(quint64 id);

    // Aborts every request in flight; their callbacks get "cancelled".
    void abortAll();

    int inFlight() const { return m_inFlight; }

private:
    void fetch(const QUrl& url, Done done, int attemptsLeft);

    QNetworkAccessManager* m_net;
    Mailbox m_mailbox;
    int m_timeoutMs = kDefaultTimeoutMs;
    int m_deadlineMs = kDefaultDeadlineMs;
    int m_attempts = kAttempts;
    qint64 m_maxBytes = kDefaultMaxBytes;
    int m_inFlight = 0;
    quint64 m_nextId = 0;
    QHash<quint64, QPointer<QObject>> m_byId;   // the QNetworkReply of each request in flight
};

} // namespace cj
