#include "TorrentTransport.h"

#include "Logging.h"
#include "StreamBroker.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QUrlQuery>

#include <mpv/client.h>
#include <mpv/stream_cb.h>

namespace cj {

namespace {

// The file a torrent:// URL names, or -1 for "the largest".
int fileIndexOf(const QUrl& url)
{
    bool ok = false;
    const int index = url.path().mid(1).toInt(&ok);
    return ok ? index : -1;
}

const QRegularExpression kHex40(QStringLiteral("^[0-9a-fA-F]{40}$"));
const QRegularExpression kBase32(QStringLiteral("^[A-Za-z2-7]{32}$"));

// Old magnet links give the info hash in base32.
QString base32ToHex(const QString& text)
{
    QByteArray bytes;
    quint64 buffer = 0;
    int bits = 0;
    for (QChar c : text.toUpper()) {
        const int v = c.isLetter() ? c.unicode() - 'A' : c.unicode() - '2' + 26;
        buffer = (buffer << 5) | quint64(v);
        bits += 5;
        if (bits >= 8) {
            bits -= 8;
            bytes.append(char((buffer >> bits) & 0xff));
        }
    }
    return QString::fromLatin1(bytes.toHex());
}

struct Magnet
{
    QString hash;  // lower-case hex, empty if none
    QString name;
    int fileIndex = -1;  // "so" (select only, BEP 53): which file to play
};

Magnet parseMagnet(const QString& uri)
{
    Magnet m;
    const QUrlQuery query(uri.mid(uri.indexOf(QLatin1Char('?')) + 1));
    for (const auto& [key, value] : query.queryItems(QUrl::FullyDecoded)) {
        if (key == QLatin1String("xt") && value.startsWith(QLatin1String("urn:btih:"), Qt::CaseInsensitive)) {
            const QString h = value.mid(9);
            if (kHex40.match(h).hasMatch())
                m.hash = h.toLower();
            else if (kBase32.match(h).hasMatch())
                m.hash = base32ToHex(h);
        } else if (key == QLatin1String("dn")) {
            m.name = QString(value).replace(QLatin1Char('+'), QLatin1Char(' '));
        } else if (key == QLatin1String("so")) {
            bool ok = false;
            const int index = value.section(QLatin1Char(','), 0, 0).section(QLatin1Char('-'), 0, 0).toInt(&ok);
            if (ok && index >= 0)
                m.fileIndex = index;
        }
    }
    return m;
}

QUrl streamUrl(const QString& hash, int fileIndex)
{
    QUrl url;
    url.setScheme(QStringLiteral("torrent"));
    url.setHost(hash);
    if (fileIndex >= 0)
        url.setPath(QStringLiteral("/%1").arg(fileIndex));
    return url;
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
    if (m_broker)
        m_broker->applyNetwork(m_settings);
}

StreamBroker& TorrentTransport::broker()
{
    std::lock_guard lock(m_brokerMutex);
    if (!m_broker)
        m_broker = std::make_shared<StreamBroker>(m_settings);
    return *m_broker;
}

QString TorrentTransport::infoHashOf(const QUrl& url)
{
    if (url.scheme() != QLatin1String("torrent"))
        return {};
    const QString host = url.host().toLower();
    return kHex40.match(host).hasMatch() ? host : QString();
}

bool TorrentTransport::registerWith(mpv_handle* mpv)
{
    const int err = mpv_stream_cb_add_ro(mpv, "torrent", this, &TorrentTransport::openCallback);
    if (err < 0)
        qCCritical(lcTransports) << "Could not add the torrent protocol to mpv:" << mpv_error_string(err);
    return err >= 0;
}

std::optional<PlayableSource> TorrentTransport::resolve(const QString& input)
{
    const QString text = input.trimmed();
    QString hash, name;
    int fileIndex = -1;
    if (text.startsWith(QLatin1String("torrent://"), Qt::CaseInsensitive)) {
        const QUrl url(text);
        if (infoHashOf(url).isEmpty())
            return std::nullopt;
        return PlayableSource::fromUrl(url, url.host());
    }
    if (text.startsWith(QLatin1String("magnet:"), Qt::CaseInsensitive)) {
        const Magnet m = parseMagnet(text);
        if (m.hash.isEmpty()) {
            qCWarning(lcTransports) << "Magnet link without a BitTorrent info hash";
            return std::nullopt;
        }
        hash = m.hash;
        name = m.name;
        fileIndex = m.fileIndex;
    } else if (kHex40.match(text).hasMatch()) {
        hash = text.toLower();
    } else if (text.endsWith(QLatin1String(".torrent"), Qt::CaseInsensitive) && QFileInfo(text).isFile()) {
        hash = StreamBroker::infoHashOfFile(text, &name);
        if (hash.isEmpty()) {
            qCWarning(lcTransports).noquote() << "Not a valid .torrent file:" << text;
            return std::nullopt;
        }
    } else {
        return std::nullopt;
    }
    {
        std::lock_guard lock(m_mutex);
        m_requests.insert(hash, text);
    }
    qCInfo(lcTransports).noquote() << "Torrent" << hash << (name.isEmpty() ? QString() : "(" + name + ")");
    return PlayableSource::fromUrl(streamUrl(hash, fileIndex), name.isEmpty() ? hash.left(12) : name);
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

    auto* stream = new Stream{handle, 0, this, url};
    info->cookie = stream;
    info->read_fn = [](void* cookie, char* buf, uint64_t nbytes) -> int64_t {
        auto* s = static_cast<Stream*>(cookie);
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
        auto* s = static_cast<Stream*>(cookie);
        if (offset < 0)
            return MPV_ERROR_GENERIC;
        s->position = offset;  // the next read moves the download window
        return offset;
    };
    info->size_fn = [](void* cookie) -> int64_t { return static_cast<Stream*>(cookie)->handle->size(); };
    info->close_fn = [](void* cookie) { delete static_cast<Stream*>(cookie); };
    info->cancel_fn = [](void* cookie) { static_cast<Stream*>(cookie)->handle->cancel(); };
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
