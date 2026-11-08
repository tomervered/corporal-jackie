// Which pieces to ask for, by when, as readers move; and, in RAM-only mode,
// which pieces to drop.

#include "BrokerEngine.h"

namespace cj {
namespace broker_detail {

void Engine::schedule(Torrent& t, bool force)
{
    if (t.removed || !t.hasMetadata || !t.handle.is_valid())
        return;

    const qint64 pieceLen = t.pieceLength();
    std::map<int, int> desired;  // piece -> deadline ms
    auto want = [&](int p, int ms) {
        if (p < 0 || p >= int(t.have.size()) || t.have[p])
            return;
        // Dropped from memory: libtorrent will not fetch it again until the
        // torrent is reloaded without it.
        if (t.evicted.count(p))
            t.reloadWanted = true;
        auto it = desired.find(p);
        if (it == desired.end() || ms < it->second)
            desired[p] = ms;
    };

    std::set<int> background;  // after the urgent window, at low priority
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
        const qint64 window = qMin(readAheadBytes(bitrate, double(t.downloadRate), pieceLen),
                                   qMax(aheadCap(), 4 * pieceLen));
        const qint64 fileEnd = r->fileOffset + r->fileSize - 1;
        const int last = t.pieceAt(qMin(pos + window, fileEnd));
        // Each piece is due when the player will reach it at the file's bitrate.
        for (int p = first; p <= last; ++p)
            want(p, int(double(qint64(p - first) * pieceLen) * 1000.0 / bitrate));
        const qint64 ahead = std::min({qint64(bitrate * kBackgroundSeconds), kBackgroundMaxBytes, aheadCap()});
        const int bgLast = t.pieceAt(qMin(pos + qMax(window, ahead), fileEnd));
        for (int p = last + 1; p <= bgLast; ++p)
            if (!t.have[p] && !t.evicted.count(p))
                background.insert(p);

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
        for (auto it = background.begin(); it != background.end();)
            it = desired.count(*it) ? background.erase(it) : std::next(it);
        std::vector<std::pair<lt::piece_index_t, lt::download_priority_t>> priorities;
        for (const auto& [p, ms] : desired)
            if (!t.scheduled.count(p))
                priorities.emplace_back(lt::piece_index_t(p), lt::top_priority);
        for (int p : background)
            if (!t.background.count(p) || t.scheduled.count(p))
                priorities.emplace_back(lt::piece_index_t(p), lt::low_priority);
        for (const auto& [p, ms] : t.scheduled) {
            if (!desired.count(p) && !t.have[p]) {
                t.handle.reset_piece_deadline(lt::piece_index_t(p));
                if (!background.count(p))
                    priorities.emplace_back(lt::piece_index_t(p), lt::dont_download);
            }
        }
        for (int p : t.background)
            if (!background.count(p) && !desired.count(p) && !t.have[p])
                priorities.emplace_back(lt::piece_index_t(p), lt::dont_download);
        if (!priorities.empty())
            t.handle.prioritize_pieces(priorities);
        for (const auto& [p, ms] : desired)
            t.handle.set_piece_deadline(lt::piece_index_t(p), ms);
    } catch (const std::exception& e) {
        // The torrent went away under us (removed from the cache).
        qCDebug(lcBroker) << "Scheduling failed:" << e.what();
        return;
    }
    t.background = std::move(background);
    t.scheduled = std::move(desired);
}

bool Engine::holds(Torrent& t, int piece)
{
    if (t.evicted.count(piece))
        return false;
    return t.handle.have_piece(lt::piece_index_t(piece));
}

// RAM only: drop verified pieces until memory is back under its cap. Pieces of
// torrents nobody reads go first, then, for the ones being read, the pieces
// furthest from every reader (behind a reader before ahead of it). Never the
// pieces just behind a reader, its read-ahead, or the file's start and end
// (players re-read the container's headers and index).
void Engine::trimMemory()
{
    const qint64 target = qint64(double(memoryCap()) * kMemoryTrimTo);
    std::vector<std::shared_ptr<Torrent>> torrents;
    {
        std::lock_guard lock(m_mutex);
        for (auto& [id, t] : m_torrents)
            torrents.push_back(t);
    }
    int dropped = 0;
    auto drop = [&](Torrent& t, PieceStore& store, int p) {
        store.drop(p);
        if (p < int(t.have.size()) && t.have[std::size_t(p)]) {
            t.have[std::size_t(p)] = false;
            t.evicted.insert(p);
        }
        if (auto it = t.pieces.find(p); it != t.pieces.end()) {
            t.pieceBytes -= it->second.second;
            t.pieces.erase(it);
            t.pieceOrder.remove(p);
        }
        ++dropped;
    };

    for (int pass = 0; pass < 2 && m_memory->totalBytes() > target; ++pass) {
        for (auto& tp : torrents) {
            if (m_memory->totalBytes() <= target)
                break;
            Torrent& t = *tp;
            std::lock_guard tl(t.mutex);
            const bool idle = t.readers.empty() || t.openHandles == 0;
            if ((pass == 0) != idle || !t.hasMetadata)
                continue;
            auto store = m_memory->find(t.id);
            if (!store)
                continue;
            if (idle) {
                for (int p : store->pieces())
                    if (p < int(t.have.size()) && t.have[std::size_t(p)])
                        drop(t, *store, p);
                continue;
            }
            // What every reader still needs, and where each one is.
            std::set<int> keep;
            std::vector<int> at;
            for (const auto& weak : t.readers) {
                auto r = weak.lock();
                if (!r || r->closed || r->cancelled)
                    continue;
                const qint64 fileEnd = r->fileOffset + r->fileSize - 1;
                const qint64 pos = r->fileOffset + qMin(r->position.load(), qMax<qint64>(0, r->fileSize - 1));
                at.push_back(t.pieceAt(pos));
                auto keepRange = [&](qint64 from, qint64 to) {
                    for (int p = t.pieceAt(qMax(r->fileOffset, from)); p <= t.pieceAt(qMin(fileEnd, to)); ++p)
                        keep.insert(p);
                };
                keepRange(pos - qMin(kMemoryBehind, memoryCap() / 8), pos + aheadCap());
                keepRange(r->fileOffset, r->fileOffset + kHeadBytes);
                keepRange(fileEnd - kTailBytes, fileEnd);
            }
            if (at.empty())
                continue;
            std::vector<std::pair<qint64, int>> candidates;  // (how far, piece)
            for (int p : store->pieces()) {
                if (keep.count(p) || p >= int(t.have.size()) || !t.have[std::size_t(p)])
                    continue;
                qint64 distance = std::numeric_limits<qint64>::max();
                for (int a : at)
                    distance = qMin(distance, p < a ? qint64(a - p) * 2 : qint64(p - a));
                candidates.emplace_back(distance, p);
            }
            std::sort(candidates.begin(), candidates.end(), std::greater<>());
            for (const auto& [distance, p] : candidates) {
                if (m_memory->totalBytes() <= target)
                    break;
                drop(t, *store, p);
            }
        }
    }
    if (dropped > 0)
        qCInfo(lcBroker).noquote() << "Memory over its cap: dropped" << dropped << "pieces, now"
                                   << m_memory->totalBytes() / (1024 * 1024) << "MiB held";
}

// RAM only: a reader needs pieces dropped from memory, which libtorrent still
// counts as had. Remove the torrent and add it again at once, telling it only
// the pieces still held; it then downloads the rest again. Peers and trackers
// carry over, so it reconnects within a second or two. t.mutex is held all the
// way, so no reader uses the old handle in between.

} // namespace broker_detail
} // namespace cj
