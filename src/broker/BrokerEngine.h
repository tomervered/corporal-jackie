#pragma once
// Inside the stream broker, shared by its parts: the constants that tune it,
// a torrent and its readers as the broker tracks them, and the Engine that
// owns the libtorrent session. Not for use outside src/broker.
//
//   BrokerEngine.cpp     the session, adding torrents, metadata, alerts
//   BrokerScheduling.cpp which pieces to ask for and when; RAM trimming
//   BrokerUpkeep.cpp     reloads, resume data, the disk cache cap, stats
//   StreamBroker.cpp     the public StreamBroker and StreamHandle

#include "StreamBroker.h"

#include "Logging.h"
#include "MemoryStorage.h"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStorageInfo>

#include <libtorrent/alert_types.hpp>
#include <libtorrent/bencode.hpp>
#include <libtorrent/load_torrent.hpp>
#include <libtorrent/magnet_uri.hpp>
#include <libtorrent/peer_info.hpp>
#include <libtorrent/read_resume_data.hpp>
#include <libtorrent/write_resume_data.hpp>
#include <libtorrent/session.hpp>
#include <libtorrent/session_params.hpp>
#include <libtorrent/torrent_info.hpp>
#include <libtorrent/torrent_status.hpp>

#include <chrono>
#include <condition_variable>
#include <limits>
#include <list>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <thread>
#include <unordered_map>

#ifdef Q_OS_WIN
#  include <windows.h>
#else
#  include <sys/stat.h>
#endif

namespace cj {
namespace broker_detail {


using Clock = std::chrono::steady_clock;

constexpr qint64 kHeadBytes = 4 * 1024 * 1024;   // fetched first: headers
constexpr qint64 kTailBytes = 4 * 1024 * 1024;   // and the index at the end
constexpr qint64 kPieceCacheBytes = 64 * 1024 * 1024;  // piece copies kept in memory per torrent
constexpr qint64 kDiskReserve = 2LL * 1024 * 1024 * 1024; // free disk space the cache always leaves
constexpr auto kStallAfter = std::chrono::seconds(10);    // no new data for the awaited piece
// Beyond the urgent window, keep downloading at low priority, so spare
// bandwidth builds a buffer: up to this many seconds of video ahead...
constexpr double kBackgroundSeconds = 600;
// ...and at most this many bytes, so one big file does not fill the cache.
constexpr qint64 kBackgroundMaxBytes = 2LL * 1024 * 1024 * 1024;

// Open trackers added to every public torrent (never the only source: DHT
// and the link's own trackers stay). Never to a private one (BEP 27): they
// would learn its info hash and the viewer's address.
constexpr const char* kPublicTrackers[] = {
    "udp://tracker.opentrackr.org:1337/announce",
    "udp://open.demonii.com:1337/announce",
    "udp://open.stealth.si:80/announce",
    "udp://tracker.torrent.eu.org:451/announce",
    "udp://exodus.desync.com:6969/announce",
    "udp://explodie.org:6969/announce",
    "udp://tracker.dler.org:6969/announce",
    "udp://tracker.openbittorrent.com:6969/announce",
};
constexpr auto kReannounceAfter = std::chrono::seconds(15); // no peers: ask trackers and DHT again
constexpr auto kCacheCheckEvery = std::chrono::seconds(60);
constexpr const char* kMetadataFile = "metadata.torrent";
constexpr const char* kLastUsedFile = ".last-used";
// libtorrent's record of which pieces are verified. With it, a torrent
// reopens at once instead of hashing everything on disk again first.
constexpr const char* kResumeFile = "resume.dat";
constexpr auto kResumeSaveWait = std::chrono::seconds(3);  // at shutdown, at most
// RAM only: what is kept behind each reader for
// short seeks back, how far under the cap a trim goes, and the least time
// between two reloads of one torrent.
constexpr qint64 kMemoryBehind = 64LL * 1024 * 1024;
constexpr double kMemoryTrimTo = 0.85;
constexpr auto kReloadEvery = std::chrono::seconds(2);

// The files as the torrent names them (libtorrent 2.1 renamed files() to
// layout()).
inline const lt::file_storage& fileLayout(const lt::torrent_info& info)
{
#if LIBTORRENT_VERSION_NUM >= 20100
    return info.layout();
#else
    return info.files();
#endif
}

inline std::string hexOf(const lt::sha1_hash& h)
{
    std::ostringstream s;
    s << h;
    return s.str();
}

// The v1 hash when there is one, as magnet links' "btih" carry it; callers
// such as the torrent transport name torrents by it.
inline std::string idOf(const lt::info_hash_t& hashes)
{
    return hashes.has_v1() ? hexOf(hashes.v1) : hexOf(hashes.get_best());
}

// One reader of one file: a handle. Its position drives the deadlines.
struct Reader
{
    int fileIndex = 0;
    qint64 fileOffset = 0;   // where the file starts inside the torrent
    qint64 fileSize = 0;
    QString fileName;
    std::atomic<qint64> position{0};
    std::atomic<double> duration{0};
    std::atomic<bool> cancelled{false};
    std::atomic<bool> closed{false};
    int scheduledPiece = -1;  // the piece the last schedule was computed for
    // Progress on the piece this reader waits for, to tell slow from stuck.
    // Written by stats() only, which runs on one thread at a time per reader.
    mutable int waitPiece = -1;
    mutable int waitBlocks = -1;
    mutable Clock::time_point waitProgressAt = Clock::now();
    mutable bool stallLogged = false;

    double bitrate() const
    {
        const double d = duration.load();
        return d > 0 ? double(fileSize) / d : kDefaultBitrate;
    }
};

struct Torrent
{
    std::string id;
    QString dir;
    lt::torrent_handle handle;

    std::mutex mutex;
    std::condition_variable cv;
    bool hasMetadata = false;
    bool failed = false;
    bool removed = false;
    std::string error;
    std::shared_ptr<const lt::torrent_info> info;
    std::vector<bool> have;
    std::map<int, std::pair<boost::shared_array<char>, int>> pieces;  // index -> data, size
    std::list<int> pieceOrder;                                         // least recent first
    qint64 pieceBytes = 0;                                             // sum of sizes in `pieces`
    std::set<int> readsPending;
    std::map<int, int> readErrors;
    std::map<int, int> scheduled;  // piece -> deadline in ms, as last sent
    std::set<int> background;      // pieces wanted at low priority, as last sent
    // RAM only: verified pieces dropped from memory that libtorrent still
    // counts as had. A reader needing one makes the torrent reload.
    std::set<int> evicted;
    bool reloadWanted = false;
    Clock::time_point reloadedAt;
    // Peers named by the links that opened it: tried again on a reload or
    // when no peers are left (a link's peers may be its only source).
    std::vector<lt::tcp::endpoint> linkPeers;
    std::vector<std::weak_ptr<Reader>> readers;
    int openHandles = 0;
    qint64 downloadRate = 0;
    Clock::time_point lastPieceAt = Clock::now();   // last piece finished, or a reader opened
    Clock::time_point peersSeenAt = Clock::now();   // last time it had a peer

    // The libtorrent handle, read under `mutex`: a RAM-only reload replaces
    // it from the alert thread. Use the copy outside the lock.
    lt::torrent_handle currentHandle()
    {
        std::lock_guard tl(mutex);
        return handle;
    }

    qint64 pieceLength() const { return info ? info->piece_length() : 0; }
    int pieceAt(qint64 torrentOffset) const { return int(torrentOffset / info->piece_length()); }

    void touch() const
    {
        QFile f(QDir(dir).filePath(QString::fromLatin1(kLastUsedFile)));
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
            f.write(QByteArray::number(QDateTime::currentMSecsSinceEpoch()));
    }

    // Verified bytes in a row from `offset` within the reader's file.
    qint64 bufferedFrom(const Reader& r, qint64 offset) const
    {
        if (!info || offset >= r.fileSize)
            return 0;
        const qint64 end = r.fileOffset + r.fileSize;
        qint64 pos = r.fileOffset + offset;
        int p = pieceAt(pos);
        while (pos < end && p < int(have.size()) && have[p]) {
            pos = qint64(p + 1) * pieceLength();
            ++p;
        }
        return qMin(pos, end) - (r.fileOffset + offset);
    }
};

// Shared by the broker and its handles, so a handle that outlives the
// broker fails cleanly instead of crashing.
class Engine
{
public:
    explicit Engine(const BrokerSettings& settings);
    ~Engine();

    void shutdown();

    std::shared_ptr<Torrent> addOrFind(const OpenRequest& req, BrokerError* error);
    void releaseHandle(Torrent& t);
    void schedule(Torrent& t, bool force);  // t.mutex must be held
    // cap < 0: the configured cap. 0 empties the cache of everything not open.
    void enforceCacheCap(const std::string& keep = {}, qint64 cap = -1);
    StreamStats stats(Torrent& t, const Reader& r);
    StreamStats torrentStats(const std::string& id);
    int listenPort() const { return m_session ? m_session->listen_port() : 0; }
    bool memoryOnly() const { return bool(m_memory); }
    qint64 memoryCap() const { return settings.memoryCapBytes; }
    qint64 memoryUsed() const { return m_memory ? m_memory->totalBytes() : 0; }
    // Most bytes ahead of a reader worth asking for: on disk, half the cache;
    // in RAM, half the memory cap, so the rest holds what was just watched.
    qint64 aheadCap() const { return memoryOnly() ? memoryCap() / 2 : settings.cacheCapBytes / 2; }
    // Does the torrent hold this verified piece? In RAM only, libtorrent's
    // answer is wrong for pieces dropped from memory. t.mutex must be held.
    bool holds(Torrent& t, int piece);

    BrokerSettings settings;
    std::vector<std::string> extraTrackers;  // for public torrents only
    std::atomic<bool> closing{false};

private:
    void alertLoop();
    void refresh(Torrent& t);
    void handleAlert(lt::alert* a);
    void onMetadata(Torrent& t);  // t.mutex must be held
    void dropExtraTrackers(lt::add_torrent_params& atp) const;
    void saveResume(Torrent& t);
    void trimMemory();
    void reload(const std::shared_ptr<Torrent>& t);
    std::shared_ptr<Torrent> find(const lt::torrent_handle& h);

    std::shared_ptr<MemoryRegistry> m_memory;  // RAM only
    std::unique_ptr<lt::session> m_session;
    std::mutex m_mutex;  // guards m_torrents
    std::unordered_map<std::string, std::shared_ptr<Torrent>> m_torrents;
    QHash<QString, QString> m_pendingDeletes;  // info hash -> folder, until libtorrent is done
    std::thread m_thread;
    std::atomic<int> m_pendingSaves{0};
    std::atomic<bool> m_shutdownStarted{false};
    bool m_debugAlerts = qEnvironmentVariableIsSet("CJ_TORRENT_LOG");
};

} // namespace broker_detail
} // namespace cj
