#include "BrokerEngine.h"

namespace cj {
namespace broker_detail {

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

} // namespace broker_detail
} // namespace cj
