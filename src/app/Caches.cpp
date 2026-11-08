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
    cache->setMaximumCacheSize(m_maxBytes);
    manager->setCache(cache);
    ++m_live;
    QObject::connect(cache, &QObject::destroyed, [this] { --m_live; });
    // `cache` as the context: the change runs on the cache's own thread.
    QObject::connect(&m_signals, &ImageCacheSignals::maximumSizeChanged, cache,
                     [cache](qint64 bytes) { cache->setMaximumCacheSize(bytes); });
    QObject::connect(&m_signals, &ImageCacheSignals::clearRequested, cache, [cache] { cache->clear(); });
    return manager;
}

void ImageCacheFactory::setMaximumSize(qint64 bytes)
{
    m_maxBytes = bytes;
    emit m_signals.maximumSizeChanged(bytes);
}

void ImageCacheFactory::clear()
{
    if (m_live > 0)
        emit m_signals.clearRequested();
    else
        QDir(m_folder).removeRecursively();  // no cache has its files open
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
    stop();
}

void Caches::stop()
{
    *m_alive = false;
    if (m_thread.joinable())
        m_thread.join();
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
    if (m_busy || !*m_alive)  // stopped: the app is closing
        return;
    m_busy = true;
    emit usageChanged();
    // The last one has finished: it reported before m_busy went false.
    if (m_thread.joinable())
        m_thread.join();
    m_thread = std::thread([this, alive = m_alive, job = std::move(job)] {
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
