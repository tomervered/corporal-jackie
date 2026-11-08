#pragma once
// App-wide preferences the UI edits that are not part of playback: where and
// how torrents are stored. Saved in the app's database. Exposed to QML as
// `preferences`.
//
// The torrent engine reads these once at start, so changes apply the next
// time the app opens; `restartNeeded` says when that is the case.

#include <QObject>
#include <QString>
#include <QUrl>

namespace cj {

class Storage;

class Preferences : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool torrentsInMemory READ torrentsInMemory WRITE setTorrentsInMemory NOTIFY changed)
    Q_PROPERTY(int memoryCapMB READ memoryCapMB WRITE setMemoryCapMB NOTIFY changed)
    Q_PROPERTY(int diskCacheGB READ diskCacheGB WRITE setDiskCacheGB NOTIFY changed)
    Q_PROPERTY(QString cacheFolder READ cacheFolder NOTIFY changed)
    Q_PROPERTY(bool customCacheFolder READ customCacheFolder NOTIFY changed)
    Q_PROPERTY(bool restartNeeded READ restartNeeded NOTIFY changed)

public:
    // defaultCacheFolder: the torrent cache under the app's cache folder.
    Preferences(Storage& storage, const QString& defaultCacheFolder, QObject* parent = nullptr);

    bool torrentsInMemory() const { return m_inMemory; }
    int memoryCapMB() const { return m_memoryCapMB; }
    int diskCacheGB() const { return m_diskCacheGB; }
    QString cacheFolder() const;   // as a path, native separators
    bool customCacheFolder() const { return !m_cacheFolder.isEmpty(); }
    bool restartNeeded() const;

    void setTorrentsInMemory(bool on);
    void setMemoryCapMB(int mb);
    void setDiskCacheGB(int gb);
    Q_INVOKABLE void setCacheFolder(const QUrl& folder);
    Q_INVOKABLE void resetCacheFolder();

    // What the running engine uses: call once at start, before the engine.
    bool startInMemory() const { return m_started.inMemory; }
    qint64 startMemoryCapBytes() const { return qint64(m_started.memoryCapMB) * 1024 * 1024; }
    qint64 startDiskCacheBytes() const { return qint64(m_started.diskCacheGB) * 1024 * 1024 * 1024; }
    QString startCacheFolder() const { return m_started.folder; }

signals:
    void changed();

private:
    void save();
    QString effectiveFolder() const;

    Storage& m_storage;
    QString m_defaultFolder;
    bool m_inMemory = true;
    int m_memoryCapMB = 1024;
    int m_diskCacheGB = 10;
    QString m_cacheFolder;  // empty = default
    struct Started { bool inMemory; int memoryCapMB; int diskCacheGB; QString folder; } m_started;
};

} // namespace cj
