// Choose which torrent pieces to fetch as readers move.

#include "BrokerEngine.h"

#include <QVarLengthArray>

namespace cj {
namespace broker_detail {

// Use one deadline when opening or seeking. Contiguous reads let the sequential
// picker fill the buffer and repair gaps as later pieces complete.
void Engine::schedule(Torrent& t, bool force)
{
    if (t.removed || !t.hasMetadata || !t.handle.is_valid())
        return;
    const auto started = Clock::now();
    ++t.counters.schedulePasses;
    struct Timer
    {
        Torrent& t;
        Clock::time_point started;
        ~Timer()
        {
            t.counters.scheduleMicros +=
                std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - started).count();
        }
    } timer{t, started};

    const qint64 pieceLen = t.pieceLength();
    t.readers.erase(std::remove_if(t.readers.begin(), t.readers.end(),
                                   [](const std::weak_ptr<Reader>& w) {
                                       auto r = w.lock();
                                       return !r || r->closed || r->cancelled;
                                   }),
                    t.readers.end());

    // A new schedule is sent when a reader moves to another piece. That is
    // cheap to check and false for most reads (a player reads a piece in
    // many small steps), so check it before building the piece maps, which
    // cover up to 600 s of video. Pieces that arrive in the meantime need no
    // new schedule; a dropped one the reader waits for comes back through
    // the forced call in StreamHandle::read.
    struct ReaderPosition
    {
        std::shared_ptr<Reader> reader;
        qint64 byteOffset;
        int firstPiece;
    };
    // Playing a single video takes one reader. Keep that common case on the stack.
    QVarLengthArray<ReaderPosition, 1> positions;
    positions.reserve(t.readers.size());
    bool anyChange = force;
    for (const auto& weak : t.readers) {
        auto r = weak.lock();
        const qint64 pos = r->fileOffset + qMin(r->position.load(), qMax<qint64>(0, r->fileSize - 1));
        const int first = t.pieceAt(pos);
        if (first != r->scheduledPiece) {
            if (r->scheduledPiece < 0 || first != r->scheduledPiece + 1)
                r->deadlinePiece = first;
            r->scheduledPiece = first;
            anyChange = true;
        }
        positions.push_back({std::move(r), pos, first});
    }
    if (!anyChange)
        return;

    std::map<int, int> deadlines;                 // piece -> ms
    std::map<int, lt::download_priority_t> wanted;  // every piece to fetch
    auto missing = [&](int p) {
        if (p < 0 || p >= int(t.have.size()) || t.have[std::size_t(p)])
            return false;
        // Dropped from memory: libtorrent will not fetch it again until the
        // torrent is reloaded without it.
        if (t.evicted.count(p))
            t.reloadWanted = true;
        return true;
    };
    auto want = [&](int p, lt::download_priority_t prio) {
        if (!missing(p))
            return;
        auto it = wanted.find(p);
        if (it == wanted.end() || it->second < prio)
            wanted[p] = prio;
    };

    for (const ReaderPosition& position : positions) {
        const Reader& r = *position.reader;
        const int first = position.firstPiece;
        const double bitrate = r.bitrate();
        const qint64 window = qMin(readAheadBytes(bitrate, double(t.downloadRate), pieceLen),
                                   qMax(aheadCap(), 4 * pieceLen));
        const qint64 ahead = std::min({qint64(bitrate * kBackgroundSeconds), kBackgroundMaxBytes, aheadCap()});
        const qint64 fileEnd = r.fileOffset + r.fileSize - 1;
        const int fileLast = t.pieceAt(fileEnd);

        if (first == r.deadlinePiece && missing(first)) {
            deadlines[first] = 0;
            want(first, lt::top_priority);
        }
        // read() returns at a piece boundary. Pull in the next piece if
        // this request already covers bytes on its far side.
        if (r.nextReadPiece >= first && r.nextReadPiece <= fileLast && missing(r.nextReadPiece)) {
            deadlines[r.nextReadPiece] = 0;
            want(r.nextReadPiece, lt::top_priority);
        }
        const int last = t.pieceAt(qMin(position.byteOffset + window, fileEnd));
        for (int p = first; p <= last; ++p)
            want(p, lt::default_priority);
        const int bgLast = t.pieceAt(qMin(position.byteOffset + qMax(window, ahead), fileEnd));
        for (int p = last + 1; p <= bgLast; ++p)
            if (!t.evicted.count(p))
                want(p, lt::low_priority);

        // The start and the end of the file hold the container's headers and
        // index, which players read first. Fetched early, without deadlines:
        // when the player reads them, its position makes them urgent.
        for (int p = t.pieceAt(r.fileOffset); p <= t.pieceAt(qMin(r.fileOffset + kHeadBytes, fileEnd)); ++p)
            want(p, lt::default_priority);
        for (int p = t.pieceAt(qMax(r.fileOffset, fileEnd - kTailBytes)); p <= fileLast; ++p)
            want(p, lt::default_priority);
    }
    ++t.counters.scheduleSent;

    try {
        std::set<int> readerFiles;
        for (const ReaderPosition& position : positions)
            readerFiles.insert(position.reader->fileIndex);
        if (openFiles(t, readerFiles))
            t.wanted.clear();  // send every piece again
        std::vector<std::pair<lt::piece_index_t, lt::download_priority_t>> priorities;
        if (t.wanted.empty()) {
            for (int p = 0; p < int(t.have.size()); ++p)
                if (!wanted.count(p))
                    priorities.emplace_back(lt::piece_index_t(p), lt::dont_download);
        }
        for (const auto& [p, prio] : wanted) {
            auto old = t.wanted.find(p);
            if (old == t.wanted.end() || old->second != prio)
                priorities.emplace_back(lt::piece_index_t(p), prio);
        }
        for (const auto& [p, prio] : t.wanted)
            if (!wanted.count(p) && !t.have[std::size_t(p)])
                priorities.emplace_back(lt::piece_index_t(p), lt::dont_download);
        if (!priorities.empty())
            t.handle.prioritize_pieces(priorities);
        for (const auto& [p, ms] : t.scheduled)
            if (!deadlines.count(p) && !t.have[std::size_t(p)])
                t.handle.reset_piece_deadline(lt::piece_index_t(p));
        for (const auto& [p, ms] : deadlines)
            t.handle.set_piece_deadline(lt::piece_index_t(p), ms);
    } catch (const std::exception& e) {
        // The torrent went away under us (removed from the cache).
        qCDebug(lcBroker) << "Scheduling failed:" << e.what();
        return;
    }
    t.wanted = std::move(wanted);
    t.scheduled = std::move(deadlines);
}

bool Engine::openFiles(Torrent& t, const std::set<int>& fileIndexes)
{
    const int count = fileLayout(*t.info).num_files();
    // Fresh (new metadata, a reload): every file starts off.
    bool changed = int(t.files.size()) != count;
    if (changed)
        t.files.assign(std::size_t(count), lt::dont_download);
    for (int f : fileIndexes) {
        if (f >= 0 && f < count && t.files[std::size_t(f)] == lt::dont_download) {
            t.files[std::size_t(f)] = lt::default_priority;
            changed = true;
        }
    }
    if (changed)
        t.handle.prioritize_files(t.files);
    return changed;
}

bool Engine::holds(Torrent& t, int piece)
{
    if (t.evicted.count(piece))
        return false;
    return t.handle.have_piece(lt::piece_index_t(piece));
}

} // namespace broker_detail
} // namespace cj
