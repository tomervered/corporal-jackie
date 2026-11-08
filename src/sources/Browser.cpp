#include "Browser.h"

#include "LocalLibrary.h"
#include "Logging.h"
#include "Session.h"
#include "StreamRanking.h"
#include "StremioAdapter.h"

#include <QLocale>
#include <QPointer>
#include <QSet>
#include <memory>

namespace cj {
namespace {

QString sizeText(qint64 bytes)
{
    return bytes > 0 ? QLocale::system().formattedDataSize(bytes, 1, QLocale::DataSizeTraditionalFormat) : QString();
}

QVariantMap previewMap(const MetaPreview& m)
{
    return {{"id", m.id},           {"name", m.name},
            {"type", m.type},       {"poster", m.poster},
            {"releaseInfo", m.releaseInfo}, {"description", m.description},
            {"adapter", m.adapter}, {"provider", m.provider},
            {"key", m.adapter + QLatin1Char(':') + m.id}};
}

} // namespace

Browser::Browser(Session* session, QObject* parent)
    : QObject(parent)
    , m_session(session)
{
}

void Browser::addAdapter(SourceAdapter* adapter)
{
    m_adapters << adapter;
    connect(adapter, &SourceAdapter::catalogsChanged, this, &Browser::rebuildCatalogs);
    rebuildCatalogs();
}

void Browser::setStremio(StremioAdapter* stremio)
{
    m_stremio = stremio;
    connect(stremio, &StremioAdapter::addonsChanged, this, &Browser::addonsChanged);
    addAdapter(stremio);
}

void Browser::setLocal(LocalLibrary* local)
{
    m_local = local;
    connect(local, &LocalLibrary::foldersChanged, this, &Browser::foldersChanged);
    connect(local, &LocalLibrary::scanningChanged, this, &Browser::foldersChanged);
    connect(local, &LocalLibrary::libraryChanged, this, [this] {
        emit foldersChanged();
        // An open local catalog shows new files at once.
        if (m_catalogIndex >= 0 && m_catalogs.value(m_catalogIndex).adapter == m_local->id())
            fetchItems(false);
    });
    addAdapter(local);
}

SourceAdapter* Browser::adapterFor(const QString& id) const
{
    for (SourceAdapter* a : m_adapters)
        if (a->id() == id)
            return a;
    return nullptr;
}

// --- Add-ons and folders ----------------------------------------------------

QVariantList Browser::addons() const
{
    QVariantList out;
    if (!m_stremio)
        return out;
    for (const InstalledAddon& a : m_stremio->addons()) {
        QStringList offers;
        for (const stremio::Resource& r : a.manifest.resources)
            offers << r.name;
        out << QVariantMap{{"id", a.manifest.id},
                           {"name", a.manifest.name},
                           {"version", a.manifest.version},
                           {"description", a.manifest.description},
                           {"url", a.manifestUrl.toDisplayString()},
                           {"offers", offers.join(QStringLiteral(", "))},
                           {"error", a.lastError}};
    }
    return out;
}

void Browser::installAddon(const QString& link)
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

void Browser::removeAddon(const QString& addonId)
{
    if (m_stremio && m_stremio->uninstall(addonId))
        emit notice(QStringLiteral("Add-on removed"), false);
}

QStringList Browser::folders() const
{
    return m_local ? m_local->folders() : QStringList{};
}

bool Browser::scanning() const
{
    return m_local && m_local->scanning();
}

int Browser::localVideoCount() const
{
    return m_local ? int(m_local->videos().size()) : 0;
}

bool Browser::addFolder(const QUrl& folder)
{
    if (!m_local)
        return false;
    const QString path = folder.isLocalFile() ? folder.toLocalFile() : folder.toString();
    const bool added = m_local->addFolder(path);
    if (!added)
        emit notice(QStringLiteral("That folder is already in the list, or it can't be read."), true);
    return added;
}

void Browser::removeFolder(const QString& path)
{
    if (m_local)
        m_local->removeFolder(path);
}

void Browser::rescanFolders()
{
    if (m_local)
        m_local->rescan();
}

// --- Catalogs and items -------------------------------------------------------

void Browser::rebuildCatalogs()
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
    if (lost) {
        ++m_itemsGen;
        m_items.clear();
        m_itemsLoading = false;
        m_canLoadMore = false;
        emit itemsChanged();
    }
}

QVariantList Browser::catalogs() const
{
    QVariantList out;
    for (const CatalogRef& c : m_catalogs)
        out << QVariantMap{{"name", c.name}, {"type", c.type}, {"adapter", c.adapter},
                           {"searchable", c.searchable}, {"searchOnly", c.searchOnly}};
    return out;
}

QVariantList Browser::items() const
{
    QVariantList out;
    for (const MetaPreview& m : m_items)
        out << previewMap(m);
    return out;
}

void Browser::openCatalog(int index, const QString& search)
{
    if (index < 0 || index >= m_catalogs.size())
        return;
    m_catalogIndex = index;
    m_search = m_catalogs[index].searchable ? search.trimmed() : QString();
    fetchItems(false);
}

void Browser::loadMore()
{
    if (m_canLoadMore && !m_itemsLoading)
        fetchItems(true);
}

void Browser::fetchItems(bool append)
{
    const CatalogRef catalog = m_catalogs.value(m_catalogIndex);
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

void Browser::openItem(int index)
{
    if (index < 0 || index >= m_items.size())
        return;
    openPreview(m_items[index]);
}

void Browser::openPreview(const MetaPreview& item)
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

QVariantMap Browser::detail() const
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

QVariantList Browser::videos() const
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

// --- Streams ----------------------------------------------------------------------

void Browser::openVideo(int index)
{
    if (!m_detail)
        return;
    QString videoId = m_detail->preview.id;
    QString title = m_detail->preview.name;
    m_streamsVideo = VideoEntry{videoId, title, 0, 0, {}};
    if (index >= 0) {
        if (index >= m_detail->videos.size())
            return;
        const VideoEntry& v = m_detail->videos[index];
        m_streamsVideo = v;
        videoId = v.id;
        title = v.season > 0 ? QStringLiteral("%1 %2x%3").arg(title).arg(v.season).arg(v.episode, 2, 10, QLatin1Char('0'))
                             : title + QStringLiteral(" · ") + v.title;
    }
    const QString type = m_detail->preview.type;
    const quint64 gen = ++m_streamsGen;
    m_streamsTitle = title;
    m_streamsType = type;
    m_streamsVideoId = videoId;
    m_streams.clear();
    m_streamProblems.clear();
    m_streamsLoading = true;
    emit streamsChanged();

    // Every adapter is asked; each answers for what it knows. Results are
    // merged and re-ranked as each provider answers.
    auto left = std::make_shared<int>(int(m_adapters.size()));
    for (SourceAdapter* a : std::as_const(m_adapters)) {
        a->fetchStreams(
            type, videoId,
            [this, gen, title](const QList<StreamOption>& streams, const QString& provider, const QString& error) {
                if (gen != m_streamsGen)
                    return;
                if (!error.isEmpty())
                    m_streamProblems << provider + QStringLiteral(": ") + error;
                for (StreamOption s : streams) {
                    if (s.source.title.isEmpty())
                        s.source.title = title;
                    m_streams << s;
                }
                ranking::sortBest(m_streams);
                if (m_streams.size() > 500)
                    m_streams.resize(500);
                emit streamsChanged();
            },
            [this, gen, left] {
                if (gen != m_streamsGen || --*left > 0)
                    return;
                m_streamsLoading = false;
                emit streamsChanged();
            });
    }
    if (m_adapters.isEmpty()) {
        m_streamsLoading = false;
        emit streamsChanged();
    }
}

QVariantList Browser::streams() const
{
    QVariantList out;
    for (const StreamOption& s : m_streams) {
        QStringList facts;
        if (s.seeders >= 0)
            facts << QStringLiteral("%1 seeders").arg(s.seeders);
        if (s.sizeBytes > 0)
            facts << sizeText(s.sizeBytes);
        facts << (s.source.kind == PlayableSource::Kind::Torrent ? QStringLiteral("torrent")
                  : s.source.kind == PlayableSource::Kind::Http  ? QStringLiteral("web")
                                                                 : QStringLiteral("file"));
        if (!s.source.subtitles.isEmpty())
            facts << QStringLiteral("%1 subtitles").arg(s.source.subtitles.size());
        out << QVariantMap{{"group", ranking::qualityLabel(s.quality)},
                           {"provider", s.providerName},
                           {"name", QString(s.name).replace(QLatin1Char('\n'), QLatin1Char(' '))},
                           {"description", s.description},
                           {"facts", facts.join(QStringLiteral(" · "))}};
    }
    return out;
}

void Browser::play(int streamIndex)
{
    if (streamIndex < 0 || streamIndex >= m_streams.size() || !m_session)
        return;
    const StreamOption choice = m_streams[streamIndex];
    qCInfo(lcSources).noquote() << "Playing" << choice.source.title << "from" << choice.providerName
                                << choice.source.url.toDisplayString(QUrl::RemoveQuery);
    // Another source of the video that is playing: go on from the same
    // place instead of from the start.
    double start = 0;
    if (m_playingVideoId == m_streamsVideoId && m_session->source() == m_playingUrl
        && (m_session->hasMedia() || m_session->loading()))
        start = m_session->position();
    if (m_detail)
        emit aboutToPlay(PlayContext{m_detail->preview, m_streamsVideo});
    m_session->playSource(choice.source, start);
    m_playingVideoId = m_streamsVideoId;
    m_playingUrl = m_session->source();
    emit playStarted(choice.source.title);

    // Subtitle add-ons answer after the video starts; their files are added
    // as tracks to this item only.
    const QUrl playing = m_session->source();
    for (SourceAdapter* a : std::as_const(m_adapters)) {
        QPointer<Session> session(m_session);
        a->fetchSubtitles(m_streamsType, m_streamsVideoId, [session, playing](const QList<SubtitleSource>& subs) {
            if (session && !subs.isEmpty())
                session->addSubtitleTracks(subs, playing);
        });
    }
}

} // namespace cj
