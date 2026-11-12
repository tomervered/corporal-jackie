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
    // Which third parties the app may contact. All off until the person has
    // answered the first-run screen; each applies at once.
    Q_PROPERTY(bool privacyChosen READ privacyChosen NOTIFY privacyChanged)
    Q_PROPERTY(bool useCinemeta READ useCinemeta WRITE setUseCinemeta NOTIFY privacyChanged)
    Q_PROPERTY(bool useDht READ useDht WRITE setUseDht NOTIFY privacyChanged)
    Q_PROPERTY(bool usePublicTrackers READ usePublicTrackers WRITE setUsePublicTrackers NOTIFY privacyChanged)
    Q_PROPERTY(bool usePortMapping READ usePortMapping WRITE setUsePortMapping NOTIFY privacyChanged)

public:
    // defaultCacheFolder: the torrent cache under the app's cache folder.
    Preferences(Storage& storage, const QString& defaultCacheFolder, QObject* parent = nullptr);

    bool torrentsInMemory() const { return m_inMemory; }
    int memoryCapMB() const { return m_memoryCapMB; }
    int diskCacheGB() const { return m_diskCacheGB; }
    QString cacheFolder() const;   // as a path, native separators
    bool customCacheFolder() const { return !m_cacheFolder.isEmpty(); }
    bool restartNeeded() const;

    bool privacyChosen() const { return m_privacyChosen; }
    // The default Cinemeta add-on (its lists, details and search).
    bool useCinemeta() const { return m_cinemeta; }
    // The DHT and discovery on the local network, to find more peers.
    bool useDht() const { return m_dht; }
    // The app's own list of open trackers, added to public torrents.
    bool usePublicTrackers() const { return m_publicTrackers; }
    // Asking the router to open a port (UPnP, NAT-PMP).
    bool usePortMapping() const { return m_portMapping; }
    void setUseCinemeta(bool on);
    void setUseDht(bool on);
    void setUsePublicTrackers(bool on);
    void setUsePortMapping(bool on);
    // The first-run screen's answer: records all four and that the person
    // has chosen. Nothing is applied before this.
    Q_INVOKABLE void choosePrivacy(bool cinemeta, bool dht, bool publicTrackers, bool portMapping);

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
    // Any of the four choices, or that they were made.
    void privacyChanged();
    // Cinemeta went from off to on or back: install or remove it.
    void cinemetaChoiceChanged(bool on);

private:
    void save();
    void savePrivacy(bool cinemetaBefore);
    QString effectiveFolder() const;

    Storage& m_storage;
    QString m_defaultFolder;
    bool m_inMemory = true;
    int m_memoryCapMB = 1024;
    int m_diskCacheGB = 10;
    QString m_cacheFolder;  // empty = default
    bool m_privacyChosen = false;
    bool m_cinemeta = false;
    bool m_dht = false;
    bool m_publicTrackers = false;
    bool m_portMapping = false;
    struct Started { bool inMemory; int memoryCapMB; int diskCacheGB; QString folder; } m_started;
};

} // namespace cj
