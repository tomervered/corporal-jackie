#include "WikiMazeSearch.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include <QUrlQuery>

namespace cj {

namespace {

// The adapter that answers for these ids: WikiMaze's results open as any
// add-on's item, through the Stremio adapter's details and streams.
constexpr auto kAdapter = "stremio";
constexpr auto kProvider = "community.wikimaze";

QVariantMap previewMap(const MetaPreview& m)
{
    return {{"id", m.id},           {"name", m.name},
            {"type", m.type},       {"poster", m.poster},
            {"releaseInfo", m.releaseInfo}, {"description", m.description},
            {"adapter", m.adapter}, {"provider", m.provider},
            {"key", m.adapter + QLatin1Char(':') + m.id}};
}

} // namespace

WikiMazeSearch::WikiMazeSearch(QObject* parent)
    : QObject(parent)
    , m_searchFetcher(this)
    , m_filterFetcher(this)
{
    m_searchFetcher.setTimeout(kTimeoutMs);
    m_searchFetcher.setDeadline(kTimeoutMs + 2000);
    m_searchFetcher.setAttempts(1);
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(kDebounceMs);
    connect(&m_debounce, &QTimer::timeout, this, &WikiMazeSearch::run);
}

void WikiMazeSearch::reset()
{
    ++m_gen;
    m_debounce.stop();
    m_searchFetcher.abortAll();
    m_loading = false;
    m_unsupported = false;
    m_error.clear();
    m_source.clear();
    m_total = 0;
    m_items.clear();
    m_genreCounts.clear();
    m_decadeCounts.clear();
}

void WikiMazeSearch::setBase(const QString& base)
{
    if (base == m_base)
        return;
    m_base = base;
    reset();
    m_filtersAsked = false;
    m_filterFetcher.abortAll();
    m_genres.clear();
    m_lists.clear();
    emit baseChanged();
    emit filtersChanged();
    emit changed();
}

QVariantList WikiMazeSearch::results() const
{
    QVariantList out;
    for (const MetaPreview& m : m_items)
        out << previewMap(m);
    return out;
}

QUrl WikiMazeSearch::searchUrl(const QString& base, const QString& text, const QString& type, const QString& genre,
                               const QString& year, const QString& list, bool live)
{
    QUrl url(base + QStringLiteral("/api/search"));
    QUrlQuery query;
    if (!type.isEmpty())
        query.addQueryItem(QStringLiteral("type"), type);
    if (!text.trimmed().isEmpty())
        query.addQueryItem(QStringLiteral("q"), text.trimmed());
    if (!genre.isEmpty())
        query.addQueryItem(QStringLiteral("genre"), genre);
    // "1995-2003" is a range of years, anything else (2026, 2010s, Older) goes as it is.
    static const QRegularExpression range(QStringLiteral("^(\\d{4})\\s*[-–]\\s*(\\d{4})$"));
    const QRegularExpressionMatch inRange = range.match(year.trimmed());
    if (inRange.hasMatch()) {
        query.addQueryItem(QStringLiteral("from"), inRange.captured(1));
        query.addQueryItem(QStringLiteral("to"), inRange.captured(2));
    } else if (!year.trimmed().isEmpty()) {
        query.addQueryItem(QStringLiteral("year"), year.trimmed());
    }
    if (!list.isEmpty())
        query.addQueryItem(QStringLiteral("list"), list);
    if (live)
        query.addQueryItem(QStringLiteral("live"), QStringLiteral("1"));
    query.addQueryItem(QStringLiteral("limit"), QString::number(kPageSize));
    url.setQuery(query);
    return url;
}

void WikiMazeSearch::search(const QString& text, const QString& type, const QString& genre,
                            const QString& year, const QString& list, bool live)
{
    if (!available() || m_unsupported)
        return;
    m_request = Request{text.trimmed(), type, genre, year, list, live};
    if (m_request.text.isEmpty() && genre.isEmpty() && year.isEmpty() && list.isEmpty()) {
        clear();
        return;
    }
    // A request in flight is answered for nobody now.
    ++m_gen;
    m_searchFetcher.abortAll();
    m_loading = true;
    m_error.clear();
    emit changed();
    m_debounce.start();
}

void WikiMazeSearch::clear()
{
    const bool had = m_loading || !m_items.isEmpty() || !m_error.isEmpty();
    const bool wasUnsupported = m_unsupported;
    reset();
    m_unsupported = wasUnsupported;   // a 404 stays known until the address changes
    if (had)
        emit changed();
}

void WikiMazeSearch::run()
{
    const quint64 gen = ++m_gen;
    const Request r = m_request;
    const QUrl url = searchUrl(m_base, r.text, r.type, r.genre, r.year, r.list, r.live);
    m_searchFetcher.get(url, [this, gen](const QJsonObject& json, const QString& error) {
        if (gen != m_gen)
            return;
        m_loading = false;
        if (!error.isEmpty()) {
            if (error == QLatin1String("HTTP 404"))
                m_unsupported = true;   // an older WikiMaze: the add-ons' own search stays
            m_error = error;
            m_items.clear();
            emit changed();
            return;
        }
        m_error.clear();
        m_source = json.value(QStringLiteral("source")).toString();
        m_total = json.value(QStringLiteral("total")).toInt();
        m_items.clear();
        for (const QJsonValue& v : json.value(QStringLiteral("results")).toArray()) {
            const QJsonObject o = v.toObject();
            MetaPreview m;
            m.adapter = QLatin1String(kAdapter);
            m.provider = QLatin1String(kProvider);
            m.id = o.value(QStringLiteral("id")).toString();
            m.type = o.value(QStringLiteral("type")).toString();
            m.name = o.value(QStringLiteral("name")).toString();
            m.poster = o.value(QStringLiteral("poster")).toString();
            m.releaseInfo = o.value(QStringLiteral("releaseInfo")).toString();
            if (!m.id.isEmpty() && !m.name.isEmpty())
                m_items << m;
        }
        const QJsonObject facets = json.value(QStringLiteral("facets")).toObject();
        m_genreCounts = facets.value(QStringLiteral("genres")).toObject().toVariantMap();
        m_decadeCounts = facets.value(QStringLiteral("decades")).toObject().toVariantMap();
        emit changed();
    });
}

void WikiMazeSearch::loadFilters()
{
    if (!available() || m_filtersAsked)
        return;
    m_filtersAsked = true;
    const QString base = m_base;
    m_filterFetcher.get(QUrl(base + QStringLiteral("/api/genres")), [this, base](const QJsonObject& json, const QString& error) {
        if (base != m_base || !error.isEmpty())
            return;
        m_genres = json.value(QStringLiteral("genres")).toArray().toVariantList();
        emit filtersChanged();
    });
    m_filterFetcher.get(QUrl(base + QStringLiteral("/api/lists")), [this, base](const QJsonObject& json, const QString& error) {
        if (base != m_base || !error.isEmpty())
            return;
        m_lists = json.value(QStringLiteral("lists")).toArray().toVariantList();
        emit filtersChanged();
    });
}

} // namespace cj
