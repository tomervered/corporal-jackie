#pragma once
// Keeps the engine from uploading while uploads are off.
//
// Unchoking nobody (unchoke_slots_limit 0) is not enough on its own:
// libtorrent still unchokes one peer at a time "optimistically", and peers
// may ask for their "allowed fast" pieces while choked. This session plugin
// sees every request a peer sends and, while uploads are off, chokes that
// peer and drops the request, so no piece data is ever sent.

#include <libtorrent/extensions.hpp>
#include <libtorrent/peer_connection_handle.hpp>
#include <libtorrent/settings_pack.hpp>

#include <atomic>
#include <memory>

namespace cj::broker_detail {

class UploadGuard : public lt::plugin
{
public:
    explicit UploadGuard(bool allowed) : m_allowed(std::make_shared<std::atomic<bool>>(allowed)) {}

    void setAllowed(bool on) { m_allowed->store(on); }
    bool allowed() const { return m_allowed->load(); }

    // The settings that match: no unchoke slots and no allowed-fast pieces
    // while uploads are off, libtorrent's defaults while on.
    static void apply(lt::settings_pack& pack, bool allowed)
    {
        pack.set_int(lt::settings_pack::unchoke_slots_limit, allowed ? 8 : 0);
        pack.set_int(lt::settings_pack::allowed_fast_set_size, allowed ? 5 : 0);
    }

    std::shared_ptr<lt::torrent_plugin> new_torrent(const lt::torrent_handle&, lt::client_data_t) override
    {
        return std::make_shared<Torrent>(m_allowed);
    }

private:
    using Flag = std::shared_ptr<std::atomic<bool>>;

    struct Peer : lt::peer_plugin
    {
        Peer(const lt::peer_connection_handle& pc, Flag allowed) : m_pc(pc), m_allowed(std::move(allowed)) {}
        bool on_request(const lt::peer_request&) override
        {
            if (m_allowed->load())
                return false;
            m_pc.choke_this_peer();
            return true;  // handled: libtorrent does not read or send the block
        }
        // An optimistic unchoke, or one from before uploads were turned off.
        void tick() override
        {
            if (!m_allowed->load() && !m_pc.is_choked())
                m_pc.choke_this_peer();
        }
        lt::peer_connection_handle m_pc;
        Flag m_allowed;
    };

    struct Torrent : lt::torrent_plugin
    {
        explicit Torrent(Flag allowed) : m_allowed(std::move(allowed)) {}
        std::shared_ptr<lt::peer_plugin> new_connection(const lt::peer_connection_handle& pc) override
        {
            return std::make_shared<Peer>(pc, m_allowed);
        }
        Flag m_allowed;
    };

    Flag m_allowed;
};

} // namespace cj::broker_detail
