#pragma once
// Torrent pieces kept in RAM instead of on disk ("RAM only" storage), so
// streaming writes nothing to the SSD. A libtorrent disk_interface that keeps
// each torrent's pieces in memory, plus a registry the broker uses to see what
// is held and to drop pieces when memory is over its cap.
//
// libtorrent cannot be told to forget a verified piece. The broker therefore
// tracks which pieces it dropped, and when a reader needs one of them again it
// re-adds the torrent with only the pieces still held (see Engine::reload).
// Reading a dropped piece answers "operation aborted", which libtorrent treats
// as harmless: it does not pause the torrent.

#include <libtorrent/disk_interface.hpp>
#include <libtorrent/file_storage.hpp>
#include <libtorrent/io_context.hpp>
#include <libtorrent/session_params.hpp>

#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace cj::broker_detail {

class MemoryRegistry;

// One torrent's pieces. Thread-safe: libtorrent's network thread writes and
// reads, the broker's threads look and drop.
class PieceStore
{
public:
    PieceStore(const lt::file_storage& files, MemoryRegistry& registry);
    ~PieceStore();

    bool has(int piece) const;
    void drop(int piece);
    void dropAllExcept(const std::vector<bool>& keep);
    std::vector<int> pieces() const;
    std::int64_t bytes() const { return m_bytes; }

    // For the disk interface.
    bool read(const lt::peer_request& r, std::vector<char>& out) const;
    void write(int piece, int offset, const char* data, int length);
    bool hash(int piece, lt::span<lt::sha256_hash> blockHashes, lt::sha1_hash& out) const;
    bool hash2(int piece, int offset, lt::sha256_hash& out) const;

private:
    int pieceSize(int piece) const;

    lt::file_storage m_files;
    MemoryRegistry& m_registry;
    mutable std::mutex m_mutex;
    std::map<int, std::vector<char>> m_pieces;
    std::atomic<std::int64_t> m_bytes{0};
};

// All stores, by torrent id (the info hash as the broker writes it). A store
// outlives its torrent in libtorrent, so a torrent removed and re-added a
// moment later finds its pieces again.
class MemoryRegistry
{
public:
    std::shared_ptr<PieceStore> find(const std::string& id) const;
    std::shared_ptr<PieceStore> findOrCreate(const std::string& id, const lt::file_storage& files);
    void erase(const std::string& id);
    std::int64_t totalBytes() const { return m_total; }

    // The disk interface constructor for lt::session_params.
    lt::disk_io_constructor_type constructor(std::shared_ptr<MemoryRegistry> self);

private:
    friend class PieceStore;
    mutable std::mutex m_mutex;
    std::map<std::string, std::shared_ptr<PieceStore>> m_stores;
    std::atomic<std::int64_t> m_total{0};
};

} // namespace cj::broker_detail
