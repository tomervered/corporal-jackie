// Choose which torrent pieces to fetch as readers move.

#include "BrokerEngine.h"

#include <QVarLengthArray>

namespace cj {
namespace broker_detail {

namespace {

// Adds the time one schedule pass took to the torrent's counters (shown in
// the stream statistics) when the pass ends, whichever way it returns.
class ScheduleTimer
{
public:
    explicit ScheduleTimer(Torrent& t) : m_torrent(t), m_started(Clock::now()) {}
    ~ScheduleTimer()
    {
        const auto took = std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - m_started);
        m_torrent.counters.scheduleMicros += took.count();
    }

private:
    Torrent& m_torrent;
    Clock::time_point m_started;
};

// Where one reader is in the torrent right now.
struct ReaderPosition
{
    std::shared_ptr<Reader> reader;
    qint64 byteOffset;  // from the start of the torrent, not of the file
    int piece;          // the piece that holds byteOffset
};

// Playing a single video takes one reader. Keep that common case on the stack.
using ReaderPositions = QVarLengthArray<ReaderPosition, 1>;

// The pieces one schedule pass wants, and which of them are urgent.
// Pieces the torrent already has are left out.
class PiecePlan
{
public:
    explicit PiecePlan(Torrent& t) : m_torrent(t) {}

    // True if the torrent does not have this piece yet.
    bool isMissing(int piece)
    {
        if (piece < 0 || piece >= int(m_torrent.have.size()) || m_torrent.have[std::size_t(piece)])
            return false;
        // Dropped from memory: libtorrent will not fetch it again until the
        // torrent is reloaded without it.
        if (m_torrent.evicted.count(piece))
            m_torrent.reloadWanted = true;
        return true;
    }

    // Asks for a missing piece. A piece asked for twice keeps the higher priority.
    void want(int piece, lt::download_priority_t priority)
    {
        if (!isMissing(piece))
            return;
        auto it = priorities.find(piece);
        if (it == priorities.end() || it->second < priority)
            priorities[piece] = priority;
    }

    // Asks for a missing piece as soon as possible: top priority and a
    // deadline of 0 ms, so libtorrent requests it from several peers at once.
    void wantNow(int piece)
    {
        if (!isMissing(piece))
            return;
        deadlines[piece] = 0;
        want(piece, lt::top_priority);
    }

    std::map<int, int> deadlines;                       // piece -> ms
    std::map<int, lt::download_priority_t> priorities;  // every piece to fetch

private:
    Torrent& m_torrent;
};

// Drops readers whose stream was closed or cancelled.
void forgetClosedReaders(Torrent& t)
{
    auto isGone = [](const std::weak_ptr<Reader>& weak) {
        auto reader = weak.lock();
        return !reader || reader->closed || reader->cancelled;
    };
    t.readers.erase(std::remove_if(t.readers.begin(), t.readers.end(), isGone), t.readers.end());
}

// Fills `positions` with where every reader is. Returns true if any reader
// moved to another piece since the last pass. A move to the very next piece
// is normal playback; any other move (opening, a seek) makes the new piece
// urgent (Reader::deadlinePiece).
bool findReaderPositions(Torrent& t, ReaderPositions& positions)
{
    bool anyMoved = false;
    positions.reserve(t.readers.size());
    for (const auto& weak : t.readers) {
        auto reader = weak.lock();
        const qint64 positionInFile = qMin(reader->position.load(), qMax<qint64>(0, reader->fileSize - 1));
        const qint64 byteOffset = reader->fileOffset + positionInFile;
        const int piece = t.pieceAt(byteOffset);
        if (piece != reader->scheduledPiece) {
            const bool jumped = reader->scheduledPiece < 0 || piece != reader->scheduledPiece + 1;
            if (jumped)
                reader->deadlinePiece = piece;
            reader->scheduledPiece = piece;
            anyMoved = true;
        }
        positions.push_back({std::move(reader), byteOffset, piece});
    }
    return anyMoved;
}

// Adds to `plan` the pieces one reader needs, from most to least urgent:
//   1. the piece it is reading, if it just opened or jumped there;
//   2. the next piece, if its current read already reaches into it;
//   3. the read-ahead window, enough to play through at the current speed;
//   4. a background buffer beyond that, at low priority, for spare bandwidth;
//   5. the start and end of the file, where video containers keep their
//      headers and index, which players read first.
// `aheadCap` is the most bytes ahead of a reader worth asking for.
void planPiecesForReader(Torrent& t, const ReaderPosition& position, qint64 aheadCap, PiecePlan& plan)
{
    const Reader& reader = *position.reader;
    const int firstPiece = position.piece;
    const qint64 pieceLength = t.pieceLength();
    const double bitrate = reader.bitrate();
    const qint64 fileEnd = reader.fileOffset + reader.fileSize - 1;
    const int fileLastPiece = t.pieceAt(fileEnd);

    if (firstPiece == reader.deadlinePiece)
        plan.wantNow(firstPiece);
    // read() returns at a piece boundary. Pull in the next piece if
    // this request already covers bytes on its far side.
    if (reader.nextReadPiece >= firstPiece && reader.nextReadPiece <= fileLastPiece)
        plan.wantNow(reader.nextReadPiece);

    const qint64 readAhead = qMin(readAheadBytes(bitrate, double(t.downloadRate), pieceLength),
                                  qMax(aheadCap, 4 * pieceLength));
    const int readAheadLastPiece = t.pieceAt(qMin(position.byteOffset + readAhead, fileEnd));
    for (int p = firstPiece; p <= readAheadLastPiece; ++p)
        plan.want(p, lt::default_priority);

    const qint64 background = std::min({qint64(bitrate * kBackgroundSeconds), kBackgroundMaxBytes, aheadCap});
    const int backgroundLastPiece = t.pieceAt(qMin(position.byteOffset + qMax(readAhead, background), fileEnd));
    for (int p = readAheadLastPiece + 1; p <= backgroundLastPiece; ++p)
        if (!t.evicted.count(p))
            plan.want(p, lt::low_priority);

    // Fetched early, without deadlines: when the player reads them, its
    // position makes them urgent.
    const int headLastPiece = t.pieceAt(qMin(reader.fileOffset + kHeadBytes, fileEnd));
    for (int p = t.pieceAt(reader.fileOffset); p <= headLastPiece; ++p)
        plan.want(p, lt::default_priority);
    const int tailFirstPiece = t.pieceAt(qMax(reader.fileOffset, fileEnd - kTailBytes));
    for (int p = tailFirstPiece; p <= fileLastPiece; ++p)
        plan.want(p, lt::default_priority);
}

// Sends libtorrent only what changed since the last pass (Torrent::wanted
// and Torrent::scheduled), then remembers this plan as the last one sent.
// Throws if the torrent went away in the meantime.
void sendPlan(Engine& engine, Torrent& t, const ReaderPositions& positions, PiecePlan& plan)
{
    std::set<int> readerFiles;
    for (const ReaderPosition& position : positions)
        readerFiles.insert(position.reader->fileIndex);
    // Turning a file on resets its pieces' priorities inside libtorrent.
    if (engine.openFiles(t, readerFiles))
        t.wanted.clear();  // so send every piece again

    std::vector<std::pair<lt::piece_index_t, lt::download_priority_t>> changes;
    // Nothing sent yet: every piece not in the plan must be turned off.
    if (t.wanted.empty()) {
        for (int p = 0; p < int(t.have.size()); ++p)
            if (!plan.priorities.count(p))
                changes.emplace_back(lt::piece_index_t(p), lt::dont_download);
    }
    // Pieces new to the plan, or with another priority than last time.
    for (const auto& [p, priority] : plan.priorities) {
        auto old = t.wanted.find(p);
        if (old == t.wanted.end() || old->second != priority)
            changes.emplace_back(lt::piece_index_t(p), priority);
    }
    // Pieces wanted last time but not now, and still missing.
    for (const auto& [p, priority] : t.wanted)
        if (!plan.priorities.count(p) && !t.have[std::size_t(p)])
            changes.emplace_back(lt::piece_index_t(p), lt::dont_download);
    if (!changes.empty())
        t.handle.prioritize_pieces(changes);

    for (const auto& [p, ms] : t.scheduled)
        if (!plan.deadlines.count(p) && !t.have[std::size_t(p)])
            t.handle.reset_piece_deadline(lt::piece_index_t(p));
    for (const auto& [p, ms] : plan.deadlines)
        t.handle.set_piece_deadline(lt::piece_index_t(p), ms);

    t.wanted = std::move(plan.priorities);
    t.scheduled = std::move(plan.deadlines);
}

} // namespace

// Tells libtorrent which pieces to fetch, and which of them first, for
// where the readers are now. Runs on every read, so it returns early when
// no reader moved to another piece.
//
// One deadline is used, when opening or seeking. After that, reads in order
// let libtorrent's sequential picker fill the buffer and repair gaps as
// later pieces complete.
void Engine::schedule(Torrent& t, bool force)
{
    if (t.removed || !t.hasMetadata || !t.handle.is_valid())
        return;
    ++t.counters.schedulePasses;
    const ScheduleTimer timer(t);

    forgetClosedReaders(t);
    // Most reads stay inside the piece the last pass planned for (a player
    // reads a piece in many small steps), so this is checked before building
    // the plan, which covers up to 600 s of video. Pieces that arrive in the
    // meantime need no new plan; a dropped one the reader waits for comes
    // back through the forced call in StreamHandle::read.
    ReaderPositions positions;
    const bool anyReaderMoved = findReaderPositions(t, positions);
    if (!anyReaderMoved && !force)
        return;

    PiecePlan plan(t);
    for (const ReaderPosition& position : positions)
        planPiecesForReader(t, position, aheadCap(), plan);
    ++t.counters.scheduleSent;

    try {
        sendPlan(*this, t, positions, plan);
    } catch (const std::exception& e) {
        // The torrent went away under us (removed from the cache).
        qCDebug(lcBroker) << "Scheduling failed:" << e.what();
    }
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
