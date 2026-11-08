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
