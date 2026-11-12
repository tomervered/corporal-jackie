#pragma once
// GETs a JSON object over HTTP(S), bounded: every request has a timeout and
// a size limit, and the JSON is parsed off the UI thread. Used by the
// Stremio adapter; one instance per adapter.

#include "Mailbox.h"

#include <QJsonObject>
#include <QObject>
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

    explicit JsonFetcher(QObject* parent = nullptr);
    ~JsonFetcher() override;

    void setTimeout(int ms) { m_timeoutMs = ms; }
    void setDeadline(int ms) { m_deadlineMs = ms; }
    void setMaxBytes(qint64 bytes) { m_maxBytes = bytes; }

    // Answers exactly once, on this object's thread, unless the fetcher is
    // deleted first: with the object, or with a short error in words
    // ("timed out", "HTTP 404", "not JSON", ...).
    using Done = std::function<void(const QJsonObject& json, const QString& error)>;
    void get(const QUrl& url, Done done);

    // Aborts every request in flight; their callbacks get "cancelled".
    void abortAll();

    int inFlight() const { return m_inFlight; }

private:
    QNetworkAccessManager* m_net;
    Mailbox m_mailbox;
    int m_timeoutMs = kDefaultTimeoutMs;
    int m_deadlineMs = kDefaultDeadlineMs;
    qint64 m_maxBytes = kDefaultMaxBytes;
    int m_inFlight = 0;
};

} // namespace cj
