#include "CatalogBrowser.h"

#include "LocalLibrary.h"
#include "Logging.h"
#include "PlaybackSession.h"
#include "StremioAdapter.h"
#include "WikiMazeSearch.h"

#include <QSet>
#include <QTimer>

#include <memory>

namespace cj {
namespace {

QVariantMap previewMap(const MetaPreview& m)
{
    return {{"id", m.id},           {"name", m.name},
            {"type", m.type},       {"poster", m.poster},
            {"releaseInfo", m.releaseInfo}, {"description", m.description},
            {"adapter", m.adapter}, {"provider", m.provider},
            {"key", m.adapter + QLatin1Char(':') + m.id}};
}

} // namespace

CatalogBrowser::CatalogBrowser(PlaybackSession* session, QObject* parent)
    : QObject(parent)
    , m_session(session)
    , m_wikimaze(new WikiMazeSearch(this))
{
    // An older WikiMaze answers 404: the add-ons' own search rows come back.
    connect(m_wikimaze, &WikiMazeSearch::changed, this, [this] {
        if (m_wikimaze->unsupported() && !m_searchAllText.isEmpty() && !m_wikimazeRowsKept)
            searchAll(m_searchAllText, m_searchType);
    });
    if (m_session)
        connect(m_session, &PlaybackSession::sourceChanged, this, [this] {
            if (m_playingShow && m_session->source() != m_playingShowUrl) {
                ++m_playingGen;
                m_playingShow.reset();
                emit playingShowChanged();
            }
        });
}

void CatalogBrowser::addAdapter(SourceAdapter* adapter)
{
    m_adapters << adapter;
    connect(adapter, &SourceAdapter::catalogsChanged, this, &CatalogBrowser::rebuildCatalogs);
    rebuildCatalogs();
}

void CatalogBrowser::setStremio(StremioAdapter* stremio)
{
    m_stremio = stremio;
    connect(stremio, &StremioAdapter::addonsChanged, this, &CatalogBrowser::addonsChanged);
    connect(stremio, &StremioAdapter::addonsChanged, this, [this] { m_wikimaze->setBase(m_stremio->wikimazeBase()); });
    m_wikimaze->setBase(stremio->wikimazeBase());
    addAdapter(stremio);
}

void CatalogBrowser::setLocal(LocalLibrary* local)
{
    m_local = local;
    connect(local, &LocalLibrary::foldersChanged, this, &CatalogBrowser::foldersChanged);
    connect(local, &LocalLibrary::scanningChanged, this, &CatalogBrowser::foldersChanged);
    connect(local, &LocalLibrary::libraryChanged, this, [this] {
        emit foldersChanged();
        // An open local catalog shows new files at once.
        if (m_catalogIndex >= 0 && m_catalogs.value(m_catalogIndex).adapter == m_local->id())
            fetchItems(false);
    });
    addAdapter(local);
}

// Who may hear an item's ids: every adapter, unless its own adapter's ids
// are private (a local file's path), and then only that one.
QList<SourceAdapter*> CatalogBrowser::adaptersAskedAbout(const QString& owner) const
{
    if (SourceAdapter* own = adapterFor(owner); own && own->idsArePrivate())
        return {own};
    return m_adapters;
}

SourceAdapter* CatalogBrowser::adapterFor(const QString& id) const
{
    for (SourceAdapter* a : m_adapters)
        if (a->id() == id)
            return a;
    return nullptr;
}

// --- Add-ons and folders ----------------------------------------------------

QVariantList CatalogBrowser::addons() const
{
    QVariantList out;
    if (!m_stremio)
        return out;
    for (const InstalledAddon& a : m_stremio->addons()) {
        QVariantList catalogs;
        for (const stremio::Catalog& c : a.manifest.catalogs)
            catalogs << QVariantMap{{"type", c.type}, {"id", c.id}, {"name", c.name},
                                    {"enabled", !a.hiddenCatalogs.contains(c.type + QLatin1Char('/') + c.id)}};
        // The lists (and details) now shown from an old copy, in words.
        QStringList cachedIn;
        for (const QString& view : a.cachedViews) {
            QString label = view == QLatin1String("meta") ? QStringLiteral("details") : view;
            for (const stremio::Catalog& c : a.manifest.catalogs)
                if (view == QStringLiteral("catalog/") + c.type + QLatin1Char('/') + c.id && !c.name.isEmpty())
                    label = c.name;
            cachedIn << label;
        }
        QStringList offers;
        for (const stremio::Resource& r : a.manifest.resources)
            offers << r.name;
        out << QVariantMap{{"id", a.manifest.id},
                           {"name", a.manifest.name},
                           {"version", a.manifest.version},
                           {"description", a.manifest.description},
                           {"url", logging::redactUrl(a.manifestUrl)},  // its path can hold a login
                           {"offers", offers.join(QStringLiteral(", "))},
                           {"protocol", a.jsp ? QStringLiteral("Jackie Source Protocol v1") : QStringLiteral("Stremio")},
                           {"error", a.lastError},
                           {"cached", !a.cachedViews.isEmpty()},
                           {"cachedIn", cachedIn.join(QStringLiteral(", "))},
                           {"enabled", !a.disabled},
                           {"catalogs", catalogs}};
    }
    return out;
}

void CatalogBrowser::installAddon(const QString& link)
{
    if (!m_stremio)
        return;
    ++m_installing;
    emit addonsChanged();
    m_stremio->install(link, [this](const QString& error) {
        --m_installing;
        emit addonsChanged();
        emit notice(error.isEmpty() ? QStringLiteral("Add-on installed") : error, !error.isEmpty());
    });
}

void CatalogBrowser::moveAddon(const QString& addonId, int delta)
{
    if (m_stremio)
        m_stremio->move(addonId, delta);
}

void CatalogBrowser::setCatalogEnabled(const QString& addonId, const QString& type, const QString& catalogId, bool enabled)
{
    if (m_stremio)
        m_stremio->setCatalogEnabled(addonId, type, catalogId, enabled);
}

void CatalogBrowser::setAddonEnabled(const QString& addonId, bool enabled)
{
    if (m_stremio && m_stremio->setEnabled(addonId, enabled))
        emit notice(enabled ? QStringLiteral("Add-on switched on") : QStringLiteral("Add-on switched off"), false);
}

void CatalogBrowser::removeAddon(const QString& addonId)
{
    if (m_stremio && m_stremio->uninstall(addonId))
        emit notice(QStringLiteral("Add-on removed"), false);
}

QStringList CatalogBrowser::folders() const
{
    return m_local ? m_local->folders() : QStringList{};
}

bool CatalogBrowser::scanning() const
{
    return m_local && m_local->scanning();
}

int CatalogBrowser::localVideoCount() const
{
    return m_local ? int(m_local->videos().size()) : 0;
}

bool CatalogBrowser::addFolder(const QUrl& folder)
{
    if (!m_local)
        return false;
    const QString path = folder.isLocalFile() ? folder.toLocalFile() : folder.toString();
    const bool added = m_local->addFolder(path);
    if (!added)
        emit notice(QStringLiteral("That folder is already in the list, or it can't be read."), true);
    return added;
}

void CatalogBrowser::removeFolder(const QString& path)
{
    if (m_local)
        m_local->removeFolder(path);
}

void CatalogBrowser::rescanFolders()
{
    if (m_local)
        m_local->rescan();
}

// --- Catalogs and items -------------------------------------------------------

void CatalogBrowser::rebuildCatalogs()
{
    const CatalogRef current = m_catalogs.value(m_catalogIndex);
    m_catalogs.clear();
    for (SourceAdapter* a : std::as_const(m_adapters))
        m_catalogs << a->catalogs();
    // Keep the open catalog open if it still exists.
    int index = -1;
    for (int i = 0; i < m_catalogs.size(); ++i)
        if (m_catalogs[i].adapter == current.adapter && m_catalogs[i].provider == current.provider
            && m_catalogs[i].id == current.id && m_catalogs[i].type == current.type)
            index = i;
    const bool lost = m_catalogIndex >= 0 && index < 0;
    m_catalogIndex = index;
    emit catalogsChanged();
    // The home rows follow the lists: an add-on installed, removed or moved.
    QStringList keys;
    for (const CatalogRef& c : std::as_const(m_catalogs))
        keys << c.adapter + c.provider + c.type + c.id;
    if (m_homeLoaded && keys != m_homeKeys)
        QTimer::singleShot(0, this, &CatalogBrowser::loadHome);
    if (lost) {
        ++m_itemsGen;
        m_items.clear();
        m_itemsLoading = false;
        m_canLoadMore = false;
        emit itemsChanged();
    }
}

QVariantList CatalogBrowser::catalogs() const
{
    QVariantList out;
    for (const CatalogRef& c : m_catalogs)
        out << QVariantMap{{"name", c.name}, {"type", c.type}, {"adapter", c.adapter}, {"provider", c.provider},
                           {"searchable", c.searchable}, {"searchOnly", c.searchOnly},
                           {"genres", c.genres}, {"genreRequired", c.genreRequired}, {"years", c.years}};
    return out;
}

QVariantList CatalogBrowser::items() const
{
    QVariantList out;
    for (const MetaPreview& m : m_items)
        out << previewMap(m);
    return out;
}

void CatalogBrowser::openCatalog(int index, const QString& search, const QString& genre, const QString& year)
{
    if (index < 0 || index >= m_catalogs.size())
        return;
    m_catalogIndex = index;
    m_search = m_catalogs[index].searchable ? search.trimmed() : QString();
    m_genre = m_catalogs[index].genres.contains(genre) ? genre : QString();
    m_year = m_catalogs[index].years.contains(year) ? year : QString();
    fetchItems(false);
}

// --- Home and search rows -----------------------------------------------------------

QVariantList CatalogBrowser::rowsMap(const QList<Row>& rows)
{
    QVariantList out;
    for (const Row& r : rows) {
        QVariantList items;
        for (const MetaPreview& m : r.items)
            items << previewMap(m);
        out << QVariantMap{{"catalog", r.catalog}, {"name", r.name}, {"type", r.type}, {"items", items},
                           {"loading", r.loading}, {"error", r.error}};
    }
    return out;
}

void CatalogBrowser::fetchRows(QList<Row>* rows, quint64* gen, const QString& search, void (CatalogBrowser::*changed)())
{
    const quint64 mine = ++*gen;
    for (int i = 0; i < rows->size(); ++i) {
        CatalogRef catalog = m_catalogs.value((*rows)[i].catalog);
        SourceAdapter* adapter = adapterFor(catalog.adapter);
        if (!adapter) {
            (*rows)[i].loading = false;
            continue;
        }
        adapter->fetchCatalog(catalog, search, 0,
                              [this, rows, gen, mine, i, changed](const QList<MetaPreview>& items, const QString& error) {
            if (*gen != mine || i >= rows->size())
                return;  // a newer load replaced these rows
            Row& row = (*rows)[i];
            row.loading = false;
            row.error = error;
            row.items = items.mid(0, kRowItems);
            emit (this->*changed)();
        });
    }
    emit (this->*changed)();
}

void CatalogBrowser::loadHome()
{
    m_homeLoaded = true;
    m_homeRows.clear();
    // Every list, not only those shown: a change to any of them reloads Home.
    m_homeKeys.clear();
    for (const CatalogRef& c : std::as_const(m_catalogs))
        m_homeKeys << c.adapter + c.provider + c.type + c.id;
    for (int i = 0; i < m_catalogs.size() && m_homeRows.size() < kHomeRows; ++i) {
        const CatalogRef& c = m_catalogs[i];
        if (c.searchOnly)
            continue;
        Row row;
        row.catalog = i;
        row.name = c.name;
        row.type = c.type;
        m_homeRows << row;
    }
    fetchRows(&m_homeRows, &m_homeGen, {}, &CatalogBrowser::homeChanged);
}

void CatalogBrowser::searchAll(const QString& text, const QString& type)
{
    m_searchRows.clear();
    m_searchAllText = text.trimmed();
    m_searchType = type;
    // While WikiMaze's own search answers, its per-list search rows would
    // show the same titles again.
    const bool wikimazeRow = m_wikimaze->available() && !m_wikimaze->unsupported();
    m_wikimazeRowsKept = !wikimazeRow;
    if (!m_searchAllText.isEmpty()) {
        for (int i = 0; i < m_catalogs.size(); ++i) {
            const CatalogRef& c = m_catalogs[i];
            if (!c.searchable || (!type.isEmpty() && c.type != type))
                continue;
            if (wikimazeRow && c.provider.startsWith(QLatin1String("community.wikimaze")))
                continue;
            Row row;
            row.catalog = i;
            row.name = c.name;
            row.type = c.type;
            m_searchRows << row;
        }
    }
    fetchRows(&m_searchRows, &m_searchGen, m_searchAllText, &CatalogBrowser::searchRowsChanged);
}

void CatalogBrowser::openRowItem(bool searchRow, int row, int index)
{
    const QList<Row>& rows = searchRow ? m_searchRows : m_homeRows;
    if (row < 0 || row >= rows.size() || index < 0 || index >= rows[row].items.size())
        return;
    openPreview(rows[row].items[index]);
}

void CatalogBrowser::openWikimazeItem(int index)
{
    const QList<MetaPreview> items = m_wikimaze->items();
    if (index >= 0 && index < items.size())
        openPreview(items[index]);
}

QObject* CatalogBrowser::wikimaze() const { return m_wikimaze; }

void CatalogBrowser::loadMore()
{
    if (m_canLoadMore && !m_itemsLoading)
        fetchItems(true);
}

void CatalogBrowser::fetchItems(bool append)
{
    CatalogRef catalog = m_catalogs.value(m_catalogIndex);
    catalog.genre = m_genre;
    catalog.year = m_year;
    SourceAdapter* adapter = adapterFor(catalog.adapter);
    const quint64 gen = ++m_itemsGen;
    if (!append)
        m_items.clear();
    m_itemsError.clear();
    m_canLoadMore = false;
    if (!adapter || (catalog.searchOnly && m_search.isEmpty())) {
        m_itemsLoading = false;
        emit itemsChanged();
        return;
    }
    m_itemsLoading = true;
    emit itemsChanged();
    adapter->fetchCatalog(catalog, m_search, append ? int(m_items.size()) : 0,
                          [this, gen](const QList<MetaPreview>& items, const QString& error) {
        if (gen != m_itemsGen)
            return;  // the user moved on
        m_itemsLoading = false;
        m_itemsError = error;
        // Some add-ons repeat items across pages.
        QSet<QString> seen;
        for (const MetaPreview& m : std::as_const(m_items))
            seen.insert(m.id);
        int added = 0;
        for (const MetaPreview& m : items) {
            if (!seen.contains(m.id)) {
                m_items << m;
                seen.insert(m.id);
                ++added;
            }
        }
        m_canLoadMore = error.isEmpty() && added > 0 && m_items.size() < 2000;
        emit itemsChanged();
    });
}

// --- Details --------------------------------------------------------------------

void CatalogBrowser::openItem(int index)
{
    if (index < 0 || index >= m_items.size())
        return;
    openPreview(m_items[index]);
}

void CatalogBrowser::openPreview(const MetaPreview& item)
{
    SourceAdapter* adapter = adapterFor(item.adapter);
    if (!adapter)
        return;
    const quint64 gen = ++m_detailGen;
    ++m_streamsGen;
    m_streams.clear();
    m_streamProblems.clear();
    m_streamsLoading = false;
    m_streamsTitle.clear();
    emit streamsChanged();
    m_detail = MetaDetail{item, {}, {}, {}, {}};
    m_detailLoading = true;
    m_detailError.clear();
    emit detailChanged();
    adapter->fetchMeta(item, [this, gen](const std::optional<MetaDetail>& meta, const QString& error) {
        if (gen != m_detailGen)
            return;
        m_detailLoading = false;
        m_detailError = error;
        if (meta)
            m_detail = meta;
        emit detailChanged();
    });
}

QVariantMap CatalogBrowser::detail() const
{
    if (!m_detail)
        return {};
    QVariantMap out = previewMap(m_detail->preview);
    out.insert("background", m_detail->background);
    out.insert("runtime", m_detail->runtime);
    out.insert("genres", m_detail->genres.join(QStringLiteral(", ")));
    out.insert("isSeries", !m_detail->videos.isEmpty());
    return out;
}

QVariantList CatalogBrowser::videos() const
{
    QVariantList out;
    if (!m_detail)
        return out;
    for (const VideoEntry& v : m_detail->videos) {
        const QString number = v.episode > 0 ? QStringLiteral("%1. ").arg(v.episode) : QString();
        out << QVariantMap{{"id", v.id},
                           {"label", number + v.title},
                           {"season", v.season > 0 ? QStringLiteral("Season %1").arg(v.season) : QStringLiteral("Specials")},
                           {"released", v.released}};
    }
    return out;
}

} // namespace cj
