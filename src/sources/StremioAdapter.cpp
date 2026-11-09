#include "StremioAdapter.h"

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

// Used only until the first refresh fetches the real manifest: enough for
// Cinemeta's popular lists and search to work on a first run.
const char kCinemetaManifest[] = R"({
  "id": "com.linvo.cinemeta",
  "version": "3.0.0",
  "name": "Cinemeta",
  "description": "Movie and series catalogs and details",
  "resources": ["catalog", "meta"],
  "types": ["movie", "series"],
  "idPrefixes": ["tt"],
  "catalogs": [
    {"type": "movie", "id": "top", "name": "Popular", "extra": [{"name": "search"}, {"name": "skip"}]},
    {"type": "series", "id": "top", "name": "Popular", "extra": [{"name": "search"}, {"name": "skip"}]},
    {"type": "movie", "id": "imdbRating", "name": "Featured", "extra": [{"name": "skip"}]},
    {"type": "series", "id": "imdbRating", "name": "Featured", "extra": [{"name": "skip"}]}
  ]
})";

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

void StremioAdapter::installDefaults()
{
    QString error;
    const QJsonObject raw = QJsonDocument::fromJson(kCinemetaManifest).object();
    if (auto m = stremio::parseManifest(raw, &error))
        setManifest(QUrl(QString::fromLatin1(kCinemetaUrl)), *m, raw);
    save();
    emit addonsChanged();
    emit catalogsChanged();
}

QJsonArray StremioAdapter::storedList() const
{
    QJsonArray list;
    for (const InstalledAddon& a : m_addons)
        list.append(QJsonObject{{"transportUrl", a.manifestUrl.toString()}, {"manifest", a.rawManifest}});
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
        if (auto m = stremio::parseManifest(raw, &error); m && url.isValid() && m_addons.size() < kMaxAddons)
            setManifest(url, *m, raw);
        else
            qCWarning(lcSources).noquote() << "Skipping a stored add-on:" << logging::redactUrl(url) << error;
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
    for (InstalledAddon& a : m_addons) {
        if (a.manifest.id == manifest.id || a.manifestUrl == url) {
            a.manifestUrl = url;
            a.manifest = manifest;
            a.rawManifest = raw;
            a.lastError.clear();
            return;
        }
    }
    m_addons.append({url, manifest, raw, {}});
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
        if (a.manifest.serves(resource, type, id))
            out << &a;
    return out;
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

QList<CatalogRef> StremioAdapter::catalogs() const
{
    QList<CatalogRef> out;
    for (const InstalledAddon& a : m_addons) {
        for (const stremio::Catalog& c : a.manifest.catalogs) {
            CatalogRef ref;
            ref.adapter = id();
            ref.provider = a.manifest.id;
            ref.id = c.id;
            ref.type = c.type;
            // "Cinemeta · Popular movies"-style names keep lists from
            // different add-ons apart.
            ref.name = a.manifest.name + QStringLiteral(" · ") + c.name + QLatin1Char(' ')
                     + typeWord(c.type).toLower();
            ref.searchable = c.searchable;
            ref.searchOnly = c.searchRequired;
            ref.genres = c.genres;
            ref.genreRequired = c.genreRequired;
            out << ref;
        }
    }
    return out;
}

void StremioAdapter::fetchCatalog(const CatalogRef& catalog, const QString& search, int skip, CatalogDone done)
{
    const InstalledAddon* addon = find(catalog.provider);
    if (!addon) {
        QTimer::singleShot(0, this, [done] { done({}, QStringLiteral("the add-on was removed")); });
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
    if (skip > 0)
        extras << qMakePair(QStringLiteral("skip"), QString::number(skip));
    const QString addonId = addon->manifest.id;
    const QUrl url = stremio::resourceUrl(stremio::baseOf(addon->manifestUrl), QStringLiteral("catalog"),
                                          catalog.type, catalog.id, extras);
    m_fetcher.get(url, [this, addonId, done](const QJsonObject& json, const QString& error) {
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
    // for this id (e.g. Cinemeta for "tt…" ids from a list add-on).
    QStringList order;
    if (const InstalledAddon* own = find(item.provider); own && own->manifest.serves("meta", item.type, item.id))
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
    m_fetcher.get(url, [this, addonId, addonIds, item, done](const QJsonObject& json, const QString& error) {
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
        m_fetcher.get(url, [this, addonId, addonName, part, finished, left](const QJsonObject& json, const QString& error) {
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
        m_fetcher.get(url, [this, addonId, addonName, done, left, all](const QJsonObject& json, const QString& error) {
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
        const std::optional<stremio::Manifest> m = stremio::parseManifest(json, &parseError);
        if (!m) {
            done(QStringLiteral("That link did not return an add-on (%1).").arg(parseError));
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

void StremioAdapter::refreshManifests()
{
    for (const InstalledAddon& a : std::as_const(m_addons)) {
        const QUrl url = a.manifestUrl;
        const QString addonId = a.manifest.id;
        m_fetcher.get(url, [this, url, addonId](const QJsonObject& json, const QString& error) {
            if (!find(addonId))
                return;  // removed meanwhile
            QString parseError;
            const auto m = error.isEmpty() ? stremio::parseManifest(json, &parseError) : std::nullopt;
            if (!m || m->id != addonId) {
                noteResult(addonId, !error.isEmpty() ? error : m ? QStringLiteral("its manifest changed id") : parseError);
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
