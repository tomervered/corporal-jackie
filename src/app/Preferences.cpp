#include "Preferences.h"

#include "Logging.h"
#include "Storage.h"

#include <QDir>
#include <QJsonDocument>
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
const QString kCatalogAddon = QStringLiteral("privacy/catalogAddon");  // WikiMaze; "privacy/cinemeta" was Stremio's server
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
const QString kColorEdits = QStringLiteral("ui/colorEdits");   // JSON text

bool knownMode(const QString& mode)
{
    return mode == QLatin1String("dark") || mode == QLatin1String("oled") || mode == QLatin1String("bright");
}

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
    m_earlier = {{QStringLiteral("catalogAddon"), value(kCatalogAddon, false).toBool()},
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
    const bool catalogBefore = m_catalogAddon;
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
    if (!knownMode(mode) || mode == m_colorMode)
        return;
    m_colorMode = mode;
    m_storage.setSetting(kColorMode, mode);
    emit appearanceChanged();
}

void Preferences::setColorEdits(const QVariantMap& edits)
{
    const QVariantMap clean = cleanEdits(edits);
    if (clean == m_colorEdits)
        return;
    m_colorEdits = clean;
    m_storage.setSetting(kColorEdits, QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(clean)).toJson(QJsonDocument::Compact)));
    emit appearanceChanged();
}

} // namespace cj
