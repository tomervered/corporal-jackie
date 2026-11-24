#pragma once
// Asks a Jackie source (docs/PROTOCOL.md) and makes timeouts rare:
//  - one connection pool (JsonFetcher) per source, so a slow one never holds
//    up another;
//  - one request has one 10 s budget in all; every network try gets at most
//    what is left, and the answer (or the failure) is delivered exactly once;
//  - a second request goes out after kHedgeMs (to the next mirror, or the
//    same address once more) and the first good answer wins; the others are
//    cancelled, and only those, not other requests of the same source;
//  - a definitive error (not_found, bad_request) is final, however it came
//    (a real HTTP 404 too), anything else moves on to the next mirror at once;
//  - rate_limited keeps that address quiet for its retryAfter; another
//    mirror can still be asked;
//  - answers are kept for their ttl; when every try failed, the last good
//    copy of a catalog or title is shown (marked stale) instead of an error.
//    Streams and subtitles are never shown stale: their links can expire;
//  - the mirror that answered is asked first next time (memory only).

#include "JsonFetcher.h"

#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QVector>

#include <functional>
#include <memory>

namespace cj {

class JspClient : public QObject
{
    Q_OBJECT

public:
    static constexpr int kHedgeMs = 2500;
    static constexpr int kTimeoutMs = 10000;   // the most one request waits, in all
    static constexpr int kMaxEntries = 400;
    static constexpr qint64 kStaleKeepMs = 24LL * 60 * 60 * 1000;   // how long an old copy may stand in
    static constexpr int kDefaultCooldownS = 5;
    static constexpr int kMaxCooldownS = 300;

    explicit JspClient(QObject* parent = nullptr);

    // data and error as JsonFetcher; `stale` is true when error is empty
    // because every try failed and an older copy was used.
    using Done = std::function<void(const QJsonObject& data, const QString& error, bool stale)>;
    // bases: the source's own address first, then its mirrors.
    void get(const QString& sourceId, const QStringList& bases, const QString& path, Done done);

    // For tests: how long before the second request goes out, and the whole budget.
    void setHedgeMs(int ms) { m_hedgeMs = ms; }
    void setBudgetMs(int ms) { m_budgetMs = ms; }
    // The source was removed or its addresses changed: its saved answers, its
    // preferred mirror and its cooldowns go, and requests in flight for the
    // old setup end as "cancelled" without saving anything.
    void forget(const QString& sourceId);
    // For tests: requests of that source still on the network.
    int inFlightFor(const QString& sourceId) const { const JsonFetcher* f = m_fetchers.value(sourceId); return f ? f->inFlight() : 0; }

private:
    struct Entry
    {
        QJsonObject data;
        qint64 freshUntil = 0;   // ms since epoch
        qint64 keepUntil = 0;    // the last moment it may stand in for a failure
    };
    struct Attempt;
    JsonFetcher* fetcherFor(const QString& sourceId);
    void start(const std::shared_ptr<Attempt>& attempt);
    void handle(const std::shared_ptr<Attempt>& attempt, const QString& base, const JsonFetcher::Reply& reply);
    void complete(const std::shared_ptr<Attempt>& attempt, const QJsonObject& data, const QString& error, bool stale);
    void fail(const std::shared_ptr<Attempt>& attempt, const QString& error);

    QHash<QString, JsonFetcher*> m_fetchers;
    QHash<QString, Entry> m_cache;           // sourceId + path
    QHash<QString, QString> m_preferred;     // sourceId -> the base that answered last
    QHash<QString, qint64> m_cooldown;       // sourceId + base -> ms since epoch it may be asked again
    QHash<QString, int> m_generation;        // sourceId -> bumped by forget()
    int m_hedgeMs = kHedgeMs;
    int m_budgetMs = kTimeoutMs;
};

} // namespace cj
