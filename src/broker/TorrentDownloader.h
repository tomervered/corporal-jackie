#pragma once
// Downloads torrents the ordinary way, for the "download as a normal torrent"
// setting: libtorrent picks pieces rarest first and writes the file straight
// into the folder the person chose, then can go on sharing it. Nothing here
// is for playback; the stream broker does that.
//
// A libtorrent session of its own, started on first use, with libtorrent's
// own disk storage: the stream broker's session may keep pieces in RAM only,
// and its cache cap and piece deadlines must not touch these files. It
// follows the same network choices (DHT, LAN discovery, port mapping, open
// trackers only for public torrents) and shares only the downloads the person
// switched sharing on for.
//
// Thread-safe. status() never waits on libtorrent: a thread of its own polls
// twice a second.

#include "BrokerTypes.h"

#include <QString>

#include <memory>

namespace cj {

namespace downloader_detail {
class Engine;
}

struct DownloadStatus
{
    enum class Phase { FindingPeers, FetchingMetadata, Downloading, Finished, Failed, Gone };
    Phase phase = Phase::Gone;
    QString name;           // the file's name, once the file list is here
    QString path;           // where the file is written, once known
    qint64 doneBytes = 0;
    qint64 sizeBytes = -1;
    qint64 downloadRate = 0;
    qint64 uploadRate = 0;
    qint64 uploadedBytes = 0;
    int peers = 0;
    bool sharing = false;   // sends pieces to peers
    bool seeding = false;   // finished and still in the session (sharing)
    QString error;
};

class TorrentDownloader
{
public:
    // Uses the network fields of `settings` (and loopbackOnly, listenPort and
    // extraTrackers for tests). Nothing starts until the first add().
    explicit TorrentDownloader(const BrokerSettings& settings);
    ~TorrentDownloader();   // stops every download; finished files stay

    TorrentDownloader(const TorrentDownloader&) = delete;
    TorrentDownloader& operator=(const TorrentDownloader&) = delete;

    // Starts downloading one file (`fileIndex`, or -1 for the largest) of a
    // magnet link, info hash or .torrent file into `folder`. Returns an id
    // (> 0), or 0 and the reason. A torrent already being downloaded here
    // cannot be added twice.
    int add(const QString& uri, int fileIndex, const QString& folder, bool share, BrokerError* error = nullptr);

    DownloadStatus status(int id) const;

    // Sharing on keeps a finished download in the session, seeding, until it
    // is switched off (then the torrent leaves; the file stays) or the app
    // closes.
    void setSharing(int id, bool on);

    // Stops a download. `deleteFiles`: removes what it wrote (for a cancel).
    void remove(int id, bool deleteFiles);

    // DHT, LAN discovery, port mapping and open trackers, while running.
    void applyNetwork(const BrokerSettings& settings);

private:
    std::shared_ptr<downloader_detail::Engine> m_engine;
};

} // namespace cj
