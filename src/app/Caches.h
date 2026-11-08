#pragma once
// The two caches the user can see and empty in Settings: posters and other
// images (an HTTP disk cache under the app's cache folder, for every network
// request QML makes), and the torrent cache. Exposed to QML as `caches`.

#include <QObject>
#include <QQmlNetworkAccessManagerFactory>

#include <atomic>
#include <functional>
#include <memory>
#include <thread>

class QNetworkDiskCache;

namespace cj {

class Storage;
class TorrentTransport;

// Tells every disk cache, each on its own thread, about a change. Queued
// connections carry it there, and a cache that is gone is disconnected.
class ImageCacheSignals : public QObject
{
    Q_OBJECT
signals:
    void maximumSizeChanged(qint64 bytes);
    void clearRequested();
};

// Gives every QML network access manager a disk cache in one folder, capped.
// QML makes managers on its own threads and calls create() there.
class ImageCacheFactory : public QQmlNetworkAccessManagerFactory
{
public:
    ImageCacheFactory(const QString& folder, qint64 maxBytes);
    QNetworkAccessManager* create(QObject* parent) override;

    QString folder() const { return m_folder; }
    void setMaximumSize(qint64 bytes);
    // Each cache empties itself a moment later, on its own thread.
    void clear();

private:
    const QString m_folder;
    std::atomic<qint64> m_maxBytes;
    std::atomic<int> m_live{0};   // caches that exist now
    ImageCacheSignals m_signals;
};

class Caches : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int imageCacheMB READ imageCacheMB WRITE setImageCacheMB NOTIFY imageCacheMBChanged)
    Q_PROPERTY(QString imageCacheUsed READ imageCacheUsed NOTIFY usageChanged)
    Q_PROPERTY(QString torrentCacheUsed READ torrentCacheUsed NOTIFY usageChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY usageChanged)

public:
    static constexpr int kDefaultImageCacheMB = 250;

    Caches(Storage& storage, TorrentTransport& torrents, const QString& imageFolder, QObject* parent = nullptr);
    ~Caches() override;
    // Waits for a measurement in progress: it asks the torrent transport,
    // which must still be there.
    void stop();

    // Install on the QML engine before it loads anything; the engine does
    // not take ownership.
    QQmlNetworkAccessManagerFactory* factory() { return &m_factory; }

    int imageCacheMB() const { return m_imageCacheMB; }
    void setImageCacheMB(int mb);
    QString imageCacheUsed() const { return m_imageUsed; }
    QString torrentCacheUsed() const { return m_torrentUsed; }
    bool busy() const { return m_busy; }

    // Measures both caches in the background; usageChanged says when done.
    Q_INVOKABLE void measure();
    Q_INVOKABLE void clearImages();
    Q_INVOKABLE void clearTorrents();

    static QString describe(qint64 bytes);

signals:
    void imageCacheMBChanged();
    void usageChanged();

private:
    void runInBackground(std::function<void()> job);

    Storage& m_storage;
    TorrentTransport& m_torrents;
    ImageCacheFactory m_factory;
    int m_imageCacheMB;
    QString m_imageUsed;
    QString m_torrentUsed;
    bool m_busy = false;
    std::shared_ptr<std::atomic<bool>> m_alive = std::make_shared<std::atomic<bool>>(true);
    std::thread m_thread;   // measuring or clearing; one at a time
};

} // namespace cj
