// Keeping torrents healthy over time: reloads, resume data, releasing
// handles, the disk cache cap, and the numbers the UI shows.

#include "BrokerEngine.h"

namespace cj {
namespace broker_detail {

void Engine::reload(const std::shared_ptr<Torrent>& tp)
{
    Torrent& t = *tp;
    std::lock_guard tl(t.mutex);
    t.reloadWanted = false;
    t.reloadedAt = Clock::now();
    if (t.removed || !t.hasMetadata || !t.handle.is_valid() || closing)
        return;

    const int n = t.info->num_pieces();
    lt::add_torrent_params atp;
    atp.ti = std::make_shared<lt::torrent_info>(*t.info);
    atp.have_pieces.resize(n, false);
    std::vector<bool> keep(std::size_t(n), false);
    if (auto store = m_memory->find(t.id)) {
        for (int p : store->pieces()) {
            if (p < n && t.have[std::size_t(p)]) {
                atp.have_pieces.set_bit(lt::piece_index_t(p));
                keep[std::size_t(p)] = true;
            }
        }
        store->dropAllExcept(keep);  // half-downloaded pieces start over
    }
    try {
        // Peers we connected to listen where we reached them; peers that
        // connected to us came from a port of their own that will not answer.
        atp.peers = t.linkPeers;
        std::vector<lt::peer_info> peers;
        t.handle.get_peer_info(peers);
        for (const auto& peer : peers) {
#if LIBTORRENT_VERSION_NUM >= 20100
            const lt::tcp::endpoint where = peer.remote_endpoint();
#else
            const lt::tcp::endpoint where = peer.ip;
#endif
            if ((peer.flags & lt::peer_info::outgoing_connection)
                && std::find(atp.peers.begin(), atp.peers.end(), where) == atp.peers.end())
                atp.peers.push_back(where);
        }
        for (const auto& tracker : t.handle.trackers()) {
            atp.trackers.push_back(tracker.url);
            atp.tracker_tiers.push_back(tracker.tier);
        }
    } catch (const std::exception&) {
    }
    atp.save_path = QFile::encodeName(t.dir).toStdString();
    atp.flags &= ~(lt::torrent_flags::auto_managed | lt::torrent_flags::paused);
    // In order: see Engine::schedule.
    atp.flags |= lt::torrent_flags::sequential_download;
    atp.piece_priorities.assign(std::size_t(n), lt::dont_download);

    const std::size_t refetch = t.evicted.size();
    m_session->remove_torrent(t.handle);
    lt::error_code ec;
    lt::torrent_handle fresh = m_session->add_torrent(std::move(atp), ec);
    if (ec) {
        qCWarning(lcBroker).noquote() << "Reloading" << QString::fromStdString(t.id)
                                      << "failed:" << QString::fromStdString(ec.message());
        t.failed = true;
        t.error = ec.message();
        t.cv.notify_all();
        return;
    }
    t.handle = fresh;
    t.evicted.clear();
    t.scheduled.clear();
    t.wanted.clear();
    t.readsPending.clear();
    t.readErrors.clear();
    t.peersSeenAt = Clock::now();
    for (const auto& weak : t.readers)
        if (auto r = weak.lock())
            r->scheduledPiece = -1;
    schedule(t, true);
    t.cv.notify_all();
    qCInfo(lcBroker).noquote() << "Reloaded" << QString::fromStdString(t.id) << "to download" << refetch
                               << "dropped pieces again";
}

void Engine::saveResume(Torrent& t)
{
    if (memoryOnly())
        return;  // nothing is kept for a later run
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
        if (!t.inUse())
            t.idleSince = Clock::now();
        schedule(t, true);
    }
    t.touch();
    if (last)
        saveResume(t);
    enforceCacheCap();
}

void Engine::enforceCacheCap(const std::string& keep, qint64 capOverride)
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
    qint64 cap = capOverride >= 0 ? capOverride : settings.cacheCapBytes;
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
                if (t->inUse())
                    continue;  // never delete what someone is watching or opening
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
        if (const lt::torrent_handle h = t.currentHandle(); h.is_valid())
            st = h.status({});
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
        // How far along the piece the reader waits for is.
        const int piece = t.pieceAt(r.fileOffset + qMin<qint64>(r.position, qMax<qint64>(0, r.fileSize - 1)));
        int blocks = 0;
        int blocksInPiece = 0;
        try {
            for (const lt::partial_piece_info& pp : t.handle.get_download_queue()) {
                if (static_cast<int>(pp.piece_index) == piece && pp.blocks_in_piece > 0) {
                    blocks = pp.finished + pp.writing;
                    blocksInPiece = pp.blocks_in_piece;
                    s.waitingPercent = qBound(0, blocks * 100 / pp.blocks_in_piece, 99);
                    break;
                }
            }
        } catch (const std::exception&) {
        }
        // Stalled means the awaited piece got no new data for a while. Data
        // arriving for other pieces does not count: that is slow, not stuck.
        const auto now = Clock::now();
        if (piece != r.waitPiece || blocks > r.waitBlocks) {
            r.waitPiece = piece;
            r.waitBlocks = blocks;
            r.waitProgressAt = now;
            r.stallLogged = false;
        }
        if (now - r.waitProgressAt >= kStallAfter) {
            s.state = StreamState::Stalled;
            if (!r.stallLogged) {
                r.stallLogged = true;
                // Why: does anyone have the piece, and is it being fetched?
                int holders = -1;
                try {
                    std::vector<int> avail;
                    t.handle.piece_availability(avail);
                    if (piece < int(avail.size()))
                        holders = avail[std::size_t(piece)];
                } catch (const std::exception&) {
                }
                qCInfo(lcBroker).noquote()
                    << "Stalled on piece" << piece << "of" << QString::fromStdString(t.id) << ":" << blocks << "/"
                    << blocksInPiece << "blocks," << holders << "peers have it," << s.peers << "peers connected,"
                    << s.downloadRate / 1024 << "KiB/s overall";
            }
        }
    } else {
        r.waitPiece = -1;
    }
    return s;
}

qint64 Engine::readCacheBytes()
{
    std::vector<std::shared_ptr<Torrent>> torrents;
    {
        std::lock_guard lock(m_mutex);
        for (auto& [id, t] : m_torrents)
            torrents.push_back(t);
    }
    qint64 total = 0;
    for (auto& t : torrents) {
        std::lock_guard tl(t->mutex);
        total += t->pieceBytes;
    }
    return total;
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
        if (const lt::torrent_handle h = t->currentHandle(); h.is_valid()) {
            const lt::torrent_status st = h.status({});
            s.peers = st.num_peers;
            s.seeds = st.num_seeds;
            s.downloadRate = st.download_payload_rate;
            s.uploadRate = st.upload_payload_rate;
        }
    } catch (const std::exception&) {
    }
    std::lock_guard tl(t->mutex);
    s.paused = t->paused;
    s.state = t->hasMetadata ? StreamState::Buffering
            : s.peers > 0    ? StreamState::FetchingMetadata
                             : StreamState::FindingPeers;
    return s;
}

} // namespace broker_detail
} // namespace cj
