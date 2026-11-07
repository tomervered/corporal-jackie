#include "StreamBroker.h"

#include "Logging.h"

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
#include <libtorrent/create_torrent.hpp>
#include <libtorrent/magnet_uri.hpp>
#include <libtorrent/read_resume_data.hpp>
#include <libtorrent/write_resume_data.hpp>
#include <libtorrent/session.hpp>
#include <libtorrent/session_params.hpp>
#include <libtorrent/torrent_info.hpp>
#include <libtorrent/torrent_status.hpp>

#include <chrono>
#include <condition_variable>
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
constexpr auto kStallAfter = std::chrono::seconds(10);    // no new piece while a reader waits
constexpr auto kReannounceAfter = std::chrono::seconds(15); // no peers: ask trackers and DHT again
constexpr auto kCacheCheckEvery = std::chrono::seconds(60);
constexpr const char* kMetadataFile = "metadata.torrent";
constexpr const char* kLastUsedFile = ".last-used";
// libtorrent's record of which pieces are verified. With it, a torrent
// reopens at once instead of hashing everything on disk again first.
constexpr const char* kResumeFile = "resume.dat";
constexpr auto kResumeSaveWait = std::chrono::seconds(3);  // at shutdown, at most

std::string hexOf(const lt::sha1_hash& h)
{
    std::ostringstream s;
    s << h;
    return s.str();
}

// The v1 hash when there is one, as magnet links' "btih" carry it; callers
// such as the torrent transport name torrents by it.
std::string idOf(const lt::info_hash_t& hashes)
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
    std::vector<std::weak_ptr<Reader>> readers;
    int openHandles = 0;
    qint64 downloadRate = 0;
    Clock::time_point lastPieceAt = Clock::now();   // last piece finished, or a reader opened
    Clock::time_point peersSeenAt = Clock::now();   // last time it had a peer

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
    void enforceCacheCap(const std::string& keep = {});
    StreamStats stats(Torrent& t, const Reader& r);
    StreamStats torrentStats(const std::string& id);
    int listenPort() const { return m_session ? m_session->listen_port() : 0; }

    BrokerSettings settings;
    std::atomic<bool> closing{false};

private:
    void alertLoop();
    void refresh(Torrent& t);
    void handleAlert(lt::alert* a);
    void onMetadata(Torrent& t);  // t.mutex must be held
    void saveResume(Torrent& t);
    std::shared_ptr<Torrent> find(const lt::torrent_handle& h);

    std::unique_ptr<lt::session> m_session;
    std::mutex m_mutex;  // guards m_torrents
    std::unordered_map<std::string, std::shared_ptr<Torrent>> m_torrents;
    QHash<QString, QString> m_pendingDeletes;  // info hash -> folder, until libtorrent is done
    std::thread m_thread;
    std::atomic<int> m_pendingSaves{0};
    std::atomic<bool> m_shutdownStarted{false};
    bool m_debugAlerts = qEnvironmentVariableIsSet("CJ_TORRENT_LOG");
};

Engine::Engine(const BrokerSettings& s)
    : settings(s)
{
    QDir().mkpath(settings.cacheDir);

    lt::settings_pack pack;
    pack.set_str(lt::settings_pack::user_agent, "CorporalJackie/0.1 libtorrent/" LIBTORRENT_VERSION);
    const std::string port = std::to_string(settings.listenPort);
    pack.set_str(lt::settings_pack::listen_interfaces,
                 settings.loopbackOnly ? "127.0.0.1:" + port : "0.0.0.0:" + port + ",[::]:" + port);
    const bool open = !settings.loopbackOnly;
    pack.set_bool(lt::settings_pack::enable_dht, open && settings.dht);
    pack.set_bool(lt::settings_pack::enable_lsd, open && settings.localDiscovery);
    pack.set_bool(lt::settings_pack::enable_upnp, open && settings.portMapping);
    pack.set_bool(lt::settings_pack::enable_natpmp, open && settings.portMapping);
    pack.set_bool(lt::settings_pack::enable_outgoing_utp, settings.utp);
    pack.set_bool(lt::settings_pack::enable_incoming_utp, settings.utp);
    pack.set_int(lt::settings_pack::connections_limit, settings.connectionsLimit);
    pack.set_int(lt::settings_pack::download_rate_limit, settings.downloadLimit);
    pack.set_int(lt::settings_pack::upload_rate_limit, settings.uploadLimit);
    // We pick pieces ourselves; a "finished" torrent must keep its peers,
    // since the next window may need them a second later.
    pack.set_bool(lt::settings_pack::close_redundant_connections, false);
    pack.set_bool(lt::settings_pack::allow_multiple_connections_per_ip, true);
    pack.set_int(lt::settings_pack::alert_mask,
                 lt::alert_category::status | lt::alert_category::error | lt::alert_category::storage
                     | lt::alert_category::piece_progress
                     | (qEnvironmentVariableIsSet("CJ_TORRENT_LOG")
                            ? lt::alert_category::peer | lt::alert_category::connect | lt::alert_category::tracker
                            : lt::alert_category_t{}));
    // Ask for blocks quickly when a deadline is close.
    pack.set_int(lt::settings_pack::request_timeout, 10);
    // Keep about one second of requests queued at each peer (default three):
    // after a seek, new requests wait behind fewer old ones.
    pack.set_int(lt::settings_pack::request_queue_time, 1);
    pack.set_int(lt::settings_pack::tick_interval, 100);
    pack.set_int(lt::settings_pack::piece_timeout, 5);
    pack.set_bool(lt::settings_pack::strict_end_game_mode, false);
    // Closing the app waits for trackers to hear goodbye; keep that short.
    pack.set_int(lt::settings_pack::stop_tracker_timeout, 1);

    lt::session_params params(std::move(pack));
    m_session = std::make_unique<lt::session>(std::move(params));
    m_thread = std::thread([this] { alertLoop(); });
    qCInfo(lcBroker).noquote() << "Torrent engine started, cache" << settings.cacheDir;
}

Engine::~Engine() { shutdown(); }

void Engine::shutdown()
{
    if (m_shutdownStarted.exchange(true))
        return;
    // Save every torrent's resume data while the alert thread still runs to
    // write it, so the next start skips re-checking the files.
    {
        std::vector<std::shared_ptr<Torrent>> torrents;
        {
            std::lock_guard lock(m_mutex);
            for (auto& [id, t] : m_torrents)
                torrents.push_back(t);
        }
        for (auto& t : torrents)
            saveResume(*t);
        const auto until = Clock::now() + kResumeSaveWait;
        while (m_pendingSaves > 0 && Clock::now() < until)
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    closing = true;
    if (m_thread.joinable())
        m_thread.join();
    {
        std::lock_guard lock(m_mutex);
        for (auto& [id, t] : m_torrents) {
            std::lock_guard tl(t->mutex);
            t->removed = true;
            t->cv.notify_all();
        }
    }
    // Ends the session in the background; destroying the proxy waits for
    // trackers to hear goodbye, at most a few seconds.
    if (m_session) {
        lt::session_proxy proxy = m_session->abort();
        m_session.reset();
    }
}

std::shared_ptr<Torrent> Engine::find(const lt::torrent_handle& h)
{
    if (!h.is_valid())
        return {};
    std::lock_guard lock(m_mutex);
    auto it = m_torrents.find(idOf(h.info_hashes()));
    return it == m_torrents.end() ? nullptr : it->second;
}

std::shared_ptr<Torrent> Engine::addOrFind(const OpenRequest& req, BrokerError* error)
{
    lt::add_torrent_params atp;
    lt::error_code ec;
    const QString uri = req.uri.trimmed();
    static const QRegularExpression hex40(QStringLiteral("^[0-9a-fA-F]{40}$"));
    if (uri.startsWith(QLatin1String("magnet:"), Qt::CaseInsensitive)) {
        atp = lt::parse_magnet_uri(uri.toStdString(), ec);
    } else if (hex40.match(uri).hasMatch()) {
        atp = lt::parse_magnet_uri("magnet:?xt=urn:btih:" + uri.toStdString(), ec);
    } else if (QFileInfo(uri).isFile()) {
        auto ti = std::make_shared<lt::torrent_info>(QFile::encodeName(uri).toStdString(), ec);
        if (!ec) {
            atp.ti = ti;
            atp.info_hashes = ti->info_hashes();
        }
    } else {
        ec = lt::errors::make_error_code(lt::errors::unsupported_url_protocol);
    }
    if (ec) {
        qCWarning(lcBroker).noquote() << "Cannot open" << uri << ":" << QString::fromStdString(ec.message());
        *error = BrokerError::InvalidUri;
        return nullptr;
    }

    const std::string id = idOf(atp.info_hashes);
    std::lock_guard lock(m_mutex);
    if (auto it = m_torrents.find(id); it != m_torrents.end()) {
        auto& t = it->second;
        try {
            // Peers and trackers from a newer magnet link still help.
            for (const auto& peer : atp.peers)
                t->handle.connect_peer(peer);
            for (const auto& tracker : atp.trackers)
                t->handle.add_tracker(lt::announce_entry(tracker));
            // An earlier error (a full disk, say) may be gone: try again.
            std::lock_guard tl(t->mutex);
            if (t->failed) {
                qCInfo(lcBroker).noquote() << "Retrying" << QString::fromStdString(id) << "after an error";
                t->failed = false;
                t->error.clear();
                t->handle.clear_error();
                t->handle.resume();
            }
        } catch (const std::exception& e) {
            qCWarning(lcBroker) << "Reopening a torrent failed:" << e.what();
        }
        return t;
    }

    auto t = std::make_shared<Torrent>();
    t->id = id;
    t->dir = QDir(settings.cacheDir).filePath(QString::fromStdString(id));
    QDir().mkpath(t->dir);
    t->touch();

    // Resume data from an earlier run: the file list and which pieces are
    // verified, so nothing waits for peers or a full re-check. The new magnet
    // link's trackers and peers are kept.
    {
        QFile resume(QDir(t->dir).filePath(QString::fromLatin1(kResumeFile)));
        if (resume.open(QIODevice::ReadOnly)) {
            const QByteArray data = resume.readAll();
            lt::error_code resumeError;
            lt::add_torrent_params rp = lt::read_resume_data({data.constData(), int(data.size())}, resumeError);
            if (!resumeError && idOf(rp.info_hashes) == id) {
                for (auto& tracker : atp.trackers)
                    rp.trackers.push_back(tracker);
                for (auto& peer : atp.peers)
                    rp.peers.push_back(peer);
                if (!rp.ti && atp.ti)
                    rp.ti = atp.ti;
                atp = std::move(rp);
                qCInfo(lcBroker).noquote() << "Resuming" << QString::fromStdString(id) << "from saved data";
            }
        }
    }

    // A file list saved on an earlier run means no wait for metadata.
    const QString saved = QDir(t->dir).filePath(QString::fromLatin1(kMetadataFile));
    if (!atp.ti && QFileInfo::exists(saved)) {
        lt::error_code loadError;
        auto ti = std::make_shared<lt::torrent_info>(QFile::encodeName(saved).toStdString(), loadError);
        if (!loadError && ti->info_hashes() == atp.info_hashes)
            atp.ti = ti;
    }

    atp.save_path = QFile::encodeName(t->dir).toStdString();
    atp.flags &= ~(lt::torrent_flags::auto_managed | lt::torrent_flags::paused);
    if (atp.ti) {
        // Nothing is wanted until a reader asks for it.
        atp.piece_priorities.assign(std::size_t(atp.ti->num_pieces()), lt::dont_download);
    }
    t->handle = m_session->add_torrent(std::move(atp), ec);
    if (ec) {
        qCWarning(lcBroker).noquote() << "Cannot add torrent:" << QString::fromStdString(ec.message());
        *error = BrokerError::TorrentError;
        return nullptr;
    }
    qCInfo(lcBroker).noquote() << "Added torrent" << QString::fromStdString(id);
    m_torrents.emplace(id, t);

    // With a .torrent file or a saved file list, metadata is already here.
    if (auto ti = t->handle.torrent_file()) {
        std::lock_guard tl(t->mutex);
        onMetadata(*t);
    }
    return t;
}

void Engine::onMetadata(Torrent& t)
{
    if (t.hasMetadata)
        return;
    t.info = t.handle.torrent_file();
    if (!t.info)
        return;
    t.hasMetadata = true;
    t.have.assign(std::size_t(t.info->num_pieces()), false);
    const lt::torrent_status st = t.handle.status(lt::torrent_handle::query_pieces);
    for (int i = 0; i < int(st.pieces.size()) && i < int(t.have.size()); ++i)
        t.have[i] = st.pieces.get_bit(lt::piece_index_t(i));
    t.handle.prioritize_pieces(
        std::vector<lt::download_priority_t>(std::size_t(t.info->num_pieces()), lt::dont_download));

    const QString saved = QDir(t.dir).filePath(QString::fromLatin1(kMetadataFile));
    if (!QFileInfo::exists(saved)) {
        lt::create_torrent ct(*t.info);
        std::vector<char> buffer;
        lt::bencode(std::back_inserter(buffer), ct.generate());
        QFile f(saved);
        if (f.open(QIODevice::WriteOnly))
            f.write(buffer.data(), qint64(buffer.size()));
    }
    qCInfo(lcBroker).noquote() << "Metadata for" << QString::fromStdString(t.id) << ":"
                               << t.info->num_files() << "files," << t.info->num_pieces() << "pieces of"
                               << t.info->piece_length() / 1024 << "KiB";
    t.cv.notify_all();
}

void Engine::alertLoop()
{
    auto lastTick = Clock::now();
    auto lastCacheCheck = Clock::now();
    std::vector<lt::alert*> alerts;
    while (!closing) {
        m_session->wait_for_alert(std::chrono::milliseconds(200));
        m_session->pop_alerts(&alerts);
        for (lt::alert* a : alerts) {
            // A libtorrent call on a torrent removed a moment ago throws; that
            // must never take the whole app down.
            try {
                handleAlert(a);
            } catch (const std::exception& e) {
                qCWarning(lcBroker) << "Handling a torrent event failed:" << e.what();
            }
        }

        if (Clock::now() - lastTick >= std::chrono::seconds(1)) {
            lastTick = Clock::now();
            std::vector<std::shared_ptr<Torrent>> torrents;
            {
                std::lock_guard lock(m_mutex);
                for (auto& [id, t] : m_torrents)
                    torrents.push_back(t);
            }
            for (auto& t : torrents) {
                try {
                    refresh(*t);
                } catch (const std::exception& e) {
                    qCDebug(lcBroker) << "Refreshing a torrent failed:" << e.what();
                }
            }
        }
        // The cap is also checked while playing: one big torrent can grow
        // past it, and the disk can fill up from other programs.
        if (Clock::now() - lastCacheCheck >= kCacheCheckEvery) {
            lastCacheCheck = Clock::now();
            enforceCacheCap();
        }
    }
}

// Once a second: the download rate, deadlines (they are relative to now), and
// a new search for peers when a torrent someone is reading has none.
void Engine::refresh(Torrent& t)
{
    {
        std::lock_guard tl(t.mutex);
        if (t.removed || !t.handle.is_valid())
            return;
    }
    const lt::torrent_status st = t.handle.status({});
    std::lock_guard tl(t.mutex);
    if (t.removed)
        return;
    t.downloadRate = st.download_payload_rate;
    const auto now = Clock::now();
    if (st.num_peers > 0 || t.openHandles == 0) {
        t.peersSeenAt = now;
    } else if (now - t.peersSeenAt >= kReannounceAfter) {
        qCInfo(lcBroker).noquote() << "No peers for" << QString::fromStdString(t.id) << ": asking again";
        t.handle.force_reannounce();
        t.handle.force_dht_announce();
        t.peersSeenAt = now;
    }
    schedule(t, true);
}

void Engine::handleAlert(lt::alert* a)
{
    if (m_debugAlerts)
        qCDebug(lcBroker).noquote() << "alert:" << QString::fromStdString(a->message());
    if (auto* pf = lt::alert_cast<lt::piece_finished_alert>(a)) {
        if (auto t = find(pf->handle)) {
            std::lock_guard tl(t->mutex);
            const int p = int(static_cast<int>(pf->piece_index));
            if (p >= 0 && p < int(t->have.size()))
                t->have[p] = true;
            t->scheduled.erase(p);
            t->lastPieceAt = Clock::now();
            t->cv.notify_all();
        }
    } else if (auto* rp = lt::alert_cast<lt::read_piece_alert>(a)) {
        if (auto t = find(rp->handle)) {
            std::lock_guard tl(t->mutex);
            const int p = static_cast<int>(rp->piece);
            t->readsPending.erase(p);
            if (rp->error) {
                ++t->readErrors[p];
                qCWarning(lcBroker).noquote() << "Reading piece" << p << "failed:"
                                              << QString::fromStdString(rp->error.message());
            } else {
                t->readErrors.erase(p);
                if (auto old = t->pieces.find(p); old != t->pieces.end())
                    t->pieceBytes -= old->second.second;
                t->pieces[p] = {rp->buffer, rp->size};
                t->pieceBytes += rp->size;
                t->pieceOrder.remove(p);
                t->pieceOrder.push_back(p);
                // Big pieces (16 MB and more in 4K torrents) would make a count
                // limit cost hundreds of MB, so the limit is in bytes. The two
                // newest pieces always stay: the one being read and the next.
                while (t->pieceBytes > kPieceCacheBytes && t->pieceOrder.size() > 2) {
                    const int oldest = t->pieceOrder.front();
                    t->pieceOrder.pop_front();
                    if (auto it = t->pieces.find(oldest); it != t->pieces.end()) {
                        t->pieceBytes -= it->second.second;
                        t->pieces.erase(it);
                    }
                }
            }
            t->cv.notify_all();
        }
    } else if (auto* md = lt::alert_cast<lt::metadata_received_alert>(a)) {
        if (auto t = find(md->handle)) {
            std::lock_guard tl(t->mutex);
            onMetadata(*t);
        }
    } else if (auto* tc = lt::alert_cast<lt::torrent_checked_alert>(a)) {
        // Data from an earlier run has been verified: refresh what we have.
        if (auto t = find(tc->handle)) {
            std::lock_guard tl(t->mutex);
            if (t->hasMetadata) {
                const lt::torrent_status st = t->handle.status(lt::torrent_handle::query_pieces);
                for (int i = 0; i < int(st.pieces.size()) && i < int(t->have.size()); ++i)
                    t->have[i] = t->have[i] || st.pieces.get_bit(lt::piece_index_t(i));
                t->cv.notify_all();
            } else {
                onMetadata(*t);
            }
        }
    } else if (auto* rd = lt::alert_cast<lt::save_resume_data_alert>(a)) {
        if (auto t = find(rd->handle); t && QFileInfo(t->dir).isDir()) {
            const std::vector<char> buffer = lt::write_resume_data_buf(rd->params);
            QSaveFile f(QDir(t->dir).filePath(QString::fromLatin1(kResumeFile)));
            if (f.open(QIODevice::WriteOnly)) {
                f.write(buffer.data(), qint64(buffer.size()));
                f.commit();
            }
        }
        --m_pendingSaves;
    } else if (lt::alert_cast<lt::save_resume_data_failed_alert>(a)) {
        --m_pendingSaves;
    } else if (auto* td = lt::alert_cast<lt::torrent_deleted_alert>(a)) {
        // libtorrent has deleted the data files: remove what is left.
        QString dir;
        {
            std::lock_guard lock(m_mutex);
            dir = m_pendingDeletes.take(QString::fromStdString(idOf(td->info_hashes)));
        }
        if (!dir.isEmpty())
            QDir(dir).removeRecursively();
    } else if (auto* te = lt::alert_cast<lt::torrent_error_alert>(a)) {
        if (auto t = find(te->handle)) {
            std::lock_guard tl(t->mutex);
            t->failed = true;
            t->error = te->error.message();
            qCWarning(lcBroker).noquote() << "Torrent error:" << QString::fromStdString(t->error);
            t->cv.notify_all();
        }
    }
}

void Engine::schedule(Torrent& t, bool force)
{
    if (t.removed || !t.hasMetadata || !t.handle.is_valid())
        return;

    const qint64 pieceLen = t.pieceLength();
    std::map<int, int> desired;  // piece -> deadline ms
    auto want = [&](int p, int ms) {
        if (p < 0 || p >= int(t.have.size()) || t.have[p])
            return;
        auto it = desired.find(p);
        if (it == desired.end() || ms < it->second)
            desired[p] = ms;
    };

    bool anyChange = force;
    t.readers.erase(std::remove_if(t.readers.begin(), t.readers.end(),
                                   [](const std::weak_ptr<Reader>& w) {
                                       auto r = w.lock();
                                       return !r || r->closed || r->cancelled;
                                   }),
                    t.readers.end());
    for (const auto& weak : t.readers) {
        auto r = weak.lock();
        const qint64 pos = r->fileOffset + qMin(r->position.load(), qMax<qint64>(0, r->fileSize - 1));
        const int first = t.pieceAt(pos);
        if (first != r->scheduledPiece) {
            r->scheduledPiece = first;
            anyChange = true;
        }
        const double bitrate = r->bitrate();
        const qint64 window = readAheadBytes(bitrate, double(t.downloadRate), pieceLen);
        const qint64 fileEnd = r->fileOffset + r->fileSize - 1;
        const int last = t.pieceAt(qMin(pos + window, fileEnd));
        // Each piece is due when the player will reach it at the file's bitrate.
        for (int p = first; p <= last; ++p)
            want(p, int(double(qint64(p - first) * pieceLen) * 1000.0 / bitrate));

        // The start and the end of the file hold the container's headers and
        // index; players read both before the first frame.
        for (int p = t.pieceAt(r->fileOffset); p <= t.pieceAt(qMin(r->fileOffset + kHeadBytes, fileEnd)); ++p)
            want(p, 0);
        for (int p = t.pieceAt(qMax(r->fileOffset, fileEnd - kTailBytes)); p <= t.pieceAt(fileEnd); ++p)
            want(p, 100);
    }
    if (!anyChange)
        return;

    try {
        for (const auto& [p, ms] : desired) {
            const lt::piece_index_t index(p);
            if (!t.scheduled.count(p))
                t.handle.piece_priority(index, lt::top_priority);
            t.handle.set_piece_deadline(index, ms);
        }
        for (const auto& [p, ms] : t.scheduled) {
            if (!desired.count(p) && !t.have[p]) {
                t.handle.reset_piece_deadline(lt::piece_index_t(p));
                t.handle.piece_priority(lt::piece_index_t(p), lt::dont_download);
            }
        }
    } catch (const std::exception& e) {
        // The torrent went away under us (removed from the cache).
        qCDebug(lcBroker) << "Scheduling failed:" << e.what();
        return;
    }
    t.scheduled = std::move(desired);
}

void Engine::saveResume(Torrent& t)
{
    {
        std::lock_guard tl(t.mutex);
        if (t.removed || !t.hasMetadata)
            return;
    }
    try {
        ++m_pendingSaves;
        t.handle.save_resume_data(lt::torrent_handle::save_info_dict);
    } catch (const std::exception&) {
        --m_pendingSaves;  // removed a moment ago: no alert will come
    }
}

void Engine::releaseHandle(Torrent& t)
{
    bool last = false;
    {
        std::lock_guard tl(t.mutex);
        if (t.openHandles > 0)
            --t.openHandles;
        last = t.openHandles == 0;
        schedule(t, true);
    }
    t.touch();
    if (last)
        saveResume(t);
    enforceCacheCap();
}

void Engine::enforceCacheCap(const std::string& keep)
{
    if (closing)
        return;
    struct Entry { QString dir; std::string id; qint64 size; qint64 lastUsed; };
    std::vector<Entry> entries;
    qint64 total = 0;

    // Measuring walks every file, so do it before taking the lock that the
    // alert thread and every open need.
    const QFileInfoList dirs = QDir(settings.cacheDir).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QFileInfo& d : dirs) {
        const qint64 size = StreamBroker::diskUsage(d.absoluteFilePath());
        total += size;
        QFile f(QDir(d.absoluteFilePath()).filePath(QString::fromLatin1(kLastUsedFile)));
        qint64 used = d.lastModified().toMSecsSinceEpoch();
        if (f.open(QIODevice::ReadOnly))
            used = f.readAll().toLongLong();
        entries.push_back({d.absoluteFilePath(), d.fileName().toStdString(), size, used});
    }

    // Never fill the disk: the cap shrinks to what fits while leaving a reserve.
    qint64 cap = settings.cacheCapBytes;
    const QStorageInfo disk(settings.cacheDir);
    if (disk.isValid() && disk.bytesAvailable() >= 0)
        cap = qMin(cap, qMax<qint64>(0, total + disk.bytesAvailable() - kDiskReserve));
    if (total <= cap)
        return;
    std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) { return a.lastUsed < b.lastUsed; });

    std::lock_guard lock(m_mutex);
    for (const Entry& e : entries) {
        if (total <= cap)
            break;
        if (e.id == keep)
            continue;  // the torrent being opened right now
        if (auto it = m_torrents.find(e.id); it != m_torrents.end()) {
            auto t = it->second;
            {
                std::lock_guard tl(t->mutex);
                if (t->openHandles > 0)
                    continue;  // never delete what someone is watching
                t->removed = true;
                t->cv.notify_all();
            }
            m_pendingDeletes.insert(QString::fromStdString(e.id), e.dir);
            m_session->remove_torrent(t->handle, lt::session::delete_files);
            m_torrents.erase(it);
        } else {
            // Not loaded this run: nothing has its files open.
            QDir(e.dir).removeRecursively();
        }
        // A loaded torrent's folder goes once libtorrent has deleted its files
        // (see torrent_deleted_alert).
        total -= e.size;
        qCInfo(lcBroker).noquote() << "Cache over its cap: removed" << QString::fromStdString(e.id)
                                   << "(" << e.size / (1024 * 1024) << "MiB)";
    }
}

StreamStats Engine::stats(Torrent& t, const Reader& r)
{
    StreamStats s;
    lt::torrent_status st;
    try {
        if (t.handle.is_valid())
            st = t.handle.status({});
    } catch (const std::exception&) {
        // Removed a moment ago: report no peers.
    }
    s.peers = st.num_peers;
    s.seeds = st.num_seeds;
    s.downloadRate = st.download_payload_rate;
    s.uploadRate = st.upload_payload_rate;

    std::lock_guard tl(t.mutex);
    if (!t.hasMetadata) {
        s.state = s.peers > 0 ? StreamState::FetchingMetadata : StreamState::FindingPeers;
        return s;
    }
    s.bufferedBytes = t.bufferedFrom(r, r.position);
    s.bufferedSeconds = double(s.bufferedBytes) / r.bitrate();
    s.state = s.bufferedBytes > 0 || r.position >= r.fileSize ? StreamState::Ready : StreamState::Buffering;
    if (s.state == StreamState::Buffering) {
        if (Clock::now() - t.lastPieceAt >= kStallAfter)
            s.state = StreamState::Stalled;
        // How far along the piece the reader waits for is.
        const int piece = t.pieceAt(r.fileOffset + qMin<qint64>(r.position, qMax<qint64>(0, r.fileSize - 1)));
        try {
            for (const lt::partial_piece_info& pp : t.handle.get_download_queue()) {
                if (static_cast<int>(pp.piece_index) == piece && pp.blocks_in_piece > 0) {
                    s.waitingPercent = qBound(0, (pp.finished + pp.writing) * 100 / pp.blocks_in_piece, 99);
                    break;
                }
            }
        } catch (const std::exception&) {
        }
    }
    return s;
}

StreamStats Engine::torrentStats(const std::string& id)
{
    std::shared_ptr<Torrent> t;
    {
        std::lock_guard lock(m_mutex);
        auto it = m_torrents.find(id);
        if (it == m_torrents.end())
            return {};
        t = it->second;
    }
    StreamStats s;
    try {
        if (t->handle.is_valid()) {
            const lt::torrent_status st = t->handle.status({});
            s.peers = st.num_peers;
            s.seeds = st.num_seeds;
            s.downloadRate = st.download_payload_rate;
            s.uploadRate = st.upload_payload_rate;
        }
    } catch (const std::exception&) {
    }
    std::lock_guard tl(t->mutex);
    s.state = t->hasMetadata ? StreamState::Buffering
            : s.peers > 0    ? StreamState::FetchingMetadata
                             : StreamState::FindingPeers;
    return s;
}

} // namespace broker_detail

using namespace broker_detail;

// --- StreamBroker ------------------------------------------------------------

StreamBroker::StreamBroker(const BrokerSettings& settings)
    : m_engine(std::make_shared<Engine>(settings))
{
}

StreamBroker::~StreamBroker() { m_engine->shutdown(); }

int StreamBroker::listenPort() const { return m_engine->listenPort(); }

StreamStats StreamBroker::torrentStats(const QString& infoHash) const
{
    return m_engine->torrentStats(infoHash.toLower().toStdString());
}
void StreamBroker::enforceCacheCap() { m_engine->enforceCacheCap(); }

std::shared_ptr<StreamHandle> StreamBroker::open(const OpenRequest& request, BrokerError* error,
                                                 const std::function<bool()>& cancelled)
{
    BrokerError dummy;
    if (!error)
        error = &dummy;
    *error = BrokerError::None;
    if (m_engine->closing) {
        *error = BrokerError::Closed;
        return nullptr;
    }

    std::shared_ptr<Torrent> t = m_engine->addOrFind(request, error);
    if (!t)
        return nullptr;
    // After adding, so the torrent being opened counts as just used and is
    // never the one deleted to make room.
    m_engine->enforceCacheCap(t->id);

    std::unique_lock lock(t->mutex);
    const auto deadline = Clock::now() + std::chrono::milliseconds(m_engine->settings.metadataTimeoutMs);
    while (!t->hasMetadata) {
        if (t->failed) { *error = BrokerError::TorrentError; return nullptr; }
        if (t->removed || m_engine->closing) { *error = BrokerError::Closed; return nullptr; }
        if (cancelled && cancelled()) { *error = BrokerError::Cancelled; return nullptr; }
        if (Clock::now() >= deadline) {
            qCWarning(lcBroker).noquote() << "No metadata for" << QString::fromStdString(t->id) << "in"
                                          << m_engine->settings.metadataTimeoutMs << "ms";
            *error = BrokerError::MetadataTimeout;
            return nullptr;
        }
        t->cv.wait_for(lock, std::chrono::milliseconds(100));
    }

    const lt::file_storage& files = t->info->files();
    int index = request.fileIndex;
    if (index < 0) {
        qint64 biggest = -1;
        for (int i = 0; i < files.num_files(); ++i) {
            const qint64 size = files.file_size(lt::file_index_t(i));
            if (size > biggest) {
                biggest = size;
                index = i;
            }
        }
    }
    if (index < 0 || index >= files.num_files()) {
        *error = BrokerError::NoSuchFile;
        return nullptr;
    }

    auto reader = std::make_shared<Reader>();
    reader->fileIndex = index;
    reader->fileOffset = files.file_offset(lt::file_index_t(index));
    reader->fileSize = files.file_size(lt::file_index_t(index));
    reader->fileName = QString::fromStdString(std::string(files.file_name(lt::file_index_t(index))));
    reader->duration = request.durationSeconds;
    t->readers.push_back(reader);
    ++t->openHandles;
    t->lastPieceAt = Clock::now();  // a fresh wait, not a stall
    t->peersSeenAt = Clock::now();
    m_engine->schedule(*t, true);
    lock.unlock();

    qCInfo(lcBroker).noquote() << "Opened" << reader->fileName << "(" << reader->fileSize / (1024 * 1024)
                               << "MiB ) from" << QString::fromStdString(t->id);
    std::shared_ptr<StreamHandle> handle(new StreamHandle);
    handle->m_engine = m_engine;
    handle->m_torrent = t;
    handle->m_reader = reader;
    return handle;
}

QString StreamBroker::infoHashOfFile(const QString& torrentFile, QString* name)
{
    lt::error_code ec;
    lt::torrent_info ti(QFile::encodeName(torrentFile).toStdString(), ec);
    if (ec)
        return {};
    if (name)
        *name = QString::fromStdString(ti.name());
    return QString::fromStdString(idOf(ti.info_hashes()));
}

qint64 StreamBroker::diskUsage(const QString& path)
{
    qint64 total = 0;
    auto sizeOf = [](const QString& file) -> qint64 {
#ifdef Q_OS_WIN
        DWORD high = 0;
        const DWORD low = GetCompressedFileSizeW(reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(file).utf16()), &high);
        if (low == INVALID_FILE_SIZE && GetLastError() != NO_ERROR)
            return QFileInfo(file).size();
        return (qint64(high) << 32) | low;
#else
        struct stat st;
        if (::stat(QFile::encodeName(file).constData(), &st) != 0)
            return 0;
        return qint64(st.st_blocks) * 512;
#endif
    };
    if (QFileInfo(path).isFile())
        return sizeOf(path);
    QDirIterator it(path, QDir::Files | QDir::Hidden | QDir::System, QDirIterator::Subdirectories);
    while (it.hasNext())
        total += sizeOf(it.next());
    return total;
}

// --- StreamHandle ------------------------------------------------------------

StreamHandle::~StreamHandle() { close(); }

qint64 StreamHandle::size() const { return m_reader->fileSize; }
int StreamHandle::fileIndex() const { return m_reader->fileIndex; }
QString StreamHandle::fileName() const { return m_reader->fileName; }
QString StreamHandle::infoHash() const { return QString::fromStdString(m_torrent->id); }

void StreamHandle::setDuration(double seconds)
{
    if (seconds > 0)
        m_reader->duration = seconds;
}

void StreamHandle::cancel()
{
    m_reader->cancelled = true;
    std::lock_guard lock(m_torrent->mutex);
    m_torrent->cv.notify_all();
}

void StreamHandle::close()
{
    if (!m_reader || m_reader->closed.exchange(true))
        return;
    {
        std::lock_guard lock(m_torrent->mutex);
        m_torrent->cv.notify_all();
    }
    if (!m_engine->closing)
        m_engine->releaseHandle(*m_torrent);
}

qint64 StreamHandle::read(qint64 offset, char* buffer, qint64 length)
{
    Reader& r = *m_reader;
    Torrent& t = *m_torrent;
    if (r.cancelled || r.closed)
        return -1;
    if (offset < 0 || length < 0)
        return -2;
    if (offset >= r.fileSize || length == 0)
        return 0;

    const qint64 absolute = r.fileOffset + offset;
    std::unique_lock lock(t.mutex);
    const int piece = t.pieceAt(absolute);
    const qint64 pieceStart = qint64(piece) * t.pieceLength();
    const qint64 inPiece = absolute - pieceStart;
    const qint64 count = std::min({length, r.fileSize - offset, qint64(t.info->piece_size(lt::piece_index_t(piece))) - inPiece});

    r.position = offset;
    m_engine->schedule(t, false);

    auto stopped = [&] { return r.cancelled || r.closed || t.removed || m_engine->closing.load(); };
    // 1. Wait until the piece is downloaded and verified.
    while (!t.have[piece]) {
        if (stopped())
            return -1;
        if (t.failed)
            return -2;
        t.cv.wait_for(lock, std::chrono::milliseconds(250));
    }
    // 2. Get its bytes from libtorrent (or the in-memory copy).
    while (!t.pieces.count(piece)) {
        if (stopped())
            return -1;
        if (t.readErrors[piece] > 3)
            return -2;
        if (!t.readsPending.count(piece)) {
            try {
                t.handle.read_piece(lt::piece_index_t(piece));
                t.readsPending.insert(piece);
            } catch (const std::exception&) {
                return t.removed ? -1 : -2;
            }
        }
        t.cv.wait_for(lock, std::chrono::milliseconds(250));
    }
    const auto& [data, size] = t.pieces[piece];
    const qint64 n = qMin(count, qint64(size) - inPiece);
    std::memcpy(buffer, data.get() + inPiece, std::size_t(qMax<qint64>(0, n)));
    t.pieceOrder.remove(piece);
    t.pieceOrder.push_back(piece);

    // Readers usually go on to the next piece: start reading it now.
    const int next = piece + 1;
    if (next < int(t.have.size()) && t.have[next] && !t.pieces.count(next) && !t.readsPending.count(next)) {
        try {
            t.handle.read_piece(lt::piece_index_t(next));
            t.readsPending.insert(next);
        } catch (const std::exception&) {
        }
    }
    r.position = offset + n;
    return n;
}

StreamStats StreamHandle::stats() const { return m_engine->stats(*m_torrent, *m_reader); }

} // namespace cj
