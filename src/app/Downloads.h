#pragma once
// Saving a video to a folder the person picks, apart from the torrent cache.
// Exposed to QML as `downloads`.
//
// Torrents go one of two ways:
// - Streamable (the default): read from start to end through the stream
//   broker, as a reader of its own, into "<name>.part"; renamed at the end.
//   The broker's cache (RAM or disk) works as for playback, so a download and
//   playback of the same file share the pieces, and the start arrives first.
// - Normal (Settings > Downloads > Advanced): the TorrentDownloader, rarest
//   pieces first, written straight into the folder, and can go on sharing
//   (seeding) after it finishes.
// Each download can share with peers or not; it starts as the "share what I
// download" switch says (off unless the person allowed uploads).
// A web link is fetched with Qt's network stack; its headers go only to the
// server it names.
//
// Privacy: the folder, the normal-torrent choice and the list of downloads
// (name, size, state, where the file is) are kept in the local database; the
// list only while "Remember what I watch" is on, and "Clear finished" empties
// it. The log says that a download started or ended with a number, never what
// it was or where it came from.

#include "BrokerTypes.h"
#include "PlaybackTypes.h"

#include <QObject>
#include <QString>
#include <QTimer>
#include <QUrl>
#include <QVariantList>

#include <memory>
#include <mutex>
#include <vector>

class QNetworkAccessManager;
class QNetworkReply;
class QFile;

namespace cj {

class Browser;
class Preferences;
class Session;
class Storage;
class TorrentDownloader;
class TorrentTransport;
class StreamHandle;
struct OpenRequest;

class Downloads : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString folder READ folder NOTIFY folderChanged)
    Q_PROPERTY(bool customFolder READ customFolder NOTIFY folderChanged)
    // Advanced: torrents download like in a torrent program, not in order.
    Q_PROPERTY(bool normalTorrents READ normalTorrents WRITE setNormalTorrents NOTIFY normalTorrentsChanged)
    // One map per download, newest first: id, name, state ("starting",
    // "downloading", "seeding", "done", "failed", "cancelled"), percent,
    // detail (sizes, speed or the error), path (once done), torrent (bool),
    // mode ("stream", "normal" or "web"), sharing (bool) and canShare.
    Q_PROPERTY(QVariantList items READ items NOTIFY itemsChanged)
    Q_PROPERTY(int activeCount READ activeCount NOTIFY itemsChanged)

public:
    // The database key of the chosen folder; nothing else is stored.
    static constexpr auto kFolderKey = "downloads/folder";
    static constexpr auto kNormalKey = "downloads/normalTorrents";
    // (The list of downloads is kept under "downloads/list"; see save().)

    // defaultFolder: used until the person picks one (a "Corporal Jackie"
    // folder in their Downloads). `torrents` may be null (no torrent support).
    Downloads(Storage& storage, Preferences& preferences, TorrentTransport* torrents,
              const QString& defaultFolder, QObject* parent = nullptr);
    ~Downloads() override;

    // Where the Download buttons come from.
    void setBrowser(Browser* browser) { m_browser = browser; }
    void setSession(Session* session) { m_session = session; }

    QString folder() const;   // native separators
    bool customFolder() const { return !m_folder.isEmpty(); }
    Q_INVOKABLE void setFolder(const QUrl& folder);
    Q_INVOKABLE void resetFolder();
    Q_INVOKABLE void openFolder() const;
    bool normalTorrents() const { return m_normal; }
    void setNormalTorrents(bool on);

    // Settings for the normal-torrent engine (network choices); applied to
    // a running one too.
    void setNetwork(const BrokerSettings& settings);

    QVariantList items() const;
    int activeCount() const;

    // Whether a source can be saved: a torrent or a web link. A local file
    // is already on this computer.
    static bool canDownload(const PlayableSource& source);
    Q_INVOKABLE bool canDownloadStream(int streamIndex) const;
    Q_INVOKABLE bool canDownloadPlaying() const;

    // Start saving. Each returns an empty string, or why it cannot start.
    QString start(const PlayableSource& source);
    Q_INVOKABLE QString downloadStream(int streamIndex);   // from browser.streams
    Q_INVOKABLE QString downloadPlaying();                 // what the Session has open

    Q_INVOKABLE void cancel(int id);
    // Sends pieces of this torrent to other peers, or stops. A finished
    // normal download keeps sharing until switched off.
    Q_INVOKABLE void setSharing(int id, bool on);
    // Takes finished, failed and cancelled ones off the list.
    Q_INVOKABLE void clearFinished();

    // Cancels everything and waits for the torrent readers to let go. Call
    // before the torrent transport goes; the destructor calls it too.
    void stop();

    // A file name from text that came from outside (a torrent's file name, a
    // link): no folders, no characters Windows refuses, not a reserved
    // Windows name, not empty, at most 180 characters. Public for tests.
    static QString safeFileName(const QString& name, const QString& fallback = QStringLiteral("video"));

signals:
    void folderChanged();
    void itemsChanged();
    void normalTorrentsChanged();
    // Finished or failed, for a toast.
    void notice(const QString& text, bool isError);

public:
    struct Job;   // one download; defined in the .cpp

private:
    QString effectiveFolder() const;
    void saveTorrentToFolder(const std::shared_ptr<Job>& job, const OpenRequest& request, const QString& folder);
    static QString copyTorrentBytes(Job& job, StreamHandle& stream, const QString& part, qint64 size);
    QString startTorrent(const PlayableSource& source);
    QString startNormal(const std::shared_ptr<Job>& job, const QString& uri, int fileIndex);
    QString startWeb(const PlayableSource& source);
    void cancelJob(const std::shared_ptr<Job>& job);
    void finish(const std::shared_ptr<Job>& job);
    void refresh();
    void save();   // the list, to the database
    void load();   // and back, as the app starts

    Storage& m_storage;
    Preferences& m_preferences;
    TorrentTransport* m_torrents = nullptr;
    BrokerSettings m_netSettings;
    std::unique_ptr<TorrentDownloader> m_normalEngine;   // started on first use
    Browser* m_browser = nullptr;   // both outlive this (AppCore)
    Session* m_session = nullptr;
    QString m_defaultFolder;
    QString m_folder;   // empty = default
    bool m_normal = false;
    QNetworkAccessManager* m_network = nullptr;
    QTimer m_refresh;    // while any download runs
    int m_nextId = 1;
    std::vector<std::shared_ptr<Job>> m_jobs;  // newest last
    bool m_stopped = false;
};

} // namespace cj
