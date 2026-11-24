#include "StremioAdapter.h"
#include "JspProtocol.h"

#include "Logging.h"

#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QTimer>

#include <memory>

namespace cj {
namespace {

// Used only until the first refresh fetches the real manifests: enough for
// the default add-ons' lists to show on a first run. A copy of
// https://wikimaze-seven.vercel.app/manifest.json (2026-11-17), refreshed 2026-11-24 (version 0.2.0: Brand New, year filter).
const char kDefaultManifest[] = R"json({"id":"community.wikimaze","version":"0.2.0","name":"WikiMaze","description":"Movie and series details from Wikidata (CC0) and TVmaze (CC BY-SA), titles in your language. Search ends with \"!\" to ask Wikidata and TVmaze directly. TV data from TVmaze.com, licensed CC BY-SA 4.0.","resources":["catalog",{"name":"meta","types":["movie","series"],"idPrefixes":["tt"]}],"types":["movie","series"],"idPrefixes":["tt"],"catalogs":[{"type":"movie","id":"wikimaze-trending","name":"WikiMovies - Trending","extra":[{"name":"genre","options":["Action","Adventure","Animation","Biography","Comedy","Crime","Documentary","Drama","Family","Fantasy","History","Horror","Musical","Mystery","Romance","Sci-Fi","Sports","Superhero","Thriller","War","Western"]},{"name":"year","options":["2026","2025","2024","2023","2022","2020s","2010s","2000s","1990s","1980s","1970s","Older"]},{"name":"search"},{"name":"skip"}]},{"type":"series","id":"wikimaze-trending","name":"WikiSeries - Trending","extra":[{"name":"genre","options":["Action","Adventure","Animation","Anime","Comedy","Crime","Documentary","Drama","Family","Fantasy","History","Horror","Medical","Mystery","Romance","Sci-Fi","Sports","Thriller","War","Western"]},{"name":"year","options":["2026","2025","2024","2023","2022","2020s","2010s","2000s","1990s","1980s","1970s","Older"]},{"name":"search"},{"name":"skip"}]},{"type":"movie","id":"wikimaze-hot","name":"WikiMovies - Surging","extra":[{"name":"genre","options":["Action","Adventure","Animation","Biography","Comedy","Crime","Documentary","Drama","Family","Fantasy","History","Horror","Musical","Mystery","Romance","Sci-Fi","Sports","Superhero","Thriller","War","Western"]},{"name":"year","options":["2026","2025","2024","2023","2022","2020s","2010s","2000s","1990s","1980s","1970s","Older"]},{"name":"search"},{"name":"skip"}]},{"type":"series","id":"wikimaze-hot","name":"WikiSeries - Surging","extra":[{"name":"genre","options":["Action","Adventure","Anime","Children","Comedy","Crime","Documentary","Drama","Family","Fantasy","History","Horror","Legal","Medical","Mystery","Romance","Sci-Fi","Sports","Supernatural","Thriller","War","Western"]},{"name":"year","options":["2026","2025","2024","2023","2022","2020s","2010s","2000s","1990s","1980s","1970s","Older"]},{"name":"search"},{"name":"skip"}]},{"type":"movie","id":"wikimaze-new","name":"WikiMovies - Brand New","extra":[{"name":"genre","options":["Action","Adventure","Animation","Biography","Comedy","Crime","Documentary","Drama","Family","Fantasy","History","Horror","Musical","Mystery","Romance","Sci-Fi","Sports","Superhero","Thriller","War","Western"]},{"name":"year","options":["2026","2025","2024","2023","2022","2020s","2010s","2000s","1990s","1980s","1970s","Older"]},{"name":"search"},{"name":"skip"}]},{"type":"series","id":"wikimaze-new","name":"WikiSeries - Brand New","extra":[{"name":"genre","options":["Action","Adventure","Anime","Children","Comedy","Crime","Documentary","Drama","Family","Fantasy","History","Horror","Legal","Medical","Mystery","Romance","Sci-Fi","Sports","Supernatural","Thriller","War","Western"]},{"name":"year","options":["2026","2025","2024","2023","2022","2020s","2010s","2000s","1990s","1980s","1970s","Older"]},{"name":"search"},{"name":"skip"}]},{"type":"movie","id":"wikimaze-popular","name":"WikiMovies - Classics","extra":[{"name":"genre","options":["Action","Adventure","Animation","Biography","Comedy","Crime","Documentary","Drama","Family","Fantasy","History","Horror","Musical","Mystery","Romance","Sci-Fi","Sports","Superhero","Thriller","War","Western"]},{"name":"year","options":["2026","2025","2024","2023","2022","2020s","2010s","2000s","1990s","1980s","1970s","Older"]},{"name":"search"},{"name":"skip"}]},{"type":"series","id":"wikimaze-classics","name":"WikiSeries - Classics","extra":[{"name":"genre","options":["Action","Adventure","Animation","Anime","Comedy","Crime","Documentary","Drama","Family","Fantasy","History","Horror","Medical","Mystery","Romance","Sci-Fi","Sports","Thriller","War","Western"]},{"name":"year","options":["2026","2025","2024","2023","2022","2020s","2010s","2000s","1990s","1980s","1970s","Older"]},{"name":"search"},{"name":"skip"}]},{"type":"series","id":"wikimaze-airing","name":"WikiSeries - Airing now","extra":[{"name":"genre","options":["Action","Adventure","Anime","Children","Comedy","Crime","Documentary","Drama","Family","Fantasy","History","Horror","Legal","Medical","Mystery","Romance","Sci-Fi","Sports","Supernatural","Thriller","War","Western"]},{"name":"year","options":["2026","2025","2024","2023","2022","2020s","2010s","2000s","1990s","1980s","1970s","Older"]},{"name":"search"},{"name":"skip"}]}],"behaviorHints":{"configurable":true}})json";

// A copy of https://caching.stremio.net/publicdomainmovies.now.sh/manifest.json (2026-11-20).
const char kPublicDomainManifest[] = R"json({"id":"org.stremio.pubdomainmovies","version":"1.0.0","name":"Public Domain Movies","description":"Torrents for public domain movies. Includes a collection of more then 500 public domain movies.","logo":"https://johnserv531.github.io/gh-images/1586344884.jpg","resources":["catalog","stream"],"types":["movie"],"catalogs":[{"type":"movie","id":"publicdomainmovies","extra":[{"name":"skip"},{"name":"search"}]}],"idPrefixes":["tt"]})json";

// A Jackie source is held to one address rule (https, or loopback http for
// development) for its own address, its mirrors and its redirects. A Stremio
// add-on is left as it always was. Empty when fine.
QString jspAddressProblem(const QUrl& manifestUrl, const QJsonObject& raw)
{
    if (!jsp::isJsp(raw) || jsp::allowedUrl(manifestUrl))
        return {};
    return QStringLiteral("A Jackie source has to use https (plain http only on this computer).");
}

struct DefaultAddon
{
    const char* id;
    const char* url;
    const char* manifest;
};
const DefaultAddon kDefaults[] = {
    {StremioAdapter::kDefaultAddonId, StremioAdapter::kDefaultAddonUrl, kDefaultManifest},
    {StremioAdapter::kPublicDomainId, StremioAdapter::kPublicDomainUrl, kPublicDomainManifest},
};

QString typeWord(const QString& type)
{
    if (type == QLatin1String("movie"))
        return QStringLiteral("Movies");
    if (type == QLatin1String("series"))
        return QStringLiteral("Series");
    if (type == QLatin1String("tv"))
        return QStringLiteral("TV");
    if (type == QLatin1String("channel"))
        return QStringLiteral("Channels");
    return type.isEmpty() ? QString() : type.at(0).toUpper() + type.mid(1);
}

} // namespace

StremioAdapter::StremioAdapter(const QString& storeFile, bool preinstall, QObject* parent)
    : SourceAdapter(parent)
    , m_storeFile(storeFile)
{
    load(preinstall);
}

void StremioAdapter::load(bool preinstall)
{
    if (m_storeFile.isEmpty())
        return;
    QFile file(m_storeFile);
    if (!file.exists()) {
        if (preinstall)
            installDefaults();
        return;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        qCWarning(lcSources).noquote() << "Could not read" << m_storeFile << file.errorString();
        return;
    }
    restore(QJsonDocument::fromJson(file.read(16 * 1024 * 1024)).object().value("addons").toArray());
}

QStringList StremioAdapter::defaultAddonIds()
{
    QStringList ids;
    for (const DefaultAddon& d : kDefaults)
        ids << QString::fromLatin1(d.id);
    return ids;
}

void StremioAdapter::installDefaults(const QStringList& only)
{
    bool added = false;
    for (const DefaultAddon& d : kDefaults) {
        const QString id = QString::fromLatin1(d.id);
        if ((!only.isEmpty() && !only.contains(id)) || find(id) || m_addons.size() >= kMaxAddons)
            continue;
        QString error;
        const QJsonObject raw = QJsonDocument::fromJson(d.manifest).object();
        if (auto m = jsp::parseAnyManifest(raw, &error)) {
            setManifest(QUrl(QString::fromLatin1(d.url)), *m, raw);
            added = true;
        } else
            qCWarning(lcSources).noquote() << "Built-in manifest for" << d.id << "is bad:" << error;
    }
    if (!added)
        return;
    save();
    emit addonsChanged();
    emit catalogsChanged();
}

bool StremioAdapter::uninstallDefaults()
{
    bool removed = false;
    for (const DefaultAddon& d : kDefaults)
        removed = uninstall(QString::fromLatin1(d.id)) || removed;
    return removed;
}

QJsonArray StremioAdapter::storedList() const
{
    QJsonArray list;
    for (const InstalledAddon& a : m_addons) {
        QJsonObject entry{{"transportUrl", a.manifestUrl.toString()}, {"manifest", a.rawManifest}};
        if (a.disabled)
            entry.insert("disabled", true);
        if (!a.hiddenCatalogs.isEmpty())
            entry.insert("hiddenCatalogs", QJsonArray::fromStringList(a.hiddenCatalogs));
        list.append(entry);
    }
    return list;
}

void StremioAdapter::restore(const QJsonArray& list)
{
    m_addons.clear();
    for (const QJsonValue& v : list) {
        const QJsonObject o = v.toObject();
        const QUrl url(o.value("transportUrl").toString());
        const QJsonObject raw = o.value("manifest").toObject();
        QString error;
        const QString addressProblem = jspAddressProblem(url, raw);
        if (auto m = jsp::parseAnyManifest(raw, &error); m && url.isValid() && m_addons.size() < kMaxAddons && addressProblem.isEmpty()) {
            setManifest(url, *m, raw);
            if (o.value("disabled").toBool())
                m_addons.last().disabled = true;
            for (const QJsonValue& h : o.value("hiddenCatalogs").toArray())
                m_addons.last().hiddenCatalogs << h.toString();
        } else
            qCWarning(lcSources).noquote() << "Skipping a stored add-on:" << logging::redactUrl(url) << (error.isEmpty() ? addressProblem : error);
    }
    qCInfo(lcSources) << "Add-ons installed:" << m_addons.size();
    emit addonsChanged();
    emit catalogsChanged();
}

void StremioAdapter::save() const
{
    if (m_storeFile.isEmpty())
        return;
    QDir().mkpath(QFileInfo(m_storeFile).absolutePath());
    QSaveFile file(m_storeFile);
    if (!file.open(QIODevice::WriteOnly)
        || file.write(QJsonDocument(QJsonObject{{"version", 1}, {"addons", storedList()}}).toJson()) < 0 || !file.commit())
        qCWarning(lcSources).noquote() << "Could not save" << m_storeFile << file.errorString();
}

void StremioAdapter::setManifest(const QUrl& url, const stremio::Manifest& manifest, const QJsonObject& raw)
{
    const bool isJsp = jsp::isJsp(raw);
    const QStringList mirrors = isJsp ? jsp::mirrors(raw) : QStringList();
    for (InstalledAddon& a : m_addons) {
        if (a.manifest.id == manifest.id || a.manifestUrl == url) {
            // New addresses: nothing saved from the old ones may be shown or reused.
            if (a.jsp && (a.manifestUrl != url || a.mirrors != mirrors || !isJsp))
                m_jsp.forget(a.manifest.id);
            a.showingCached = false;
            a.manifestUrl = url;
            a.manifest = manifest;
            a.rawManifest = raw;
            a.jsp = isJsp;
            a.mirrors = mirrors;
            a.lastError.clear();
            return;
        }
    }
    InstalledAddon a;
    a.manifestUrl = url;
    a.manifest = manifest;
    a.rawManifest = raw;
    a.jsp = isJsp;
    a.mirrors = mirrors;
    m_addons.append(a);
}

void StremioAdapter::ask(const InstalledAddon& addon, const QUrl& stremioUrl, const QString& jspPath,
                         QJsonObject (*asStremio)(const QJsonObject&), JsonFetcher::Done done)
{
    if (!addon.jsp) {
        m_fetcher.get(stremioUrl, std::move(done));
        return;
    }
    const QString addonId = addon.manifest.id;
    const QString primary = stremio::baseOf(addon.manifestUrl);
    // The same address rule for the primary as for the mirrors, before any request.
    if (!jsp::allowedUrl(QUrl(primary))) {
        QTimer::singleShot(0, this, [done = std::move(done)] {
            done({}, QStringLiteral("a Jackie source has to use https"));
        });
        return;
    }
    QStringList bases{primary};
    bases << addon.mirrors;
    m_jsp.get(addonId, bases, jspPath, [this, addonId, asStremio, done = std::move(done)](const QJsonObject& data, const QString& error, bool stale) {
        if (stale)
            qCWarning(lcSources) << "A Jackie source did not answer; showing its last good answer";
        if (error != QLatin1String("cancelled"))
            noteCached(addonId, stale);
        done(error.isEmpty() ? asStremio(data) : QJsonObject(), error);
    });
}

const InstalledAddon* StremioAdapter::find(const QString& addonId) const
{
    for (const InstalledAddon& a : m_addons)
        if (a.manifest.id == addonId)
            return &a;
    return nullptr;
}

QList<const InstalledAddon*> StremioAdapter::serving(const QString& resource, const QString& type,
                                                     const QString& id) const
{
    QList<const InstalledAddon*> out;
    for (const InstalledAddon& a : m_addons)
        if (!a.disabled && a.manifest.serves(resource, type, id))
            out << &a;
    return out;
}

void StremioAdapter::noteCached(const QString& addonId, bool cached)
{
    for (InstalledAddon& a : m_addons) {
        if (a.manifest.id == addonId && a.showingCached != cached) {
            a.showingCached = cached;
            emit addonsChanged();
        }
    }
}

void StremioAdapter::noteResult(const QString& addonId, const QString& error)
{
    for (InstalledAddon& a : m_addons) {
        if (a.manifest.id == addonId && a.lastError != error) {
            a.lastError = error;
            emit addonsChanged();
        }
    }
}

QString StremioAdapter::wikimazeBase() const
{
    for (const InstalledAddon& a : m_addons)
        if (!a.disabled && a.manifest.id.startsWith(QLatin1String(kDefaultAddonId)))
            return stremio::baseOf(a.manifestUrl);
    return {};
}

QList<CatalogRef> StremioAdapter::catalogs() const
{
    QList<CatalogRef> out;
    for (const InstalledAddon& a : m_addons) {
        if (a.disabled)
            continue;
        for (const stremio::Catalog& c : a.manifest.catalogs) {
            if (a.hiddenCatalogs.contains(c.type + QLatin1Char('/') + c.id))
                continue;
            CatalogRef ref;
            ref.adapter = id();
            ref.provider = a.manifest.id;
            ref.id = c.id;
            ref.type = c.type;
            // "WikiMaze · Trending"-style names keep lists from
            // different add-ons apart.
            ref.name = a.manifest.name + QStringLiteral(" · ") + c.name + QLatin1Char(' ')
                     + typeWord(c.type).toLower();
            ref.searchable = c.searchable;
            ref.searchOnly = c.searchRequired;
            ref.genres = c.genres;
            ref.genreRequired = c.genreRequired;
            ref.years = c.years;
            out << ref;
        }
    }
    return out;
}

void StremioAdapter::fetchCatalog(const CatalogRef& catalog, const QString& search, int skip, CatalogDone done)
{
    const InstalledAddon* addon = find(catalog.provider);
    if (!addon || addon->disabled) {
        const QString why = addon ? QStringLiteral("the add-on is switched off") : QStringLiteral("the add-on was removed");
        QTimer::singleShot(0, this, [done, why] { done({}, why); });
        return;
    }
    QList<QPair<QString, QString>> extras;
    if (!search.trimmed().isEmpty())
        extras << qMakePair(QStringLiteral("search"), search.trimmed());
    // A catalog that needs a genre gets its first one when none was picked.
    const QString genre = !catalog.genre.isEmpty() ? catalog.genre
                        : catalog.genreRequired    ? catalog.genres.value(0)
                                                   : QString();
    if (!genre.isEmpty() && search.trimmed().isEmpty())
        extras << qMakePair(QStringLiteral("genre"), genre);
    if (!catalog.year.isEmpty() && search.trimmed().isEmpty())
        extras << qMakePair(QStringLiteral("year"), catalog.year);
    if (skip > 0)
        extras << qMakePair(QStringLiteral("skip"), QString::number(skip));
    const QString addonId = addon->manifest.id;
    const QUrl url = stremio::resourceUrl(stremio::baseOf(addon->manifestUrl), QStringLiteral("catalog"),
                                          catalog.type, catalog.id, extras);
    const QString searchText = search.trimmed();
    ask(*addon, url,
        jsp::catalogPath(catalog.type, catalog.id, searchText, genre, catalog.year, skip),
        jsp::catalogAsStremio, [this, addonId, done](const QJsonObject& json, const QString& error) {
        noteResult(addonId, error);
        if (!error.isEmpty()) {
            done({}, error);
            return;
        }
        QList<MetaPreview> items = stremio::parseMetas(json);
        for (MetaPreview& m : items) {
            m.adapter = id();
            m.provider = addonId;
        }
        done(items, {});
    });
}

void StremioAdapter::fetchMeta(const MetaPreview& item, MetaDone done)
{
    // The add-on the item came from first, then any other that has details
    // for this id (e.g. WikiMaze for "tt…" ids from a list add-on).
    QStringList order;
    if (const InstalledAddon* own = find(item.provider); own && !own->disabled && own->manifest.serves("meta", item.type, item.id))
        order << own->manifest.id;
    for (const InstalledAddon* a : serving(QStringLiteral("meta"), item.type, item.id))
        if (!order.contains(a->manifest.id))
            order << a->manifest.id;
    tryMeta(order, item, std::move(done));
}

void StremioAdapter::tryMeta(QStringList addonIds, const MetaPreview& item, MetaDone done)
{
    if (addonIds.isEmpty()) {
        // Nobody has details: a movie still plays from the preview alone.
        QTimer::singleShot(0, this, [item, done] {
            MetaDetail d;
            d.preview = item;
            done(d, {});
        });
        return;
    }
    const InstalledAddon* addon = find(addonIds.takeFirst());
    if (!addon) {
        tryMeta(addonIds, item, std::move(done));
        return;
    }
    const QString addonId = addon->manifest.id;
    const QUrl url = stremio::resourceUrl(stremio::baseOf(addon->manifestUrl), QStringLiteral("meta"), item.type, item.id);
    ask(*addon, url, jsp::metaPath(item.type, item.id), jsp::metaAsStremio,
        [this, addonId, addonIds, item, done](const QJsonObject& json, const QString& error) {
        noteResult(addonId, error);
        std::optional<MetaDetail> meta = error.isEmpty() ? stremio::parseMeta(json) : std::nullopt;
        if (!meta) {
            if (addonIds.isEmpty() && !error.isEmpty()) {
                done(std::nullopt, error);
                return;
            }
            tryMeta(addonIds, item, done);
            return;
        }
        meta->preview.adapter = item.adapter;
        meta->preview.provider = item.provider;
        if (meta->preview.type.isEmpty())
            meta->preview.type = item.type;
        if (meta->preview.name.isEmpty())
            meta->preview.name = item.name;
        if (meta->preview.poster.isEmpty())
            meta->preview.poster = item.poster;
        done(meta, {});
    });
}

void StremioAdapter::fetchStreams(const QString& type, const QString& videoId, StreamsPart part,
                                  std::function<void()> finished)
{
    const QList<const InstalledAddon*> addons = serving(QStringLiteral("stream"), type, videoId);
    if (addons.isEmpty()) {
        QTimer::singleShot(0, this, finished);
        return;
    }
    auto left = std::make_shared<int>(int(addons.size()));
    for (const InstalledAddon* addon : addons) {
        const QString addonId = addon->manifest.id;
        const QString addonName = addon->manifest.name;
        const QUrl url = stremio::resourceUrl(stremio::baseOf(addon->manifestUrl), QStringLiteral("stream"), type, videoId);
        ask(*addon, url, jsp::streamsPath(type, videoId), jsp::streamsAsStremio,
            [this, addonId, addonName, part, finished, left](const QJsonObject& json, const QString& error) {
            noteResult(addonId, error);
            part(error.isEmpty() ? stremio::parseStreams(json, addonId, addonName) : QList<StreamOption>{},
                 addonName, error);
            if (--*left == 0)
                finished();
        });
    }
}

void StremioAdapter::fetchSubtitles(const QString& type, const QString& videoId, SubtitlesDone done)
{
    const QList<const InstalledAddon*> addons = serving(QStringLiteral("subtitles"), type, videoId);
    if (addons.isEmpty()) {
        QTimer::singleShot(0, this, [done] { done({}); });
        return;
    }
    auto left = std::make_shared<int>(int(addons.size()));
    auto all = std::make_shared<QList<SubtitleSource>>();
    for (const InstalledAddon* addon : addons) {
        const QString addonId = addon->manifest.id;
        const QString addonName = addon->manifest.name;
        const QUrl url = stremio::resourceUrl(stremio::baseOf(addon->manifestUrl), QStringLiteral("subtitles"), type, videoId);
        ask(*addon, url, jsp::subtitlesPath(type, videoId), jsp::subtitlesAsStremio,
            [this, addonId, addonName, done, left, all](const QJsonObject& json, const QString& error) {
            noteResult(addonId, error);
            for (SubtitleSource s : stremio::parseSubtitles(json)) {
                if (s.label.isEmpty())
                    s.label = addonName + (s.language.isEmpty() ? QString() : QStringLiteral(" · ") + s.language);
                *all << s;
            }
            if (--*left == 0)
                done(*all);
        });
    }
}

void StremioAdapter::install(const QString& link, std::function<void(const QString& error)> done)
{
    const QUrl url = stremio::normalizeManifestUrl(link);
    if (!url.isValid()) {
        QTimer::singleShot(0, this, [done] {
            done(QStringLiteral("That is not an add-on link. It should end in /manifest.json."));
        });
        return;
    }
    const bool known = std::any_of(m_addons.begin(), m_addons.end(), [&](const InstalledAddon& a) {
        return a.manifestUrl == url;
    });
    if (!known && m_addons.size() >= kMaxAddons) {
        QTimer::singleShot(0, this, [done] { done(QStringLiteral("Too many add-ons installed (50). Remove one first.")); });
        return;
    }
    m_fetcher.get(url, [this, url, done](const QJsonObject& json, const QString& error) {
        if (!error.isEmpty()) {
            done(QStringLiteral("The add-on could not be reached (%1).").arg(error));
            return;
        }
        QString parseError;
        const std::optional<stremio::Manifest> m = jsp::parseAnyManifest(json, &parseError);
        if (!m) {
            done(QStringLiteral("That link did not return an add-on (%1).").arg(parseError));
            return;
        }
        if (const QString addressProblem = jspAddressProblem(url, json); !addressProblem.isEmpty()) {
            done(addressProblem);
            return;
        }
        if (m->configurationRequired) {
            done(QStringLiteral("%1 has to be set up on its website first. Install the link it gives you after that.")
                     .arg(m->name));
            return;
        }
        setManifest(url, *m, json);
        save();
        qCInfo(lcSources).noquote() << "Installed add-on" << m->id << m->version << "from" << logging::redactUrl(url);
        emit addonsChanged();
        emit catalogsChanged();
        done({});
    });
}

bool StremioAdapter::uninstall(const QString& addonId)
{
    const auto it = std::find_if(m_addons.begin(), m_addons.end(),
                                 [&](const InstalledAddon& a) { return a.manifest.id == addonId; });
    if (it == m_addons.end())
        return false;
    m_addons.erase(it);
    m_jsp.forget(addonId);
    save();
    qCInfo(lcSources).noquote() << "Removed add-on" << addonId;
    emit addonsChanged();
    emit catalogsChanged();
    return true;
}

bool StremioAdapter::move(const QString& addonId, int delta)
{
    const auto it = std::find_if(m_addons.begin(), m_addons.end(),
                                 [&](const InstalledAddon& a) { return a.manifest.id == addonId; });
    if (it == m_addons.end())
        return false;
    const int from = int(it - m_addons.begin());
    const int to = qBound(0, from + delta, int(m_addons.size()) - 1);
    if (to == from)
        return false;
    m_addons.move(from, to);
    save();
    emit addonsChanged();
    emit catalogsChanged();
    return true;
}

bool StremioAdapter::setCatalogEnabled(const QString& addonId, const QString& type, const QString& catalogId, bool enabled)
{
    const QString key = type + QLatin1Char('/') + catalogId;
    for (InstalledAddon& a : m_addons) {
        if (a.manifest.id != addonId || a.hiddenCatalogs.contains(key) == !enabled)
            continue;
        if (enabled)
            a.hiddenCatalogs.removeAll(key);
        else
            a.hiddenCatalogs << key;
        save();
        emit addonsChanged();
        emit catalogsChanged();
        return true;
    }
    return false;
}

bool StremioAdapter::setEnabled(const QString& addonId, bool enabled)
{
    for (InstalledAddon& a : m_addons) {
        if (a.manifest.id != addonId || a.disabled == !enabled)
            continue;
        a.disabled = !enabled;
        qCInfo(lcSources).noquote() << "Add-on" << addonId << (enabled ? "switched on" : "switched off");
        save();
        emit addonsChanged();
        emit catalogsChanged();
        return true;
    }
    return false;
}

void StremioAdapter::refreshManifests()
{
    for (const InstalledAddon& a : std::as_const(m_addons)) {
        if (a.disabled)
            continue;   // switched off: not contacted
        const QUrl url = a.manifestUrl;
        const QString addonId = a.manifest.id;
        m_fetcher.get(url, [this, url, addonId](const QJsonObject& json, const QString& error) {
            if (!find(addonId))
                return;  // removed meanwhile
            QString parseError;
            const auto m = error.isEmpty() ? jsp::parseAnyManifest(json, &parseError) : std::nullopt;
            if (!m || m->id != addonId) {
                noteResult(addonId, !error.isEmpty() ? error : m ? QStringLiteral("its manifest changed id") : parseError);
                return;
            }
            if (const QString addressProblem = jspAddressProblem(url, json); !addressProblem.isEmpty()) {
                noteResult(addonId, addressProblem);
                return;
            }
            setManifest(url, *m, json);
            save();
            emit addonsChanged();
            emit catalogsChanged();
        });
    }
}

} // namespace cj
