#include "TorrentDownloader.h"

#include "BrokerEngine.h"
#include "Logging.h"
#include "UploadGuard.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

#include <libtorrent/load_torrent.hpp>
#include <libtorrent/magnet_uri.hpp>
#include <libtorrent/session.hpp>
#include <libtorrent/session_params.hpp>
#include <libtorrent/torrent_info.hpp>
#include <libtorrent/torrent_status.hpp>

#include <chrono>
#include <condition_variable>
#include <map>
#include <mutex>
#include <thread>

namespace cj {
namespace downloader_detail {

using broker_detail::idOf;
using broker_detail::kPublicTrackers;
using broker_detail::UploadGuard;
using Clock = std::chrono::steady_clock;

namespace {

bool parse(const QString& uri, lt::add_torrent_params& atp)
{
    lt::error_code ec;
    static const QRegularExpression hex40(QStringLiteral("^[0-9a-fA-F]{40}$"));
    if (uri.startsWith(QLatin1String("magnet:"), Qt::CaseInsensitive)) {
        atp = lt::parse_magnet_uri(uri.toStdString(), ec);
    } else if (hex40.match(uri).hasMatch()) {
        atp = lt::parse_magnet_uri("magnet:?xt=urn:btih:" + uri.toStdString(), ec);
    } else if (QFileInfo(uri).isFile()) {
        try {
            atp = lt::load_torrent_file(QFile::encodeName(uri).toStdString());
        } catch (const lt::system_error& e) {
            ec = e.code();
        }
    } else {
        ec = lt::errors::make_error_code(lt::errors::unsupported_url_protocol);
    }
    return !ec;
}

} // namespace

struct Item
{
    int id = 0;
    std::string hash;   // lower-case hex; also UploadGuard's key (userdata)
    lt::torrent_handle handle;
    bool inSession = false;
    int fileIndex = -1;
    bool filesChosen = false;
    QString folder;
    Clock::time_point added;
    DownloadStatus status;
};

class Engine
{
public:
    explicit Engine(const BrokerSettings& s) : m_settings(s) {}
    ~Engine() { shutdown(); }

    void shutdown()
    {
        {
            std::lock_guard lock(m_mutex);
            if (m_closing)
                return;
            m_closing = true;
        }
        m_wake.notify_all();
        if (m_thread.joinable())
            m_thread.join();
        std::lock_guard lock(m_mutex);
        if (m_session) {
            lt::session_proxy proxy = m_session->abort();
            m_session.reset();
        }
    }

    int add(const QString& uri, int fileIndex, const QString& folder, bool share, BrokerError* error)
    {
        lt::add_torrent_params atp;
        if (!parse(uri.trimmed(), atp)) {
            *error = BrokerError::InvalidUri;
            return 0;
        }
        if (!QDir().mkpath(folder)) {
            *error = BrokerError::TorrentError;
            return 0;
        }
        std::lock_guard lock(m_mutex);
        if (m_closing) {
            *error = BrokerError::Closed;
            return 0;
        }
        start();
        const std::string hash = idOf(atp.info_hashes);
        for (const auto& [id, item] : m_items) {
            if (item->hash == hash && item->inSession) {
                *error = BrokerError::TorrentError;
                return 0;
            }
        }
        auto item = std::make_shared<Item>();
        item->id = m_nextId++;
        item->hash = hash;
        item->fileIndex = fileIndex;
        item->folder = folder;
        item->added = Clock::now();
        item->status.phase = DownloadStatus::Phase::FindingPeers;
        item->status.sharing = share;
        m_guard->setSharing(hash, share);

        // Open trackers only once the metadata says the torrent is public,
        // as the stream broker does (a private one must not be announced
        // anywhere else).
        if (atp.ti && !atp.ti->priv() && m_settings.publicTrackers)
            addOpenTrackers(atp);
        atp.save_path = QFile::encodeName(folder).toStdString();
        atp.userdata = &item->hash;
        atp.flags &= ~(lt::torrent_flags::auto_managed | lt::torrent_flags::paused);
        // Files start at priority 0 when the file list comes; the chosen one
        // is turned on then (so the others are never created).
        atp.flags |= lt::torrent_flags::default_dont_download;
        lt::error_code ec;
        item->handle = m_session->add_torrent(std::move(atp), ec);
        if (ec) {
            qCWarning(lcBroker).noquote() << "Cannot add a download:" << QString::fromStdString(ec.message());
            *error = BrokerError::TorrentError;
            return 0;
        }
        item->inSession = true;
        m_items.emplace(item->id, item);
        qCInfo(lcBroker) << "Download" << item->id << "added to the download engine";
        *error = BrokerError::None;
        m_wake.notify_all();
        return item->id;
    }

    DownloadStatus status(int id) const
    {
        std::lock_guard lock(m_mutex);
        const auto it = m_items.find(id);
        return it == m_items.end() ? DownloadStatus{} : it->second->status;
    }

    void setSharing(int id, bool on)
    {
        std::lock_guard lock(m_mutex);
        const auto it = m_items.find(id);
        if (it == m_items.end())
            return;
        Item& item = *it->second;
        item.status.sharing = on;
        m_guard->setSharing(item.hash, on);
        // Finished and no longer shared: nothing left to do in the session.
        if (!on && item.status.phase == DownloadStatus::Phase::Finished)
            leave(item, false);
    }

    void remove(int id, bool deleteFiles)
    {
        std::lock_guard lock(m_mutex);
        const auto it = m_items.find(id);
        if (it == m_items.end())
            return;
        leave(*it->second, deleteFiles);
        m_items.erase(it);
    }

    void applyNetwork(const BrokerSettings& s)
    {
        std::lock_guard lock(m_mutex);
        m_settings.dht = s.dht;
        m_settings.localDiscovery = s.localDiscovery;
        m_settings.portMapping = s.portMapping;
        m_settings.publicTrackers = s.publicTrackers;
        if (!m_session || m_settings.loopbackOnly)
            return;
        lt::settings_pack pack;
        pack.set_bool(lt::settings_pack::enable_dht, s.dht);
        pack.set_bool(lt::settings_pack::enable_lsd, s.localDiscovery);
        pack.set_bool(lt::settings_pack::enable_upnp, s.portMapping);
        pack.set_bool(lt::settings_pack::enable_natpmp, s.portMapping);
        m_session->apply_settings(std::move(pack));
    }

private:
    // m_mutex held.
    void start()
    {
        if (m_session)
            return;
        lt::settings_pack pack;
        pack.set_str(lt::settings_pack::user_agent, "CorporalJackie/0.1 libtorrent/" LIBTORRENT_VERSION);
        const std::string port = std::to_string(m_settings.listenPort);
        pack.set_str(lt::settings_pack::listen_interfaces,
                     m_settings.loopbackOnly ? "127.0.0.1:" + port : "0.0.0.0:" + port + ",[::]:" + port);
        const bool open = !m_settings.loopbackOnly;
        pack.set_bool(lt::settings_pack::enable_dht, open && m_settings.dht);
        pack.set_bool(lt::settings_pack::enable_lsd, open && m_settings.localDiscovery);
        pack.set_bool(lt::settings_pack::enable_upnp, open && m_settings.portMapping);
        pack.set_bool(lt::settings_pack::enable_natpmp, open && m_settings.portMapping);
        pack.set_bool(lt::settings_pack::enable_outgoing_utp, false);
        pack.set_bool(lt::settings_pack::enable_incoming_utp, m_settings.utp);
        pack.set_int(lt::settings_pack::connections_limit, m_settings.connectionsLimit);
        pack.set_int(lt::settings_pack::download_rate_limit, m_settings.downloadLimit);
        pack.set_int(lt::settings_pack::upload_rate_limit, m_settings.uploadLimit);
        // Unchoke slots are always there; the guard lets only the downloads
        // the person shares send anything.
        UploadGuard::apply(pack, true);
        pack.set_bool(lt::settings_pack::allow_multiple_connections_per_ip, true);
        pack.set_bool(lt::settings_pack::announce_to_all_tiers, true);
        pack.set_bool(lt::settings_pack::announce_to_all_trackers, true);
        pack.set_int(lt::settings_pack::stop_tracker_timeout, 1);
        pack.set_int(lt::settings_pack::alert_mask, lt::alert_category::error);
        lt::session_params params(std::move(pack));
        m_guard = std::make_shared<UploadGuard>(false);
        params.extensions.push_back(m_guard);
        m_session = std::make_unique<lt::session>(std::move(params));
        m_thread = std::thread([this] { pollLoop(); });
        qCInfo(lcBroker) << "Download engine started";
    }

    void addOpenTrackers(lt::add_torrent_params& atp) const
    {
        std::vector<std::string> extra;
        for (const QString& t : m_settings.extraTrackers)
            extra.push_back(t.toStdString());
        if (extra.empty() && !m_settings.loopbackOnly)
            extra.assign(std::begin(kPublicTrackers), std::end(kPublicTrackers));
        for (const std::string& t : extra)
            if (std::find(atp.trackers.begin(), atp.trackers.end(), t) == atp.trackers.end())
                atp.trackers.push_back(t);
    }

    // m_mutex held. Takes the torrent out of the session; its file stays
    // unless `deleteFiles`.
    void leave(Item& item, bool deleteFiles)
    {
        if (!item.inSession)
            return;
        item.inSession = false;
        item.status.seeding = false;
        item.status.uploadRate = 0;
        item.status.downloadRate = 0;
        try {
            m_session->remove_torrent(item.handle, deleteFiles ? lt::session::delete_files : lt::remove_flags_t{});
        } catch (const std::exception&) {
        }
        m_guard->forget(item.hash);
        // Bits of pieces that cross into files nobody wanted: libtorrent
        // keeps them in a hidden ".<hash>.parts" file. Gone once the torrent
        // has let go of it.
        m_partFiles.push_back({QDir(item.folder).filePath(QStringLiteral(".%1.parts").arg(QString::fromStdString(item.hash))), 0});
    }

    void pollLoop()
    {
        std::unique_lock lock(m_mutex);
        while (!m_closing) {
            m_wake.wait_for(lock, std::chrono::milliseconds(500));
            if (m_closing)
                break;
            std::vector<std::shared_ptr<Item>> items;
            for (auto& [id, item] : m_items)
                if (item->inSession)
                    items.push_back(item);
            lock.unlock();
            for (auto& item : items)
                poll(item);
            lock.lock();
            for (auto& item : items)
                settle(*item);
            for (auto it = m_partFiles.begin(); it != m_partFiles.end();) {
                if (!QFileInfo::exists(it->first) || QFile::remove(it->first) || ++it->second > 20)
                    it = m_partFiles.erase(it);
                else
                    ++it;
            }
        }
    }

    // Without the lock: libtorrent's calls wait on its network thread.
    void poll(const std::shared_ptr<Item>& item)
    {
        DownloadStatus s;
        lt::torrent_handle h;
        bool chosen = false;
        int fileIndex = -1;
        {
            std::lock_guard lock(m_mutex);
            s = item->status;
            h = item->handle;
            chosen = item->filesChosen;
            fileIndex = item->fileIndex;
        }
        try {
            const lt::torrent_status st = h.status({});
            s.peers = st.num_peers;
            s.downloadRate = st.download_payload_rate;
            s.uploadRate = st.upload_payload_rate;
            s.uploadedBytes = st.all_time_upload;
            if (st.errc) {
                s.phase = DownloadStatus::Phase::Failed;
                s.error = QString::fromStdString(st.errc.message());
            } else if (!st.has_metadata) {
                // No time limit: like any torrent program, it keeps looking
                // until someone has it or the person cancels.
                s.phase = st.num_peers > 0 ? DownloadStatus::Phase::FetchingMetadata : DownloadStatus::Phase::FindingPeers;
            } else {
                const std::shared_ptr<const lt::torrent_info> ti = h.torrent_file();
#if LIBTORRENT_VERSION_NUM >= 20100
                const lt::file_storage& files = ti->layout();
#else
                const lt::file_storage& files = ti->files();
#endif
                if (!chosen) {
                    if (fileIndex < 0) {
                        std::int64_t best = -1;
                        for (const lt::file_index_t f : files.file_range())
                            if (!files.pad_file_at(f) && files.file_size(f) > best) {
                                best = files.file_size(f);
                                fileIndex = int(static_cast<int>(f));
                            }
                    }
                    if (fileIndex < 0 || fileIndex >= files.num_files()) {
                        s.phase = DownloadStatus::Phase::Failed;
                        s.error = QStringLiteral("no such file");
                    } else {
                        std::vector<lt::download_priority_t> prio(std::size_t(files.num_files()), lt::dont_download);
                        prio[std::size_t(fileIndex)] = lt::default_priority;
                        h.prioritize_files(prio);
                        if (!ti->priv() && m_settings.publicTrackers) {
                            lt::add_torrent_params extra;
                            addOpenTrackers(extra);
                            for (const std::string& t : extra.trackers)
                                h.add_tracker(lt::announce_entry(t));
                        }
                        chosen = true;
                    }
                }
                if (chosen) {
                    const lt::file_index_t f(fileIndex);
                    s.name = QString::fromStdString(std::string(files.file_name(f)));
                    s.path = QDir(item->folder).filePath(QString::fromStdString(files.file_path(f)));
                    s.sizeBytes = files.file_size(f);
                    std::vector<std::int64_t> progress;
                    h.file_progress(progress, lt::torrent_handle::piece_granularity);
                    s.doneBytes = std::size_t(fileIndex) < progress.size() ? progress[std::size_t(fileIndex)] : 0;
                    if (s.doneBytes >= s.sizeBytes) {
                        s.phase = DownloadStatus::Phase::Finished;
                        s.seeding = s.sharing;
                        s.downloadRate = 0;
                    } else {
                        s.phase = DownloadStatus::Phase::Downloading;
                    }
                }
            }
        } catch (const std::exception&) {
            return;   // removed meanwhile
        }
        std::lock_guard lock(m_mutex);
        if (!item->inSession)
            return;
        s.sharing = item->status.sharing;   // may have changed meanwhile
        s.seeding = s.phase == DownloadStatus::Phase::Finished && s.sharing;
        item->status = s;
        item->filesChosen = chosen;
        item->fileIndex = fileIndex;
    }

    // m_mutex held: a finished download nobody shares, or a failed one,
    // leaves the session.
    void settle(Item& item)
    {
        if (!item.inSession)
            return;
        if (item.status.phase == DownloadStatus::Phase::Failed) {
            qCInfo(lcBroker) << "Download" << item.id << "failed";
            leave(item, true);
        } else if (item.status.phase == DownloadStatus::Phase::Finished && !item.status.sharing) {
            qCInfo(lcBroker) << "Download" << item.id << "finished";
            leave(item, false);
        }
    }

    BrokerSettings m_settings;
    mutable std::mutex m_mutex;
    std::condition_variable m_wake;
    bool m_closing = false;
    std::unique_ptr<lt::session> m_session;
    std::shared_ptr<UploadGuard> m_guard;
    std::thread m_thread;
    std::map<int, std::shared_ptr<Item>> m_items;
    std::vector<std::pair<QString, int>> m_partFiles;   // path, tries
    int m_nextId = 1;
};

} // namespace downloader_detail

TorrentDownloader::TorrentDownloader(const BrokerSettings& settings)
    : m_engine(std::make_shared<downloader_detail::Engine>(settings))
{
}

TorrentDownloader::~TorrentDownloader() { m_engine->shutdown(); }

int TorrentDownloader::add(const QString& uri, int fileIndex, const QString& folder, bool share, BrokerError* error)
{
    BrokerError e = BrokerError::None;
    const int id = m_engine->add(uri, fileIndex, folder, share, &e);
    if (error)
        *error = e;
    return id;
}

DownloadStatus TorrentDownloader::status(int id) const { return m_engine->status(id); }
void TorrentDownloader::setSharing(int id, bool on) { m_engine->setSharing(id, on); }
void TorrentDownloader::remove(int id, bool deleteFiles) { m_engine->remove(id, deleteFiles); }
void TorrentDownloader::applyNetwork(const BrokerSettings& settings) { m_engine->applyNetwork(settings); }

} // namespace cj
