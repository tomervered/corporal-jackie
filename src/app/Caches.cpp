#include "Caches.h"

#include "Logging.h"
#include "Storage.h"
#include "StreamBroker.h"
#include "TorrentTransport.h"

#include <QDir>
#include <QDirIterator>
#include <QNetworkAccessManager>
#include <QNetworkDiskCache>
#include <QTimer>

#include <thread>

namespace cj {

namespace {
const QString kImageCacheKey = QStringLiteral("cache/imagesMB");

qint64 folderSize(const QString& path)
{
    qint64 total = 0;
    QDirIterator it(path, QDir::Files | QDir::Hidden | QDir::NoSymLinks, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        total += it.fileInfo().size();
    }
    return total;
}
} // namespace

// --- ImageCacheFactory ------------------------------------------------------------

ImageCacheFactory::ImageCacheFactory(const QString& folder, qint64 maxBytes)
    : m_folder(folder)
    , m_maxBytes(maxBytes)
{
}

QNetworkAccessManager* ImageCacheFactory::create(QObject* parent)
{
    auto* manager = new QNetworkAccessManager(parent);
    auto* cache = new QNetworkDiskCache(manager);
    cache->setCacheDirectory(m_folder);
    std::lock_guard lock(m_mutex);
    cache->setMaximumCacheSize(m_maxBytes);
    manager->setCache(cache);
    m_caches.erase(std::remove_if(m_caches.begin(), m_caches.end(), [](const auto& c) { return c.isNull(); }),
                   m_caches.end());
    m_caches.emplace_back(cache);
    return manager;
}

void ImageCacheFactory::setMaximumSize(qint64 bytes)
{
    std::lock_guard lock(m_mutex);
    m_maxBytes = bytes;
    // Each cache lives on its manager's thread: change it there.
    for (const QPointer<QNetworkDiskCache>& cache : m_caches)
        if (cache)
            QMetaObject::invokeMethod(cache.data(), [cache, bytes] {
                if (cache)
                    cache->setMaximumCacheSize(bytes);
            }, Qt::QueuedConnection);
}

void ImageCacheFactory::clear()
{
    std::vector<QPointer<QNetworkDiskCache>> caches;
    {
        std::lock_guard lock(m_mutex);
        caches = m_caches;
    }
    bool any = false;
    for (const QPointer<QNetworkDiskCache>& cache : caches) {
        if (!cache)
            continue;
        any = true;
        QMetaObject::invokeMethod(cache.data(), [cache] {
            if (cache)
                cache->clear();
        }, Qt::QueuedConnection);
    }
    if (!any)
        QDir(m_folder).removeRecursively();
}

// --- Caches ----------------------------------------------------------------------------

Caches::Caches(Storage& storage, TorrentTransport& torrents, const QString& imageFolder, QObject* parent)
    : QObject(parent)
    , m_storage(storage)
    , m_torrents(torrents)
    , m_factory(imageFolder, qint64(qBound(50, storage.setting(kImageCacheKey, kDefaultImageCacheMB).toInt(), 10000))
                                 * 1024 * 1024)
    , m_imageCacheMB(qBound(50, storage.setting(kImageCacheKey, kDefaultImageCacheMB).toInt(), 10000))
{
}

Caches::~Caches()
{
    *m_alive = false;
    for (std::thread& t : m_threads)
        t.join();
}

void Caches::setImageCacheMB(int mb)
{
    mb = qBound(50, mb, 10000);
    if (mb == m_imageCacheMB)
        return;
    m_imageCacheMB = mb;
    m_storage.setSetting(kImageCacheKey, mb);
    m_factory.setMaximumSize(qint64(mb) * 1024 * 1024);
    emit imageCacheMBChanged();
}

QString Caches::describe(qint64 bytes)
{
    if (bytes < 1024 * 1024)
        return QStringLiteral("under 1 MB");
    if (bytes < qint64(1024) * 1024 * 1024)
        return QStringLiteral("%1 MB").arg(bytes / (1024 * 1024));
    return QStringLiteral("%1 GB").arg(double(bytes) / (1024.0 * 1024 * 1024), 0, 'f', 1);
}

void Caches::runInBackground(std::function<void()> job)
{
    if (m_busy)
        return;
    m_busy = true;
    emit usageChanged();
    // Joined at the latest when this object goes; finished ones go now.
    m_threads.emplace_back([this, alive = m_alive, job = std::move(job)] {
        job();
        const qint64 images = folderSize(m_factory.folder());
        const qint64 torrents = m_torrents.cacheUsage();
        if (!*alive)
            return;
        QMetaObject::invokeMethod(this, [this, alive, images, torrents] {
            if (!*alive)
                return;
            m_imageUsed = describe(images);
            m_torrentUsed = describe(torrents);
            m_busy = false;
            emit usageChanged();
        }, Qt::QueuedConnection);
    });
}

void Caches::measure()
{
    runInBackground([] {});
}

void Caches::clearImages()
{
    qCInfo(lcApp) << "Clearing the image cache";
    m_factory.clear();
    // Each cache empties itself on its own thread in a moment.
    QTimer::singleShot(500, this, &Caches::measure);
}

void Caches::clearTorrents()
{
    qCInfo(lcApp) << "Clearing the torrent cache";
    runInBackground([this] { m_torrents.clearCache(); });
}

} // namespace cj
