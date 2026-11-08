// The broker's engine: the libtorrent session, adding torrents, metadata and
// the alert loop.

#include "BrokerEngine.h"

namespace cj {
namespace broker_detail {

Engine::Engine(const BrokerSettings& s)
    : settings(s)
{
    QDir().mkpath(settings.cacheDir);
    for (const QString& tracker : settings.extraTrackers)
        extraTrackers.push_back(tracker.toStdString());
    if (settings.extraTrackers.isEmpty() && !settings.loopbackOnly)
        extraTrackers.assign(std::begin(kPublicTrackers), std::end(kPublicTrackers));

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
    // Room for bursts of alerts (each finished piece is one); a dropped
    // alert costs a 2 s retry in read().
    pack.set_int(lt::settings_pack::alert_queue_size, 20000);
    // More peers, sooner: announce to every tracker (not just the first that
    // answers in each tier), and open new connections faster at the start.
    pack.set_bool(lt::settings_pack::announce_to_all_tiers, true);
    pack.set_bool(lt::settings_pack::announce_to_all_trackers, true);
    pack.set_int(lt::settings_pack::connection_speed, 100);
    pack.set_int(lt::settings_pack::torrent_connect_boost, 60);
    // Closing the app waits for trackers to hear goodbye; keep that short.
    pack.set_int(lt::settings_pack::stop_tracker_timeout, 1);

    lt::session_params params(std::move(pack));
    if (settings.memoryOnly) {
        m_memory = std::make_shared<MemoryRegistry>();
        params.disk_io_constructor = m_memory->constructor(m_memory);
    }
    m_session = std::make_unique<lt::session>(std::move(params));
    m_thread = std::thread([this] { alertLoop(); });
    if (m_memory)
        qCInfo(lcBroker).noquote() << "Torrent engine started, pieces in RAM (cap" << memoryCap() / (1024 * 1024)
                                   << "MiB), file lists in" << settings.cacheDir;
    else
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

void Engine::dropExtraTrackers(lt::add_torrent_params& atp) const
{
    const auto isExtra = [this](const std::string& url) {
        return std::find(extraTrackers.begin(), extraTrackers.end(), url) != extraTrackers.end()
            || std::find(std::begin(kPublicTrackers), std::end(kPublicTrackers), url) != std::end(kPublicTrackers);
    };
    std::vector<std::string> trackers;
    std::vector<int> tiers;
    for (std::size_t i = 0; i < atp.trackers.size(); ++i) {
        if (isExtra(atp.trackers[i]))
            continue;
        trackers.push_back(atp.trackers[i]);
        if (i < atp.tracker_tiers.size())
            tiers.push_back(atp.tracker_tiers[i]);
    }
    atp.trackers = std::move(trackers);
    atp.tracker_tiers = std::move(tiers);
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
        // Its own trackers come with it (a private torrent's only ones).
        try {
            atp = lt::load_torrent_file(QFile::encodeName(uri).toStdString());
        } catch (const lt::system_error& e) {
            ec = e.code();
        }
    } else {
        ec = lt::errors::make_error_code(lt::errors::unsupported_url_protocol);
    }
    if (ec) {
        qCWarning(lcBroker).noquote() << "Cannot open" << uri << ":" << QString::fromStdString(ec.message());
        *error = BrokerError::InvalidUri;
        return nullptr;
    }

    // Magnet links from add-ons often name few trackers; well-known open
    // trackers find more peers sooner. But a private torrent (BEP 27) may be
    // announced only to its own trackers, and an announce cannot be taken
    // back. So they are added only once the metadata says it is public:
    // here for a .torrent file, in onMetadata() for everything else (a
    // magnet link, cached metadata, resume data).
    if (atp.ti && !atp.ti->priv()) {
        for (const std::string& tracker : extraTrackers)
            if (std::find(atp.trackers.begin(), atp.trackers.end(), tracker) == atp.trackers.end())
                atp.trackers.push_back(tracker);
    }

    const std::string id = idOf(atp.info_hashes);
    std::lock_guard lock(m_mutex);
    if (auto it = m_torrents.find(id); it != m_torrents.end()) {
        auto& t = it->second;
        try {
            // Peers and trackers from a newer magnet link still help.
            const lt::torrent_handle h = t->currentHandle();
            for (const auto& peer : atp.peers)
                h.connect_peer(peer);
            {
                std::lock_guard tl(t->mutex);
                for (const auto& peer : atp.peers)
                    if (std::find(t->linkPeers.begin(), t->linkPeers.end(), peer) == t->linkPeers.end())
                        t->linkPeers.push_back(peer);
            }
            {
                std::lock_guard tl(t->mutex);
                // Known to be private: only its own trackers.
                if (t->info && t->info->priv())
                    dropExtraTrackers(atp);
                // Still waiting for metadata, and this request brings it (a
                // .torrent file of the same torrent): hand it over.
                if (!t->hasMetadata && atp.ti) {
                    const auto info = atp.ti->info_section();
                    h.set_metadata(lt::span<char const>(info.data(), info.size()));
                }
            }
            for (const auto& tracker : atp.trackers)
                h.add_tracker(lt::announce_entry(tracker));
            std::lock_guard tl(t->mutex);
            // Counted under m_mutex, so the cache cannot delete it from here on.
            ++t->pendingOpens;
            if (t->paused) {
                qCInfo(lcBroker).noquote() << "Resuming" << QString::fromStdString(id) << ": opened again";
                t->paused = false;
                t->handle.resume();
                // The peer list was cleared at the pause: find peers afresh.
                for (const auto& peer : t->linkPeers)
                    t->handle.connect_peer(peer);
                t->handle.force_reannounce();
                t->handle.force_dht_announce();
            }
            // An earlier error (a full disk, say) may be gone: try again.
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
    t->pendingOpens = 1;  // this open, from before the cache can see it
    t->linkPeers = atp.peers;
    t->dir = QDir(settings.cacheDir).filePath(QString::fromStdString(id));
    QDir().mkpath(t->dir);
    t->touch();

    // Resume data from an earlier run: the file list and which pieces are
    // verified, so nothing waits for peers or a full re-check. The new magnet
    // link's trackers and peers are kept.
    {
        // In RAM only nothing from an earlier run is held: no resume data.
        QFile resume(QDir(t->dir).filePath(QString::fromLatin1(kResumeFile)));
        if (!memoryOnly() && resume.open(QIODevice::ReadOnly)) {
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
                // Earlier versions saved the open trackers with torrents
                // whose privacy was not yet known. onMetadata() adds them
                // back once the torrent is known to be public.
                if (!rp.ti || rp.ti->priv())
                    dropExtraTrackers(rp);
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

// An open has become a reader, failed or been cancelled.
void Engine::endPendingOpen(Torrent& t)
{
    std::lock_guard tl(t.mutex);
    if (t.pendingOpens > 0)
        --t.pendingOpens;
    if (!t.inUse())
        t.idleSince = Clock::now();
}

// Once a second. Paused, a torrent stops uploading and keeps no peers; its
// cached bytes stay, and opening it again resumes it.
void Engine::pauseIfIdle(Torrent& t)
{
    std::lock_guard tl(t.mutex);
    if (t.removed || t.paused || t.inUse() || !t.handle.is_valid()
        || Clock::now() - t.idleSince < std::chrono::milliseconds(settings.idlePauseMs))
        return;
    t.handle.pause();
    // libtorrent waits a minute before it calls a peer it was just connected
    // to again; a cleared list lets a reopen reconnect at once.
    t.handle.clear_peers();
    t.paused = true;
    qCInfo(lcBroker).noquote() << "Paused" << QString::fromStdString(t.id) << ": nobody is using it";
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
    // Now it is known whether the open trackers may hear of it. libtorrent
    // stops its DHT and peer exchange for a private torrent by itself. The
    // private branch only cleans up after older versions: nothing adds them
    // before this point any more.
    if (t.info->priv()) {
        lt::add_torrent_params own;
        for (const lt::announce_entry& e : t.handle.trackers()) {
            own.trackers.push_back(e.url);
            own.tracker_tiers.push_back(e.tier);
        }
        const std::size_t before = own.trackers.size();
        dropExtraTrackers(own);
        if (own.trackers.size() != before) {
            std::vector<lt::announce_entry> keep;
            for (std::size_t i = 0; i < own.trackers.size(); ++i) {
                lt::announce_entry e(own.trackers[i]);
                e.tier = std::uint8_t(own.tracker_tiers[i]);
                keep.push_back(e);
            }
            t.handle.replace_trackers(keep);
            qCInfo(lcBroker).noquote() << QString::fromStdString(t.id) << "is private: open trackers removed";
        }
    } else {
        for (const std::string& tracker : extraTrackers)
            t.handle.add_tracker(lt::announce_entry(tracker));
    }

    const QString saved = QDir(t.dir).filePath(QString::fromLatin1(kMetadataFile));
    if (!QFileInfo::exists(saved)) {
        // A .torrent file is the info dictionary, verbatim, under "info".
        const auto info = t.info->info_section();
        QFile f(saved);
        if (f.open(QIODevice::WriteOnly)) {
            f.write("d4:info");
            f.write(info.data(), qint64(info.size()));
            f.write("e");
        }
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

        if (m_memory) {
            if (m_memory->totalBytes() > memoryCap())
                trimMemory();
            std::vector<std::shared_ptr<Torrent>> toReload;
            {
                std::lock_guard lock(m_mutex);
                for (auto& [id, t] : m_torrents) {
                    std::lock_guard tl(t->mutex);
                    if (t->reloadWanted && Clock::now() - t->reloadedAt >= kReloadEvery)
                        toReload.push_back(t);
                }
            }
            for (auto& t : toReload) {
                try {
                    reload(t);
                } catch (const std::exception& e) {
                    qCWarning(lcBroker) << "Reloading a torrent failed:" << e.what();
                }
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
                    pauseIfIdle(*t);
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
    lt::torrent_handle h;
    {
        std::lock_guard tl(t.mutex);
        if (t.removed || !t.handle.is_valid())
            return;
        h = t.handle;
    }
    const lt::torrent_status st = h.status({});
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
        for (const auto& peer : t.linkPeers)
            t.handle.connect_peer(peer);
        t.peersSeenAt = now;
    }
    schedule(t, true);
}

void Engine::handleAlert(lt::alert* a)
{
    if (m_debugAlerts)
        qCDebug(lcBroker).noquote() << "alert:" << QString::fromStdString(a->message());
    if (lt::alert_cast<lt::alerts_dropped_alert>(a)) {
        qCWarning(lcBroker) << "libtorrent dropped alerts; reads check pieces directly";
    } else if (auto* pf = lt::alert_cast<lt::piece_finished_alert>(a)) {
        if (auto t = find(pf->handle)) {
            std::lock_guard tl(t->mutex);
            // From a torrent replaced by a reload: the new one does not know
            // this piece, so neither may we.
            if (pf->handle != t->handle)
                return;
            const int p = int(static_cast<int>(pf->piece_index));
            if (p >= 0 && p < int(t->have.size()))
                t->have[p] = true;
            t->evicted.erase(p);
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

} // namespace broker_detail
} // namespace cj
