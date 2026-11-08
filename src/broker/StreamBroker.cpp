#include "BrokerEngine.h"

namespace cj {

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

void StreamBroker::clearCache() { m_engine->enforceCacheCap({}, 0); }
bool StreamBroker::memoryOnly() const { return m_engine->memoryOnly(); }
qint64 StreamBroker::memoryUsed() const { return m_engine->memoryUsed(); }
qint64 StreamBroker::readCacheBytes() const { return m_engine->readCacheBytes(); }

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
    // addOrFind() counted this open as a use; it ends however this returns.
    struct PendingOpen
    {
        broker_detail::Engine& engine;
        Torrent& torrent;
        ~PendingOpen() { engine.endPendingOpen(torrent); }
    } pending{*m_engine, *t};
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

    // A torrent reopened from the cache first reads its resume data, in the
    // background. Until then libtorrent reports none of its pieces, and the
    // reader would wait for (or ask peers for) pieces already on disk. It
    // takes a moment; wait for it, at most 3 s.
    for (int i = 0; i < 60; ++i) {
        lt::torrent_status st;
        try {
            st = t->handle.status(lt::torrent_handle::query_pieces);
        } catch (const std::exception&) {
            break;
        }
        if (st.state != lt::torrent_status::checking_resume_data) {
            if (i > 0 || st.num_pieces > 0)
                qCDebug(lcBroker).noquote() << "Resume data of" << QString::fromStdString(t->id) << "read:"
                                            << st.num_pieces << "pieces, state" << int(st.state);
            for (int p = 0; p < int(st.pieces.size()) && p < int(t->have.size()); ++p)
                if (st.pieces.get_bit(lt::piece_index_t(p)) && !t->evicted.count(p))
                    t->have[std::size_t(p)] = true;
            break;
        }
        t->cv.wait_for(lock, std::chrono::milliseconds(50));
        if (t->removed || m_engine->closing) { *error = BrokerError::Closed; return nullptr; }
    }

    const lt::file_storage& files = fileLayout(*t->info);
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
    try {
        const lt::add_torrent_params atp = lt::load_torrent_file(QFile::encodeName(torrentFile).toStdString());
        if (!atp.ti)
            return {};
        if (name)
            *name = QString::fromStdString(atp.ti->name());
        return QString::fromStdString(idOf(atp.ti->info_hashes()));
    } catch (const std::exception&) {
        return {};
    }
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
    // Alerts can be dropped when libtorrent's queue overflows (many pieces
    // finishing at once). A read must never wait on an alert that will not
    // come, so every 2 s it asks libtorrent directly.
    // For the log: how long each step of a slow read took.
    const auto startedAt = std::chrono::steady_clock::now();
    auto verifiedAt = startedAt;
    for (;;) {
        // 1. Wait until the piece is downloaded and verified.
        int waits = 0;
        while (!t.have[piece]) {
            if (stopped())
                return -1;
            if (t.failed)
                return -2;
            t.cv.wait_for(lock, std::chrono::milliseconds(250));
            if (++waits % 8 == 0) {
                try {
                    if (!t.have[piece] && m_engine->holds(t, piece)) {
                        qCInfo(lcBroker) << "Piece" << piece << "was done without an alert";
                        t.have[piece] = true;
                        t.scheduled.erase(piece);
                    }
                } catch (const std::exception&) {
                }
            }
        }
        verifiedAt = std::chrono::steady_clock::now();
        // 2. Get its bytes from libtorrent (or the in-memory copy).
        waits = 0;
        bool dropped = false;
        bool rested = false;
        while (!t.pieces.count(piece)) {
            if (stopped())
                return -1;
            // RAM only: dropped from memory since it was verified. Fetch it
            // again (the engine reloads the torrent) and wait as in step 1.
            if (!t.have[piece]) {
                dropped = true;
                break;
            }
            // A failed read is asked again after a pause, so a passing
            // state (a torrent just reloaded, say) does not use up the tries.
            if (t.readErrors[piece] > 10)
                return -2;
            if (++waits % 20 == 0)
                t.readsPending.erase(piece);  // 5 s without an answer: ask again
            if (!t.readsPending.count(piece) && t.readErrors[piece] > 0 && !rested) {
                lock.unlock();
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
                lock.lock();
                rested = true;
                continue;  // the piece may have been dropped meanwhile
            }
            if (!t.readsPending.count(piece)) {
                rested = false;
                try {
                    t.handle.read_piece(lt::piece_index_t(piece));
                    t.readsPending.insert(piece);
                } catch (const std::exception&) {
                    return t.removed ? -1 : -2;
                }
            }
            t.cv.wait_for(lock, std::chrono::milliseconds(250));
        }
        if (!dropped)
            break;
        m_engine->schedule(t, true);
    }
    if (const auto now = std::chrono::steady_clock::now(); now - startedAt > std::chrono::milliseconds(40)) {
        using std::chrono::duration_cast;
        using std::chrono::milliseconds;
        qCDebug(lcBroker).noquote() << "Read of piece" << piece << "waited"
                                    << duration_cast<milliseconds>(verifiedAt - startedAt).count()
                                    << "ms for the download and" << duration_cast<milliseconds>(now - verifiedAt).count()
                                    << "ms for the bytes";
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

PieceState StreamHandle::stateAt(qint64 offset) const
{
    Torrent& t = *m_torrent;
    std::lock_guard lock(t.mutex);
    if (!t.hasMetadata || offset < 0 || offset >= m_reader->fileSize)
        return PieceState::Missing;
    const int piece = t.pieceAt(m_reader->fileOffset + offset);
    if (t.evicted.count(piece))
        return PieceState::Evicted;
    return piece < int(t.have.size()) && t.have[std::size_t(piece)] ? PieceState::Held : PieceState::Missing;
}

} // namespace cj
