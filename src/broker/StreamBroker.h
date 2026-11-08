#pragma once
// The torrent stream broker: turns one file inside a torrent into bytes that
// any reader can read at any offset. Read docs/BROKER_CONTRACT.md first.
//
// The only code in the app that calls libtorrent. It never knows who reads.
// Every method is safe to call from any thread; reads block the calling
// thread (mpv's stream thread in Step 4), never the UI.

#include "BrokerTypes.h"

#include <atomic>
#include <functional>
#include <memory>
#include <string>

namespace cj {

namespace broker_detail {
struct Torrent;
struct Reader;
class Engine;
}

struct OpenRequest
{
    // A magnet link, a 40-character info hash, or a path to a .torrent file.
    QString uri;
    // Which file to stream; -1 picks the largest file.
    int fileIndex = -1;
    // The video's length if known (Stremio often knows it). Used to estimate
    // the bitrate until the player reports the real duration.
    double durationSeconds = 0;
};

class StreamHandle
{
public:
    ~StreamHandle();  // closes the handle

    qint64 size() const;
    int fileIndex() const;
    QString fileName() const;
    QString infoHash() const;

    // Reads up to `length` bytes at `offset` into `buffer`. Waits until the
    // bytes are downloaded and verified. Returns the number of bytes read
    // (0 at the end of the file), or -1 if cancelled or closed, -2 on error.
    // May return fewer bytes than asked (it never crosses a piece boundary).
    qint64 read(qint64 offset, char* buffer, qint64 length);

    // Makes the pending read and every later read return -1. For when the
    // reader goes away (mpv closing the stream); a seek is just a new read.
    void cancel();
    void close();

    // The player's real duration, once known; improves the read-ahead.
    void setDuration(double seconds);

    StreamStats stats() const;

private:
    friend class StreamBroker;
    StreamHandle() = default;
    std::shared_ptr<broker_detail::Engine> m_engine;
    std::shared_ptr<broker_detail::Torrent> m_torrent;
    std::shared_ptr<broker_detail::Reader> m_reader;
};

class StreamBroker
{
public:
    explicit StreamBroker(const BrokerSettings& settings);
    ~StreamBroker();  // cancels every read; handles stop working

    StreamBroker(const StreamBroker&) = delete;
    StreamBroker& operator=(const StreamBroker&) = delete;

    // Adds the torrent if needed, waits for its file list, and opens one file
    // as a new reader. Blocks until then, the metadata timeout, or until
    // `cancelled` returns true (checked every 100 ms). Several handles may be
    // open on one file; each gets its own read-ahead.
    std::shared_ptr<StreamHandle> open(const OpenRequest& request, BrokerError* error = nullptr,
                                       const std::function<bool()>& cancelled = {});

    // Peers and state of a torrent that may still be opening (no handle yet):
    // FindingPeers, FetchingMetadata, or Buffering once the file list is here.
    // Default stats if the broker does not have it.
    StreamStats torrentStats(const QString& infoHash) const;

    // The libtorrent listen port, for tests and the debug overlay.
    int listenPort() const;

    // Deletes least recently used torrents until the cache is under its cap.
    // Torrents with an open handle are never deleted. Runs on its own after
    // open and close; public for tests.
    void enforceCacheCap();
    // Deletes every cached torrent that is not being watched.
    void clearCache();

    // RAM only (BrokerSettings::memoryOnly): bytes of pieces held in memory,
    // over all torrents. Kept near the memory cap; 0 when pieces go to disk.
    bool memoryOnly() const;
    qint64 memoryUsed() const;

    // The info hash (lower-case hex) of a .torrent file, and its name. Empty
    // if the file cannot be read as a torrent.
    static QString infoHashOfFile(const QString& torrentFile, QString* name = nullptr);

    // Disk space the cache folder really uses (sparse files count only the
    // parts written).
    static qint64 diskUsage(const QString& path);

private:
    std::shared_ptr<broker_detail::Engine> m_engine;
};

} // namespace cj
