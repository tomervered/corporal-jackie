#include "Preferences.h"

#include "Logging.h"
#include "Storage.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QUrl>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>

namespace cj {

namespace {
const QString kInMemory = QStringLiteral("torrents/inMemory");
// Bumped when a storage default changes, so installs that saved the old one
// move to the new one once. 2: disk instead of RAM only (2026-11-14).
const QString kStorageDefaults = QStringLiteral("torrents/defaultsVersion");
constexpr int kStorageDefaultsVersion = 2;
const QString kMemoryCap = QStringLiteral("torrents/memoryCapMB");
const QString kDiskCap = QStringLiteral("torrents/diskCacheGB");
const QString kFolder = QStringLiteral("torrents/cacheFolder");
const QString kPrivacyChosen = QStringLiteral("privacy/chosen");
// The default add-ons, WikiMaze and Public Domain Movies; "privacy/cinemeta"
// was Stremio's server.
const QString kCatalogAddon = QStringLiteral("privacy/catalogAddon");
const QString kDht = QStringLiteral("privacy/dht");
const QString kTrackers = QStringLiteral("privacy/publicTrackers");
const QString kPortMapping = QStringLiteral("privacy/portMapping");
const QString kTorrents = QStringLiteral("privacy/torrents");
const QString kUploads = QStringLiteral("privacy/uploads");
// Bumped when the first-run screen gains a question, so it asks once more.
// 2: playing torrents and uploading (2026-11-15).
const QString kPrivacyVersion = QStringLiteral("privacy/version");
constexpr int kPrivacyScreenVersion = 2;
const QString kColorMode = QStringLiteral("ui/colorMode");
const QString kUpscalerBadge = QStringLiteral("ui/showUpscalerBadge");
const QString kScreenshots = QStringLiteral("ui/screenshots");
const QString kPlayerHalo = QStringLiteral("ui/playerHalo");
const QStringList kHaloLevels = {QStringLiteral("full"), QStringLiteral("half"), QStringLiteral("little"), QStringLiteral("off")};
const QString kSavedPalettes = QStringLiteral("ui/savedPalettes");   // JSON text
constexpr int kMaxPalettes = 50;
constexpr int kMaxPaletteName = 40;
const QString kColorEdits = QStringLiteral("ui/colorEdits");   // JSON text

bool knownMode(const QString& mode)
{
    return mode == QLatin1String("dark") || mode == QLatin1String("oled") || mode == QLatin1String("bright")
        || mode == QLatin1String("custom");
}

// A colour file: {"corporalJackieColors": 1, "accent": "#rrggbb", ...}.
const QLatin1String kColorFileMark("corporalJackieColors");
constexpr qint64 kColorFileMaxBytes = 16 * 1024;

// Only #rrggbb values for the four colours of a known mode are kept.
QVariantMap cleanEdits(const QVariantMap& edits)
{
    static const QRegularExpression hex(QStringLiteral("^#[0-9a-fA-F]{6}$"));
    QVariantMap out;
    for (auto mode = edits.begin(); mode != edits.end(); ++mode) {
        if (!knownMode(mode.key()))
            continue;
        QVariantMap colors;
        const QVariantMap given = mode.value().toMap();
        for (const char* key : {"accent", "canvas", "panel", "text"}) {
            const QString value = given.value(QLatin1String(key)).toString();
            if (hex.match(value).hasMatch())
                colors.insert(QLatin1String(key), value.toLower());
        }
        if (!colors.isEmpty())
            out.insert(mode.key(), colors);
    }
    return out;
}
// Four valid colours, or an empty map.
QVariantMap cleanColors(const QVariantMap& colors)
{
    const QVariantMap clean = cleanEdits({{QStringLiteral("custom"), colors}}).value(QStringLiteral("custom")).toMap();
    return clean.size() == 4 ? clean : QVariantMap();
}
QString cleanName(const QString& name)
{
    QString out = name.simplified();
    out.remove(QRegularExpression(QStringLiteral("[\\x00-\\x1f]")));
    return out.left(kMaxPaletteName);
}

} // namespace

Preferences::Preferences(Storage& storage, const QString& defaultCacheFolder, QObject* parent)
    : QObject(parent)
    , m_storage(storage)
    , m_defaultFolder(defaultCacheFolder)
{
    const auto value = [&](const QString& key, const QVariant& fallback) { return storage.setting(key, fallback); };
    m_inMemory = value(kInMemory, false).toBool();
    if (value(kStorageDefaults, 1).toInt() < kStorageDefaultsVersion) {
        // RAM only was the default and held up to its whole cap (1 GB) on top
        // of mpv's own buffer; disk mode keeps the app's memory flat.
        m_inMemory = false;
        storage.setSetting(kInMemory, false);
        storage.setSetting(kStorageDefaults, kStorageDefaultsVersion);
    }
    m_memoryCapMB = qBound(256, value(kMemoryCap, 1024).toInt(), 16384);
    m_diskCacheGB = qBound(1, value(kDiskCap, 10).toInt(), 1000);
    m_cacheFolder = value(kFolder, QString()).toString();
    if (!m_cacheFolder.isEmpty() && !QDir().mkpath(m_cacheFolder)) {
        qCWarning(lcApp).noquote() << "Torrent cache folder" << m_cacheFolder << "is not usable; using the default";
        m_cacheFolder.clear();
    }
    // The default add-ons come installed: their switch starts on until the
    // person answers. The other switches start off.
    m_earlier = {{QStringLiteral("catalogAddon"), value(kCatalogAddon, true).toBool()},
                 {QStringLiteral("dht"), value(kDht, false).toBool()},
                 {QStringLiteral("trackers"), value(kTrackers, false).toBool()},
                 {QStringLiteral("portMapping"), value(kPortMapping, false).toBool()}};
    m_privacyChosen = value(kPrivacyChosen, false).toBool()
        && value(kPrivacyVersion, 1).toInt() >= kPrivacyScreenVersion;
    // Off unless the person said yes: a missing answer is a no.
    m_catalogAddon = m_privacyChosen && value(kCatalogAddon, false).toBool();
    m_dht = m_privacyChosen && value(kDht, false).toBool();
    m_publicTrackers = m_privacyChosen && value(kTrackers, false).toBool();
    m_portMapping = m_privacyChosen && value(kPortMapping, false).toBool();
    m_torrents = m_privacyChosen && value(kTorrents, false).toBool();
    m_uploads = m_privacyChosen && value(kUploads, false).toBool();
    const QString mode = value(kColorMode, QStringLiteral("dark")).toString();
    m_colorMode = knownMode(mode) ? mode : QStringLiteral("dark");
    m_upscalerBadge = value(kUpscalerBadge, true).toBool();
    m_screenshots = value(kScreenshots, true).toBool();
    {
        const QString halo = value(kPlayerHalo, QStringLiteral("full")).toString();
        m_playerHalo = kHaloLevels.contains(halo) ? halo : QStringLiteral("full");
    }
    for (const QJsonValue& v : QJsonDocument::fromJson(value(kSavedPalettes, QString()).toString().toUtf8()).array()) {
        const QVariantMap given = v.toObject().toVariantMap();
        QVariantMap colors = cleanColors(given);
        const QString name = cleanName(given.value(QStringLiteral("name")).toString());
        if (colors.isEmpty() || name.isEmpty() || m_savedPalettes.size() >= kMaxPalettes)
            continue;
        colors.insert(QStringLiteral("name"), name);
        m_savedPalettes.append(colors);
    }
    m_colorEdits = cleanEdits(QJsonDocument::fromJson(value(kColorEdits, QString()).toString().toUtf8()).object().toVariantMap());
    m_started = {m_inMemory, m_memoryCapMB, m_diskCacheGB, effectiveFolder()};
}

QString Preferences::effectiveFolder() const { return m_cacheFolder.isEmpty() ? m_defaultFolder : m_cacheFolder; }

QString Preferences::cacheFolder() const { return QDir::toNativeSeparators(effectiveFolder()); }

bool Preferences::restartNeeded() const
{
    return m_inMemory != m_started.inMemory || m_memoryCapMB != m_started.memoryCapMB
        || m_diskCacheGB != m_started.diskCacheGB || effectiveFolder() != m_started.folder;
}

void Preferences::setTorrentsInMemory(bool on)
{
    if (on == m_inMemory)
        return;
    m_inMemory = on;
    save();
}

void Preferences::setMemoryCapMB(int mb)
{
    mb = qBound(256, mb, 16384);
    if (mb == m_memoryCapMB)
        return;
    m_memoryCapMB = mb;
    save();
}

void Preferences::setDiskCacheGB(int gb)
{
    gb = qBound(1, gb, 1000);
    if (gb == m_diskCacheGB)
        return;
    m_diskCacheGB = gb;
    save();
}

void Preferences::setUseCatalogAddon(bool on)
{
    if (on == m_catalogAddon)
        return;
    m_catalogAddon = on;
    savePrivacy(!on);
}

void Preferences::setUseDht(bool on)
{
    if (on == m_dht)
        return;
    m_dht = on;
    savePrivacy(m_catalogAddon);
}

void Preferences::setUsePublicTrackers(bool on)
{
    if (on == m_publicTrackers)
        return;
    m_publicTrackers = on;
    savePrivacy(m_catalogAddon);
}

void Preferences::setUsePortMapping(bool on)
{
    if (on == m_portMapping)
        return;
    m_portMapping = on;
    savePrivacy(m_catalogAddon);
}

void Preferences::setUseTorrents(bool on)
{
    if (on == m_torrents)
        return;
    m_torrents = on;
    savePrivacy(m_catalogAddon);
}

void Preferences::setAllowUploads(bool on)
{
    if (on == m_uploads)
        return;
    m_uploads = on;
    savePrivacy(m_catalogAddon);
}

void Preferences::choosePrivacy(bool catalogAddon, bool dht, bool publicTrackers, bool portMapping, bool torrents,
                                bool uploads)
{
    // Before the first answer the built-in add-ons follow the screen's
    // starting switch (AppCore installs them), so that is what changes.
    const bool catalogBefore = m_privacyChosen ? m_catalogAddon : m_earlier.value(QStringLiteral("catalogAddon")).toBool();
    m_catalogAddon = catalogAddon;
    m_dht = dht;
    m_publicTrackers = publicTrackers;
    m_portMapping = portMapping;
    m_torrents = torrents;
    m_uploads = uploads;
    m_privacyChosen = true;
    savePrivacy(catalogBefore);
}

// Saves the four answers and tells the app, which acts on them.
void Preferences::savePrivacy(bool catalogBefore)
{
    // Choosing from Settings is choosing: the first-run screen is not needed after.
    m_privacyChosen = true;
    m_storage.setSetting(kPrivacyChosen, true);
    m_storage.setSetting(kPrivacyVersion, kPrivacyScreenVersion);
    m_storage.setSetting(kTorrents, m_torrents);
    m_storage.setSetting(kUploads, m_uploads);
    m_storage.setSetting(kCatalogAddon, m_catalogAddon);
    m_storage.setSetting(kDht, m_dht);
    m_storage.setSetting(kTrackers, m_publicTrackers);
    m_storage.setSetting(kPortMapping, m_portMapping);
    qCInfo(lcApp).noquote() << "Network choices: catalog add-on" << m_catalogAddon << "DHT and LAN discovery" << m_dht
                            << "open trackers" << m_publicTrackers << "port mapping" << m_portMapping
                            << "torrents" << m_torrents << "uploads" << m_uploads;
    emit privacyChanged();
    if (catalogBefore != m_catalogAddon)
        emit catalogAddonChoiceChanged(m_catalogAddon);
}

void Preferences::setCacheFolder(const QUrl& folder)
{
    const QString path = folder.isLocalFile() ? folder.toLocalFile() : folder.toString();
    if (path.isEmpty())
        return;
    // A folder of our own inside the chosen one, so clearing the cache never
    // touches the user's other files.
    const QString ours = QDir(path).filePath(QStringLiteral("Corporal Jackie torrents"));
    if (!QDir().mkpath(ours)) {
        qCWarning(lcApp).noquote() << "Cannot use" << ours << "for the torrent cache";
        return;
    }
    m_cacheFolder = QDir::cleanPath(ours);
    save();
}

void Preferences::resetCacheFolder()
{
    if (m_cacheFolder.isEmpty())
        return;
    m_cacheFolder.clear();
    save();
}

void Preferences::save()
{
    m_storage.setSetting(kInMemory, m_inMemory);
    m_storage.setSetting(kMemoryCap, m_memoryCapMB);
    m_storage.setSetting(kDiskCap, m_diskCacheGB);
    m_storage.setSetting(kFolder, m_cacheFolder);
    qCInfo(lcApp).noquote() << "Torrent storage:" << (m_inMemory ? "RAM only" : "disk") << m_memoryCapMB << "MB RAM,"
                            << m_diskCacheGB << "GB disk, folder" << effectiveFolder()
                            << (restartNeeded() ? "(applies after a restart)" : "");
    emit changed();
}

void Preferences::setColorMode(const QString& mode)
{
    setTheme(mode, m_colorEdits);
}

void Preferences::setScreenshots(bool on)
{
    if (on == m_screenshots)
        return;
    m_screenshots = on;
    m_storage.setSetting(kScreenshots, on);
    emit screenshotsChanged();
}

void Preferences::setShowUpscalerBadge(bool on)
{
    if (on == m_upscalerBadge)
        return;
    m_upscalerBadge = on;
    m_storage.setSetting(kUpscalerBadge, on);
    emit upscalerBadgeChanged();
    emit appearanceChanged();
}

void Preferences::setColorEdits(const QVariantMap& edits)
{
    setTheme(m_colorMode, edits);
}

void Preferences::setTheme(const QString& mode, const QVariantMap& edits)
{
    if (!knownMode(mode))
        return;
    const QVariantMap clean = cleanEdits(edits);
    if (mode == m_colorMode && clean == m_colorEdits)
        return;
    if (clean != m_colorEdits) {
        m_colorEdits = clean;
        m_storage.setSetting(kColorEdits, QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(clean)).toJson(QJsonDocument::Compact)));
    }
    if (mode != m_colorMode) {
        m_colorMode = mode;
        m_storage.setSetting(kColorMode, mode);
    }
    emit themeChanged();
    emit appearanceChanged();
}

void Preferences::setPlayerHalo(const QString& level)
{
    if (!kHaloLevels.contains(level) || level == m_playerHalo)
        return;
    m_playerHalo = level;
    m_storage.setSetting(kPlayerHalo, level);
    emit playerHaloChanged();
    emit appearanceChanged();
}

void Preferences::storePalettes()
{
    m_storage.setSetting(kSavedPalettes, QString::fromUtf8(QJsonDocument(QJsonArray::fromVariantList(m_savedPalettes)).toJson(QJsonDocument::Compact)));
    emit savedPalettesChanged();
    emit appearanceChanged();
}

QString Preferences::savePalette(const QString& name, const QVariantMap& colors)
{
    const QString clean = cleanName(name);
    if (clean.isEmpty())
        return QStringLiteral("Give the palette a name.");
    QVariantMap palette = cleanColors(colors);
    if (palette.isEmpty())
        return QStringLiteral("These colors could not be saved.");
    palette.insert(QStringLiteral("name"), clean);
    for (int i = 0; i < m_savedPalettes.size(); ++i) {
        if (m_savedPalettes[i].toMap().value(QStringLiteral("name")).toString().compare(clean, Qt::CaseInsensitive) == 0) {
            m_savedPalettes[i] = palette;
            storePalettes();
            return {};
        }
    }
    if (m_savedPalettes.size() >= kMaxPalettes)
        return QStringLiteral("There are already %1 saved palettes. Remove one first.").arg(kMaxPalettes);
    m_savedPalettes.append(palette);
    storePalettes();
    return {};
}

void Preferences::removePalette(const QString& name)
{
    for (int i = 0; i < m_savedPalettes.size(); ++i) {
        if (m_savedPalettes[i].toMap().value(QStringLiteral("name")).toString() == name) {
            m_savedPalettes.removeAt(i);
            storePalettes();
            return;
        }
    }
}

QString Preferences::exportColors(const QUrl& file, const QString& name, const QVariantMap& colors) const
{
    const QVariantMap clean = cleanColors(colors);
    if (clean.isEmpty())
        return QStringLiteral("These colors could not be saved.");
    if (!file.isLocalFile())
        return QStringLiteral("Pick a file on this computer.");
    QJsonObject out = QJsonObject::fromVariantMap(clean);
    out.insert(kColorFileMark, 1);
    const QString cleanedName = cleanName(name);
    if (!cleanedName.isEmpty())
        out.insert(QStringLiteral("name"), cleanedName);
    QSaveFile f(file.toLocalFile());
    if (!f.open(QIODevice::WriteOnly) || f.write(QJsonDocument(out).toJson()) < 0 || !f.commit())
        return QStringLiteral("Could not write ") + file.fileName() + QStringLiteral(".");
    return {};
}

QString Preferences::importColors(const QUrl& file)
{
    if (!file.isLocalFile())
        return QStringLiteral("Pick a file on this computer.");
    QFile f(file.toLocalFile());
    if (!f.open(QIODevice::ReadOnly))
        return QStringLiteral("Could not open ") + file.fileName() + QStringLiteral(".");
    if (f.size() > kColorFileMaxBytes)
        return QStringLiteral("That is not a colors file.");
    const QJsonObject in = QJsonDocument::fromJson(f.read(kColorFileMaxBytes)).object();
    if (!in.contains(kColorFileMark))
        return QStringLiteral("That is not a colors file.");
    const QVariantMap clean = cleanColors(in.toVariantMap());
    if (clean.isEmpty())
        return QStringLiteral("That colors file is missing some colors.");
    QString name = cleanName(in.value(QStringLiteral("name")).toString());
    if (name.isEmpty())
        name = cleanName(QFileInfo(file.toLocalFile()).completeBaseName());
    if (!name.isEmpty())
        savePalette(name, clean);   // a full list just skips this
    QVariantMap edits = m_colorEdits;
    edits.insert(QStringLiteral("custom"), clean);
    setTheme(QStringLiteral("custom"), edits);
    return {};
}

} // namespace cj
