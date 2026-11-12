#include "Preferences.h"

#include "Logging.h"
#include "Storage.h"

#include <QDir>

namespace cj {

namespace {
const QString kInMemory = QStringLiteral("torrents/inMemory");
const QString kMemoryCap = QStringLiteral("torrents/memoryCapMB");
const QString kDiskCap = QStringLiteral("torrents/diskCacheGB");
const QString kFolder = QStringLiteral("torrents/cacheFolder");
const QString kPrivacyChosen = QStringLiteral("privacy/chosen");
const QString kCinemeta = QStringLiteral("privacy/cinemeta");
const QString kDht = QStringLiteral("privacy/dht");
const QString kTrackers = QStringLiteral("privacy/publicTrackers");
const QString kPortMapping = QStringLiteral("privacy/portMapping");
} // namespace

Preferences::Preferences(Storage& storage, const QString& defaultCacheFolder, QObject* parent)
    : QObject(parent)
    , m_storage(storage)
    , m_defaultFolder(defaultCacheFolder)
{
    const auto value = [&](const QString& key, const QVariant& fallback) { return storage.setting(key, fallback); };
    m_inMemory = value(kInMemory, true).toBool();
    m_memoryCapMB = qBound(256, value(kMemoryCap, 1024).toInt(), 16384);
    m_diskCacheGB = qBound(1, value(kDiskCap, 10).toInt(), 1000);
    m_cacheFolder = value(kFolder, QString()).toString();
    if (!m_cacheFolder.isEmpty() && !QDir().mkpath(m_cacheFolder)) {
        qCWarning(lcApp).noquote() << "Torrent cache folder" << m_cacheFolder << "is not usable; using the default";
        m_cacheFolder.clear();
    }
    m_privacyChosen = value(kPrivacyChosen, false).toBool();
    // Off unless the person said yes: a missing answer is a no.
    m_cinemeta = m_privacyChosen && value(kCinemeta, false).toBool();
    m_dht = m_privacyChosen && value(kDht, false).toBool();
    m_publicTrackers = m_privacyChosen && value(kTrackers, false).toBool();
    m_portMapping = m_privacyChosen && value(kPortMapping, false).toBool();
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

void Preferences::setUseCinemeta(bool on)
{
    if (on == m_cinemeta)
        return;
    m_cinemeta = on;
    savePrivacy(!on);
}

void Preferences::setUseDht(bool on)
{
    if (on == m_dht)
        return;
    m_dht = on;
    savePrivacy(m_cinemeta);
}

void Preferences::setUsePublicTrackers(bool on)
{
    if (on == m_publicTrackers)
        return;
    m_publicTrackers = on;
    savePrivacy(m_cinemeta);
}

void Preferences::setUsePortMapping(bool on)
{
    if (on == m_portMapping)
        return;
    m_portMapping = on;
    savePrivacy(m_cinemeta);
}

void Preferences::choosePrivacy(bool cinemeta, bool dht, bool publicTrackers, bool portMapping)
{
    const bool cinemetaBefore = m_cinemeta;
    m_cinemeta = cinemeta;
    m_dht = dht;
    m_publicTrackers = publicTrackers;
    m_portMapping = portMapping;
    m_privacyChosen = true;
    savePrivacy(cinemetaBefore);
}

// Saves the four answers and tells the app, which acts on them.
void Preferences::savePrivacy(bool cinemetaBefore)
{
    // Choosing from Settings is choosing: the first-run screen is not needed after.
    m_privacyChosen = true;
    m_storage.setSetting(kPrivacyChosen, true);
    m_storage.setSetting(kCinemeta, m_cinemeta);
    m_storage.setSetting(kDht, m_dht);
    m_storage.setSetting(kTrackers, m_publicTrackers);
    m_storage.setSetting(kPortMapping, m_portMapping);
    qCInfo(lcApp).noquote() << "Network choices: Cinemeta" << m_cinemeta << "DHT and LAN discovery" << m_dht
                            << "open trackers" << m_publicTrackers << "port mapping" << m_portMapping;
    emit privacyChanged();
    if (cinemetaBefore != m_cinemeta)
        emit cinemetaChoiceChanged(m_cinemeta);
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

} // namespace cj
