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

namespace {

// Until the torrent's file list is here, at most the metadata timeout.
BrokerError waitForMetadata(Engine& engine, Torrent& t, std::unique_lock<std::mutex>& lock,
                            const std::function<bool()>& cancelled)
{
    const auto deadline = Clock::now() + std::chrono::milliseconds(engine.settings.metadataTimeoutMs);
    while (!t.hasMetadata) {
        if (t.failed)
            return BrokerError::TorrentError;
        if (t.removed || engine.closing)
            return BrokerError::Closed;
        if (cancelled && cancelled())
            return BrokerError::Cancelled;
        if (Clock::now() >= deadline) {
            qCWarning(lcBroker).noquote() << "No metadata for" << QString::fromStdString(t.id) << "in"
                                          << engine.settings.metadataTimeoutMs << "ms";
            return BrokerError::MetadataTimeout;
        }
        t.cv.wait_for(lock, std::chrono::milliseconds(100));
    }
    return BrokerError::None;
}

// A torrent reopened from the cache first reads its resume data, in the
// background. Until then libtorrent reports none of its pieces, and the
// reader would wait for (or ask peers for) pieces already on disk. It takes
// a moment; wait for it, at most 3 s, and take the pieces it lists.
BrokerError takeResumedPieces(Engine& engine, Torrent& t, std::unique_lock<std::mutex>& lock)
{
    for (int i = 0; i < 60; ++i) {
        lt::torrent_status st;
        try {
            st = t.handle.status(lt::torrent_handle::query_pieces);
        } catch (const std::exception&) {
            break;
        }
        if (st.state != lt::torrent_status::checking_resume_data) {
            for (int p = 0; p < int(st.pieces.size()) && p < int(t.have.size()); ++p)
                if (st.pieces.get_bit(lt::piece_index_t(p)) && !t.evicted.count(p))
                    t.have[std::size_t(p)] = true;
            break;
        }
        t.cv.wait_for(lock, std::chrono::milliseconds(50));
        if (t.removed || engine.closing)
            return BrokerError::Closed;
    }
    return BrokerError::None;
}

} // namespace

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
    *error = waitForMetadata(*m_engine, *t, lock, cancelled);
    if (*error == BrokerError::None)
        *error = takeResumedPieces(*m_engine, *t, lock);
    if (*error != BrokerError::None)
        return nullptr;
    const lt::file_storage& files = fileLayout(*t->info);
    const int index = request.fileIndex >= 0 ? request.fileIndex : largestFile(files);
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

namespace {

// What a wait in StreamHandle::read() ended with.
enum class Wait { Done, Dropped, Stopped, Failed };

// Step 1 of a read: until the piece is downloaded and verified. Alerts can be
// dropped when libtorrent's queue overflows (many pieces finishing at once),
// so every 2 s it asks libtorrent directly instead of waiting on one.
template <typename Stopped>
Wait waitUntilVerified(Engine& engine, Torrent& t, std::unique_lock<std::mutex>& lock, int piece, Stopped stopped)
{
    for (int waits = 1; !t.have[std::size_t(piece)]; ++waits) {
        if (stopped())
            return Wait::Stopped;
        if (t.failed)
            return Wait::Failed;
        t.cv.wait_for(lock, std::chrono::milliseconds(250));
        if (waits % 40 == 0)
            qCDebug(lcBroker).noquote() << "Read of piece" << piece << "of" << QString::fromStdString(t.id)
                                        << "still waits for the download after" << waits / 4 << "s";
        if (waits % 8 == 0 && !t.have[std::size_t(piece)]) {
            try {
                if (engine.holds(t, piece)) {
                    qCInfo(lcBroker) << "Piece" << piece << "was done without an alert";
                    t.have[std::size_t(piece)] = true;
                    t.scheduled.erase(piece);
                }
            } catch (const std::exception&) {
            }
        }
    }
    return Wait::Done;
}

// Step 2: until libtorrent has handed over the verified piece's bytes.
// Dropped: RAM only, the piece left memory after it was verified.
template <typename Stopped>
Wait waitForBytes(Torrent& t, std::unique_lock<std::mutex>& lock, int piece, Stopped stopped)
{
    bool rested = false;
    for (int waits = 1; !t.pieces.count(piece); ++waits) {
        if (stopped())
            return Wait::Stopped;
        if (!t.have[std::size_t(piece)])
            return Wait::Dropped;
        if (t.readErrors[piece] > 10)
            return Wait::Failed;
        if (waits % 20 == 0) {
            qCDebug(lcBroker).noquote() << "Read of piece" << piece << "of" << QString::fromStdString(t.id)
                                        << "has no bytes after" << waits / 4 << "s; errors" << t.readErrors[piece];
            t.readsPending.erase(piece);  // 5 s without an answer: ask again
        }
        if (!t.readsPending.count(piece)) {
            // A failed read is asked again after a pause, so a passing state
            // (a torrent just reloaded, say) does not use up the tries.
            if (t.readErrors[piece] > 0 && !rested) {
                lock.unlock();
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
                lock.lock();
                rested = true;
                continue;  // the piece may have been dropped meanwhile
            }
            rested = false;
            try {
                t.handle.read_piece(lt::piece_index_t(piece));
                t.readsPending.insert(piece);
            } catch (const std::exception&) {
                return t.removed ? Wait::Stopped : Wait::Failed;
            }
        }
        t.cv.wait_for(lock, std::chrono::milliseconds(250));
    }
    return Wait::Done;
}

// Readers usually go on to the next piece: ask for its bytes now.
void readAhead(Torrent& t, int piece)
{
    if (piece >= int(t.have.size()) || !t.have[std::size_t(piece)] || t.pieces.count(piece)
        || t.readsPending.count(piece))
        return;
    try {
        t.handle.read_piece(lt::piece_index_t(piece));
        t.readsPending.insert(piece);
    } catch (const std::exception&) {
    }
}

} // namespace

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
    const qint64 inPiece = absolute - qint64(piece) * t.pieceLength();
    const qint64 count = std::min({length, r.fileSize - offset, qint64(t.info->piece_size(lt::piece_index_t(piece))) - inPiece});

    r.position = offset;
    m_engine->schedule(t, false);

    auto stopped = [&] { return r.cancelled || r.closed || t.removed || m_engine->closing.load(); };
    // How long each step took, for the log.
    const auto startedAt = Clock::now();
    auto verifiedAt = startedAt;
    for (;;) {
        Wait w = waitUntilVerified(*m_engine, t, lock, piece, stopped);
        verifiedAt = Clock::now();
        if (w == Wait::Done)
            w = waitForBytes(t, lock, piece, stopped);
        if (w == Wait::Stopped)
            return -1;
        if (w == Wait::Failed)
            return -2;
        if (w == Wait::Done)
            break;
        m_engine->schedule(t, true);  // dropped: fetch it again, and wait as before
    }
    if (const auto now = Clock::now(); now - startedAt > std::chrono::milliseconds(40)) {
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
    readAhead(t, piece + 1);
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
