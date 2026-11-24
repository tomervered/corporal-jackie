#pragma once
// Asks a Jackie source (docs/PROTOCOL.md) and makes timeouts rare:
//  - one connection pool (JsonFetcher) per source, so a slow one never holds
//    up another;
//  - a second request goes out after kHedgeMs (to the next mirror, or the
//    same address once more) and the first good answer wins;
//  - a definitive error (not_found, bad_request) is final, anything else
//    moves on to the next mirror at once;
//  - answers are kept for their ttl and, when every try failed, the last
//    good copy is shown instead of an error;
//  - the mirror that answered is asked first next time (memory only).

#include "JsonFetcher.h"

#include <QHash>
#include <QObject>

#include <functional>
#include <memory>

namespace cj {

class JspClient : public QObject
{
    Q_OBJECT

public:
    static constexpr int kHedgeMs = 2500;
    static constexpr int kTimeoutMs = 10000;   // the most one request waits
    static constexpr int kMaxEntries = 400;

    explicit JspClient(QObject* parent = nullptr);

    // data and error as JsonFetcher; `stale` is true when error is empty
    // because every try failed and an older copy was used.
    using Done = std::function<void(const QJsonObject& data, const QString& error, bool stale)>;
    // bases: the source's own address first, then its mirrors.
    void get(const QString& sourceId, const QStringList& bases, const QString& path, Done done);

    // For tests: how long before the second request goes out.
    void setHedgeMs(int ms) { m_hedgeMs = ms; }
    void forget(const QString& sourceId);   // the source was removed

private:
    struct Entry
    {
        QJsonObject data;
        qint64 freshUntil = 0;   // ms since epoch
    };
    struct Attempt;
    JsonFetcher* fetcherFor(const QString& sourceId);
    void start(const std::shared_ptr<Attempt>& attempt);

    QHash<QString, JsonFetcher*> m_fetchers;
    QHash<QString, Entry> m_cache;       // sourceId + path
    QHash<QString, QString> m_preferred; // sourceId -> the base that answered last
    int m_hedgeMs = kHedgeMs;
};

} // namespace cj
