#include "TorrentTransport.h"

#include "Logging.h"
#include "StreamBroker.h"

#include <QDir>
#include <QFileInfo>

#include <mpv/client.h>
#include <mpv/stream_cb.h>

#include <mutex>
#include <unordered_map>

namespace cj {

namespace {

// The file a torrent:// URL names, or -1 for "the largest".
int fileIndexOf(const QUrl& url)
{
    bool ok = false;
    const int index = url.path().mid(1).toInt(&ok);
    return ok ? index : -1;
}

} // namespace

// What mpv holds for one open stream: the cookie in the callbacks below.
struct TorrentTransport::Stream
{
    std::shared_ptr<StreamHandle> handle;
    qint64 position = 0;
    TorrentTransport* transport = nullptr;
    QUrl url;
    bool errorReported = false;
};

namespace {

// Open streams by cookie number. Never destroyed: mpv may close a stream
// while the program exits.
struct StreamTable
{
    std::mutex mutex;
    std::uintptr_t next = 1;
    std::unordered_map<std::uintptr_t, TorrentTransport::Stream*> streams;
};

StreamTable& streamTable()
{
    static auto* table = new StreamTable;
    return *table;
}

std::uintptr_t addStream(TorrentTransport::Stream* stream)
{
    StreamTable& t = streamTable();
    std::lock_guard lock(t.mutex);
    const std::uintptr_t id = t.next++;
    t.streams.emplace(id, stream);
    return id;
}

// For read, seek and size, which mpv calls only between open and close.
TorrentTransport::Stream* streamOf(void* cookie)
{
    StreamTable& t = streamTable();
    std::lock_guard lock(t.mutex);
    return t.streams.at(reinterpret_cast<std::uintptr_t>(cookie));
}

std::shared_ptr<StreamHandle> handleOf(void* cookie)
{
    StreamTable& t = streamTable();
    std::lock_guard lock(t.mutex);
    const auto it = t.streams.find(reinterpret_cast<std::uintptr_t>(cookie));
    return it == t.streams.end() ? nullptr : it->second->handle;
}

TorrentTransport::Stream* takeStream(void* cookie)
{
    StreamTable& t = streamTable();
    std::lock_guard lock(t.mutex);
    const auto it = t.streams.find(reinterpret_cast<std::uintptr_t>(cookie));
    if (it == t.streams.end())
        return nullptr;
    TorrentTransport::Stream* s = it->second;
    t.streams.erase(it);
    return s;
}

} // namespace

TorrentTransport::TorrentTransport(const BrokerSettings& settings, QObject* parent)
    : QObject(parent)
    , m_settings(settings)
{
    qRegisterMetaType<cj::StreamStatus>();
    qRegisterMetaType<cj::PlaybackError>();
    m_statusThread = std::thread([this] { statusLoop(); });
}

TorrentTransport::~TorrentTransport()
{
    abort();
    if (m_statusThread.joinable())
        m_statusThread.join();
    // Unblocks any read still waiting; the handles then fail cleanly.
    std::lock_guard lock(m_brokerMutex);
    m_broker.reset();
}

qint64 TorrentTransport::cacheUsage() const
{
    return m_settings.cacheDir.isEmpty() ? 0 : StreamBroker::diskUsage(m_settings.cacheDir);
}

void TorrentTransport::clearCache()
{
    std::shared_ptr<StreamBroker> running;
    {
        std::lock_guard lock(m_brokerMutex);
        running = m_broker;
    }
    if (running) {
        running->clearCache();
        return;
    }
    // Not started this run: nothing has a file open.
    if (m_settings.cacheDir.isEmpty())
        return;
    for (const QFileInfo& d : QDir(m_settings.cacheDir).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot))
        QDir(d.absoluteFilePath()).removeRecursively();
    qCInfo(lcTransports).noquote() << "Torrent cache cleared:" << m_settings.cacheDir;
}

void TorrentTransport::setNetwork(const BrokerSettings& s)
{
    std::lock_guard lock(m_brokerMutex);
    m_settings.dht = s.dht;
    m_settings.localDiscovery = s.localDiscovery;
    m_settings.portMapping = s.portMapping;
    m_settings.publicTrackers = s.publicTrackers;
    m_settings.upload = s.upload;
    m_settings.torrents = s.torrents;
    if (m_broker)
        m_broker->applyNetwork(m_settings);
}

bool TorrentTransport::engineStarted()
{
    std::lock_guard lock(m_brokerMutex);
    return m_broker != nullptr;
}

StreamBroker& TorrentTransport::broker()
{
    std::lock_guard lock(m_brokerMutex);
    if (!m_broker)
        m_broker = std::make_shared<StreamBroker>(m_settings);
    return *m_broker;
}

bool TorrentTransport::registerWith(mpv_handle* mpv)
{
    const int err = mpv_stream_cb_add_ro(mpv, "torrent", this, &TorrentTransport::openCallback);
    if (err < 0)
        qCCritical(lcTransports) << "Could not add the torrent protocol to mpv:" << mpv_error_string(err);
    return err >= 0;
}

void TorrentTransport::setCurrent(const QUrl& url)
{
    const QString hash = infoHashOf(url);
    std::vector<std::shared_ptr<StreamHandle>> others;
    {
        std::lock_guard lock(m_mutex);
        // Another file of the same torrent (the next episode of a season
        // pack) is a new item too: its own URL, duration and reports.
        if (hash == m_currentHash && (hash.isEmpty() || url == m_currentUrl))
            return;
        m_currentHash = hash;
        m_currentUrl = hash.isEmpty() ? QUrl() : url;
        m_duration = 0;
        ++m_generation;
        for (const auto& weak : m_handles)
            if (auto h = weak.lock(); h && h->infoHash() != hash)
                others.push_back(h);
    }
    // A read for the old item may be waiting on pieces that will never come;
    // let it go now rather than when mpv gets round to closing it.
    for (auto& h : others)
        h->cancel();
    m_statusWake.notify_all();
}

// m_mutex must be held.
bool TorrentTransport::isCurrent(const StreamHandle& h) const
{
    const int file = fileIndexOf(m_currentUrl);
    return h.infoHash() == m_currentHash && (file < 0 || h.fileIndex() == file);
}

void TorrentTransport::abort()
{
    {
        std::lock_guard lock(m_mutex);
        m_closing = true;
    }
    m_statusWake.notify_all();
    std::vector<std::shared_ptr<StreamHandle>> handles;
    {
        std::lock_guard lock(m_mutex);
        for (const auto& weak : m_handles)
            if (auto h = weak.lock())
                handles.push_back(h);
    }
    for (auto& h : handles)
        h->cancel();
}

void TorrentTransport::setDuration(double seconds)
{
    if (seconds <= 0)
        return;
    std::lock_guard lock(m_mutex);
    m_duration = seconds;
    for (const auto& weak : m_handles)
        if (auto h = weak.lock(); h && isCurrent(*h))
            h->setDuration(seconds);
}

// --- mpv's side: these run on mpv's threads. They never call libmpv. ---------

int TorrentTransport::openCallback(void* self, char* uri, mpv_stream_cb_info* info)
{
    return static_cast<TorrentTransport*>(self)->open(uri, info);
}

int TorrentTransport::open(const char* uri, mpv_stream_cb_info* info)
{
    const QUrl url(QString::fromUtf8(uri));
    const QString hash = infoHashOf(url);
    if (hash.isEmpty()) {
        qCWarning(lcTransports) << "Not a torrent stream URL:" << uri;
        return MPV_ERROR_LOADING_FAILED;
    }
    bool ok = true;
    const QString path = url.path().mid(1);
    const int fileIndex = path.isEmpty() ? -1 : path.toInt(&ok);
    if (!ok)
        return MPV_ERROR_LOADING_FAILED;
    bool allowed = false;
    {
        std::lock_guard lock(m_brokerMutex);
        allowed = m_settings.torrents;
    }
    if (!allowed) {
        // Before broker(): the engine must not start, and no peer is called.
        qCInfo(lcTransports) << "Torrent playback is off; not opening" << hash;
        QUrl current;
        {
            std::lock_guard lock(m_mutex);
            if (hash == m_currentHash)
                current = m_currentUrl;
        }
        if (!current.isEmpty())
            emit openFailed(PlaybackError::TorrentsOff, tr("Torrent playback is off."), current);
        return MPV_ERROR_LOADING_FAILED;
    }

    OpenRequest request;
    quint64 generation = 0;
    {
        std::lock_guard lock(m_mutex);
        request.uri = m_requests.value(hash, hash);
        request.fileIndex = fileIndex;
        if (hash == m_currentHash)
            request.durationSeconds = m_duration;
        ++m_opening[hash];
        generation = m_generation;
    }
    // Waiting for the file list can take a while. Give up once the user has
    // moved to another item or the app is closing.
    auto cancelled = [this, hash, generation] {
        if (m_closing)
            return true;
        if (m_generation == generation)
            return false;
        std::lock_guard lock(m_mutex);
        return m_currentHash != hash;
    };
    BrokerError error = BrokerError::None;
    std::shared_ptr<StreamHandle> handle = m_closing ? nullptr : broker().open(request, &error, cancelled);
    {
        std::lock_guard lock(m_mutex);
        if (--m_opening[hash] <= 0)
            m_opening.remove(hash);
        if (handle)
            m_handles.push_back(handle);
        m_handles.erase(std::remove_if(m_handles.begin(), m_handles.end(),
                                       [](const std::weak_ptr<StreamHandle>& w) { return w.expired(); }),
                        m_handles.end());
    }
    if (!handle) {
        qCWarning(lcTransports) << "Opening the torrent failed:" << int(error);
        QUrl current;
        {
            std::lock_guard lock(m_mutex);
            // Only for the file being played: an earlier episode of the
            // same torrent failing is old news.
            if (hash == m_currentHash && (fileIndexOf(m_currentUrl) < 0 || fileIndexOf(m_currentUrl) == fileIndex))
                current = m_currentUrl;
        }
        // Cancelled or closed means the user moved on: nothing to show.
        if (!current.isEmpty() && error == BrokerError::MetadataTimeout)
            emit openFailed(PlaybackError::NoPeers, tr("Nobody sharing this torrent was found in time."), current);
        else if (!current.isEmpty() && error == BrokerError::TorrentError)
            emit openFailed(PlaybackError::StreamFailed, tr("The torrent stopped with an error."), current);
        return error == BrokerError::NoSuchFile || error == BrokerError::InvalidUri ? MPV_ERROR_UNKNOWN_FORMAT
                                                                                    : MPV_ERROR_LOADING_FAILED;
    }

    // The cookie is a number, not the Stream: mpv can call cancel_fn after
    // close_fn. Its stream_cb frees the cancel hook only after close_fn
    // returns, and the core thread can trigger it in between when the next
    // file starts. A pointer would be freed by then (the macOS crash in
    // StreamHandle::cancel); a number that is gone from the table is ignored.
    info->cookie = reinterpret_cast<void*>(addStream(new Stream{handle, 0, this, url}));
    info->read_fn = [](void* cookie, char* buf, uint64_t nbytes) -> int64_t {
        Stream* s = streamOf(cookie);
        const qint64 n = s->handle->read(s->position, buf, qint64(nbytes));
        if (n == -2 && !s->errorReported) {
            // A torrent error mid-play (a full disk): mpv would just see the
            // stream end, so say why.
            s->errorReported = true;
            emit s->transport->openFailed(PlaybackError::StreamFailed,
                                          TorrentTransport::tr("The torrent stopped with an error."), s->url);
        }
        if (n < 0)
            return -1;  // cancelled, closed or a torrent error
        s->position += n;
        return n;
    };
    info->seek_fn = [](void* cookie, int64_t offset) -> int64_t {
        Stream* s = streamOf(cookie);
        if (offset < 0)
            return MPV_ERROR_GENERIC;
        s->position = offset;  // the next read moves the download window
        return offset;
    };
    info->size_fn = [](void* cookie) -> int64_t { return streamOf(cookie)->handle->size(); };
    info->close_fn = [](void* cookie) { delete takeStream(cookie); };
    info->cancel_fn = [](void* cookie) {
        if (const std::shared_ptr<StreamHandle> h = handleOf(cookie))
            h->cancel();
    };
    qCInfo(lcTransports).noquote() << "Streaming" << handle->fileName() << "to mpv";
    return 0;
}

// --- Status for the UI -----------------------------------------------------------

void TorrentTransport::statusLoop()
{
    std::unique_lock lock(m_mutex);
    while (!m_closing) {
        m_statusWake.wait_for(lock, std::chrono::milliseconds(500));
        if (m_closing)
            break;
        lock.unlock();
        pollStatus();
        lock.lock();
    }
}

// On the status thread: libtorrent's status calls wait on its network thread,
// which must never stall the UI.
void TorrentTransport::pollStatus()
{
    QString hash;
    QUrl url;
    std::shared_ptr<StreamHandle> handle;
    bool opening = false;
    {
        std::lock_guard lock(m_mutex);
        hash = m_currentHash;
        url = m_currentUrl;
        opening = m_opening.contains(hash);
        // The newest open handle on the current file is the one playing.
        // While another file of the same torrent opens, there is none yet:
        // the torrent's own figures are shown.
        for (auto it = m_handles.rbegin(); it != m_handles.rend() && !handle; ++it)
            if (auto h = it->lock(); h && isCurrent(*h))
                handle = h;
    }
    if (hash.isEmpty() || (!handle && !opening))
        return;

    const StreamStats st = handle ? handle->stats() : broker().torrentStats(hash);
    StreamStatus status;
    status.peers = st.peers;
    status.downloadRate = st.downloadRate;
    switch (st.state) {
    case StreamState::FindingPeers:     status.phase = StreamPhase::FindingPeers; break;
    case StreamState::FetchingMetadata: status.phase = StreamPhase::FetchingMetadata; break;
    case StreamState::Buffering:        status.phase = StreamPhase::Buffering; break;
    case StreamState::Stalled:          status.phase = StreamPhase::Stalled; break;
    case StreamState::Ready:            status.phase = StreamPhase::Ready; break;
    }
    status.percent = st.waitingPercent;
    status.bufferedSeconds = st.bufferedSeconds;
    status.source = url;
    emit statusChanged(status);
}

} // namespace cj
