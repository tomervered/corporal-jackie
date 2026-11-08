#include "MemoryStorage.h"

#include <libtorrent/aux_/vector.hpp>
#include <libtorrent/disk_buffer_holder.hpp>
#include <libtorrent/error_code.hpp>
#include <libtorrent/hasher.hpp>
#include <libtorrent/storage_defs.hpp>
#include <libtorrent/version.hpp>

#include <boost/asio/post.hpp>

#include <algorithm>
#include <cstring>
#include <sstream>

namespace cj::broker_detail {

namespace {

// libtorrent 2.1 (MSYS2 ships it) changed a few disk interface types: status
// codes became flags, and buffers no longer carry their size.
#if LIBTORRENT_VERSION_NUM >= 20100
const lt::status_t kStatusOk{};
const lt::status_t kStatusFatal = lt::disk_status::fatal_disk_error;
#else
const lt::status_t kStatusOk = lt::status_t::no_error;
const lt::status_t kStatusFatal = lt::status_t::fatal_disk_error;
#endif

lt::storage_error aborted()
{
    // libtorrent ignores this error on its own reads (no paused torrent, no
    // error state); peers asking for the piece are told we no longer have it.
    lt::storage_error e;
    e.ec = boost::asio::error::operation_aborted;
    e.operation = lt::operation_t::file_read;
    return e;
}

std::string idOf(const lt::sha1_hash& h)
{
    std::ostringstream s;
    s << h;
    return s.str();
}

} // namespace

// --- PieceStore ---------------------------------------------------------------

PieceStore::PieceStore(const lt::file_storage& files, MemoryRegistry& registry)
    : m_files(files)
    , m_registry(registry)
{
}

PieceStore::~PieceStore() { m_registry.m_total -= m_bytes; }

int PieceStore::pieceSize(int piece) const { return m_files.piece_size(lt::piece_index_t(piece)); }

bool PieceStore::has(int piece) const
{
    std::lock_guard lock(m_mutex);
    return m_pieces.count(piece) > 0;
}

void PieceStore::drop(int piece)
{
    std::lock_guard lock(m_mutex);
    if (auto it = m_pieces.find(piece); it != m_pieces.end()) {
        const auto size = std::int64_t(it->second.size());
        m_bytes -= size;
        m_registry.m_total -= size;
        m_pieces.erase(it);
    }
}

void PieceStore::dropAllExcept(const std::vector<bool>& keep)
{
    std::lock_guard lock(m_mutex);
    for (auto it = m_pieces.begin(); it != m_pieces.end();) {
        if (it->first < int(keep.size()) && keep[std::size_t(it->first)]) {
            ++it;
            continue;
        }
        const auto size = std::int64_t(it->second.size());
        m_bytes -= size;
        m_registry.m_total -= size;
        it = m_pieces.erase(it);
    }
}

std::vector<int> PieceStore::pieces() const
{
    std::lock_guard lock(m_mutex);
    std::vector<int> out;
    out.reserve(m_pieces.size());
    for (const auto& [p, data] : m_pieces)
        out.push_back(p);
    return out;
}

bool PieceStore::read(const lt::peer_request& r, std::vector<char>& out) const
{
    std::lock_guard lock(m_mutex);
    auto it = m_pieces.find(static_cast<int>(r.piece));
    if (it == m_pieces.end() || r.start < 0 || r.start >= int(it->second.size()))
        return false;
    const int n = std::min(r.length, int(it->second.size()) - r.start);
    out.assign(it->second.begin() + r.start, it->second.begin() + r.start + n);
    return true;
}

void PieceStore::write(int piece, int offset, const char* data, int length)
{
    std::lock_guard lock(m_mutex);
    auto& buffer = m_pieces[piece];
    if (buffer.empty()) {
        const int size = pieceSize(piece);
        buffer.resize(std::size_t(size));
        m_bytes += size;
        m_registry.m_total += size;
    }
    if (offset < 0 || offset + length > int(buffer.size()))
        return;
    std::memcpy(buffer.data() + offset, data, std::size_t(length));
}

bool PieceStore::hash(int piece, lt::span<lt::sha256_hash> blockHashes, lt::sha1_hash& out) const
{
    std::lock_guard lock(m_mutex);
    auto it = m_pieces.find(piece);
    if (it == m_pieces.end())
        return false;
    const std::vector<char>& data = it->second;
    if (!blockHashes.empty()) {
        // v2 torrents: a SHA-256 per 16 KiB block of the piece's own file part.
        const int size2 = m_files.piece_size2(lt::piece_index_t(piece));
        const int blocks = m_files.blocks_in_piece2(lt::piece_index_t(piece));
        int offset = 0;
        for (int k = 0; k < blocks && k < int(blockHashes.size()); ++k) {
            const int len = std::min(lt::default_block_size, size2 - offset);
            blockHashes[k] = lt::hasher256(data.data() + offset, len).final();
            offset += len;
        }
    }
    out = lt::hasher(data.data(), int(data.size())).final();
    return true;
}

bool PieceStore::hash2(int piece, int offset, lt::sha256_hash& out) const
{
    std::lock_guard lock(m_mutex);
    auto it = m_pieces.find(piece);
    if (it == m_pieces.end())
        return false;
    const int size2 = m_files.piece_size2(lt::piece_index_t(piece));
    const int len = std::min(lt::default_block_size, size2 - offset);
    if (offset < 0 || len <= 0)
        return false;
    out = lt::hasher256(it->second.data() + offset, len).final();
    return true;
}

// --- MemoryRegistry -------------------------------------------------------------

std::shared_ptr<PieceStore> MemoryRegistry::find(const std::string& id) const
{
    std::lock_guard lock(m_mutex);
    auto it = m_stores.find(id);
    return it == m_stores.end() ? nullptr : it->second;
}

std::shared_ptr<PieceStore> MemoryRegistry::findOrCreate(const std::string& id, const lt::file_storage& files)
{
    std::lock_guard lock(m_mutex);
    auto& store = m_stores[id];
    if (!store)
        store = std::make_shared<PieceStore>(files, *this);
    return store;
}

void MemoryRegistry::erase(const std::string& id)
{
    std::shared_ptr<PieceStore> gone;  // freed outside the lock
    std::lock_guard lock(m_mutex);
    if (auto it = m_stores.find(id); it != m_stores.end()) {
        gone = std::move(it->second);
        m_stores.erase(it);
    }
}

// --- The disk interface ---------------------------------------------------------

namespace {

class MemoryDiskIo final : public lt::disk_interface, public lt::buffer_allocator_interface
{
public:
    MemoryDiskIo(lt::io_context& ioc, std::shared_ptr<MemoryRegistry> registry)
        : m_ioc(ioc)
        , m_registry(std::move(registry))
    {
    }

    lt::storage_holder new_torrent(const lt::storage_params& params, const std::shared_ptr<void>&) override
    {
        const std::string id = idOf(params.info_hash);
        auto store = m_registry->findOrCreate(id, params.files);
        lt::storage_index_t idx;
        if (m_free.empty()) {
            idx = m_slots.end_index();
            m_slots.emplace_back(Slot{id, store});
        } else {
            idx = m_free.back();
            m_free.pop_back();
            m_slots[idx] = Slot{id, store};
        }
        return lt::storage_holder(idx, *this);
    }

    // The store stays in the registry: the broker may re-add this torrent
    // at once (reload), and decides itself when its pieces go.
    void remove_torrent(lt::storage_index_t idx) override
    {
        m_slots[idx] = Slot{};
        m_free.push_back(idx);
    }

    void async_read(lt::storage_index_t storage, const lt::peer_request& r,
                    std::function<void(lt::disk_buffer_holder, const lt::storage_error&)> handler,
                    lt::disk_job_flags_t) override
    {
        std::vector<char> data;
        const auto& store = m_slots[storage].store;
        if (!store || !store->read(r, data)) {
            boost::asio::post(m_ioc, [handler = std::move(handler)] { handler(lt::disk_buffer_holder(), aborted()); });
            return;
        }
        // At least the asked length: libtorrent 2.1 assumes it.
        const int size = int(data.size());
        char* buffer = new char[std::size_t(std::max(size, r.length))];
        std::memcpy(buffer, data.data(), data.size());
        boost::asio::post(m_ioc, [this, handler = std::move(handler), buffer, size] {
#if LIBTORRENT_VERSION_NUM >= 20100
            (void)size;
            handler(lt::disk_buffer_holder(*this, buffer), lt::storage_error());
#else
            handler(lt::disk_buffer_holder(*this, buffer, size), lt::storage_error());
#endif
        });
    }

    bool async_write(lt::storage_index_t storage, const lt::peer_request& r, const char* buf,
                     std::shared_ptr<lt::disk_observer>, std::function<void(const lt::storage_error&)> handler,
                     lt::disk_job_flags_t) override
    {
        if (const auto& store = m_slots[storage].store)
            store->write(static_cast<int>(r.piece), r.start, buf, r.length);
        boost::asio::post(m_ioc, [handler = std::move(handler)] { handler(lt::storage_error()); });
        return false;
    }

    void async_hash(lt::storage_index_t storage, lt::piece_index_t piece, lt::span<lt::sha256_hash> v2,
                    lt::disk_job_flags_t,
                    std::function<void(lt::piece_index_t, const lt::sha1_hash&, const lt::storage_error&)> handler) override
    {
        lt::sha1_hash h;
        const auto& store = m_slots[storage].store;
        const bool ok = store && store->hash(static_cast<int>(piece), v2, h);
        boost::asio::post(m_ioc, [handler = std::move(handler), piece, h, ok] {
            handler(piece, h, ok ? lt::storage_error() : aborted());
        });
    }

    void async_hash2(lt::storage_index_t storage, lt::piece_index_t piece, int offset, lt::disk_job_flags_t,
                     std::function<void(lt::piece_index_t, const lt::sha256_hash&, const lt::storage_error&)> handler) override
    {
        lt::sha256_hash h;
        const auto& store = m_slots[storage].store;
        const bool ok = store && store->hash2(static_cast<int>(piece), offset, h);
        boost::asio::post(m_ioc, [handler = std::move(handler), piece, h, ok] {
            handler(piece, h, ok ? lt::storage_error() : aborted());
        });
    }

    void async_move_storage(lt::storage_index_t, std::string path, lt::move_flags_t,
                            std::function<void(lt::status_t, const std::string&, const lt::storage_error&)> handler) override
    {
        boost::asio::post(m_ioc, [handler = std::move(handler), path] {
            lt::storage_error e;
            e.ec = boost::system::errc::make_error_code(boost::system::errc::operation_not_supported);
            handler(kStatusFatal, path, e);
        });
    }

    void async_release_files(lt::storage_index_t, std::function<void()> handler) override
    {
        if (handler)
            boost::asio::post(m_ioc, std::move(handler));
    }

    void async_check_files(lt::storage_index_t, const lt::add_torrent_params*,
                           lt::aux::vector<std::string, lt::file_index_t>,
                           std::function<void(lt::status_t, const lt::storage_error&)> handler) override
    {
        // Nothing on disk to check; pieces named in resume data are held.
        boost::asio::post(m_ioc, [handler = std::move(handler)] { handler(kStatusOk, lt::storage_error()); });
    }

    void async_stop_torrent(lt::storage_index_t, std::function<void()> handler) override
    {
        if (handler)
            boost::asio::post(m_ioc, std::move(handler));
    }

    void async_rename_file(lt::storage_index_t, lt::file_index_t index, std::string name,
                           std::function<void(const std::string&, lt::file_index_t, const lt::storage_error&)> handler) override
    {
        boost::asio::post(m_ioc, [handler = std::move(handler), index, name] { handler(name, index, lt::storage_error()); });
    }

    void async_delete_files(lt::storage_index_t storage, lt::remove_flags_t,
                            std::function<void(const lt::storage_error&)> handler) override
    {
        // Removed for good (the cache cap): its pieces go too.
        m_registry->erase(m_slots[storage].id);
        boost::asio::post(m_ioc, [handler = std::move(handler)] { handler(lt::storage_error()); });
    }

    void async_set_file_priority(lt::storage_index_t, lt::aux::vector<lt::download_priority_t, lt::file_index_t> prio,
                                 std::function<void(const lt::storage_error&,
                                                    lt::aux::vector<lt::download_priority_t, lt::file_index_t>)> handler) override
    {
        boost::asio::post(m_ioc, [handler = std::move(handler), prio = std::move(prio)]() mutable {
            handler(lt::storage_error(), std::move(prio));
        });
    }

    void async_clear_piece(lt::storage_index_t storage, lt::piece_index_t index,
                           std::function<void(lt::piece_index_t)> handler) override
    {
        // A piece that failed its hash check: its data is useless.
        if (const auto& store = m_slots[storage].store)
            store->drop(static_cast<int>(index));
        boost::asio::post(m_ioc, [handler = std::move(handler), index] { handler(index); });
    }

    void update_stats_counters(lt::counters&) const override {}
    std::vector<lt::open_file_state> get_status(lt::storage_index_t) const override { return {}; }
    void abort(bool) override {}
    void submit_jobs() override {}
    void settings_updated() override {}

    void free_disk_buffer(char* buffer) override { delete[] buffer; }
#if LIBTORRENT_VERSION_NUM >= 20100
    void free_multiple_buffers(lt::span<char*> buffers) override
    {
        for (char* b : buffers)
            delete[] b;
    }
#endif

private:
    struct Slot
    {
        std::string id;
        std::shared_ptr<PieceStore> store;
    };

    lt::io_context& m_ioc;
    std::shared_ptr<MemoryRegistry> m_registry;
    lt::aux::vector<Slot, lt::storage_index_t> m_slots;
    std::vector<lt::storage_index_t> m_free;
};

} // namespace

lt::disk_io_constructor_type MemoryRegistry::constructor(std::shared_ptr<MemoryRegistry> self)
{
    return [self](lt::io_context& ioc, const lt::settings_interface&, lt::counters&) {
        return std::unique_ptr<lt::disk_interface>(new MemoryDiskIo(ioc, self));
    };
}

} // namespace cj::broker_detail
