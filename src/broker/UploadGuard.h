#pragma once
// Keeps the engine from uploading while uploads are off.
//
// Unchoking nobody (unchoke_slots_limit 0) is not enough on its own:
// libtorrent still unchokes one peer at a time "optimistically", and peers
// may ask for their "allowed fast" pieces while choked. This session plugin
// sees every request a peer sends and, while uploads are off, chokes that
// peer and drops the request, so no piece data is ever sent.
//
// One torrent can be allowed to share while the rest are not (a download the
// person chose to share): setSharing() with the id the torrent was added
// under, passed to libtorrent as add_torrent_params::userdata (a pointer to
// that std::string, read only while the torrent is being added).

#include <libtorrent/extensions.hpp>
#include <libtorrent/peer_connection_handle.hpp>
#include <libtorrent/settings_pack.hpp>

#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <string>

namespace cj::broker_detail {

class UploadGuard : public lt::plugin
{
public:
    explicit UploadGuard(bool allowed) : m_allowed(std::make_shared<std::atomic<bool>>(allowed)) {}

    void setAllowed(bool on) { m_allowed->store(on); }
    bool allowed() const { return m_allowed->load(); }

    // Lets one torrent share (or not) whatever the global switch says. Off
    // again: it follows the global switch.
    void setSharing(const std::string& id, bool on)
    {
        flagOf(id)->store(on);
        std::lock_guard lock(m_mutex);
        m_anyShared = false;
        for (const auto& [key, flag] : m_shares)
            m_anyShared = m_anyShared || flag->load();
    }
    // Whether any torrent shares on its own: unchoke slots are needed then.
    bool anySharing() const
    {
        std::lock_guard lock(m_mutex);
        return m_anyShared;
    }
    // Forgets a torrent's own choice (it was removed). A plugin still alive
    // keeps its flag until libtorrent drops it.
    void forget(const std::string& id)
    {
        std::lock_guard lock(m_mutex);
        m_shares.erase(id);
        m_anyShared = false;
        for (const auto& [key, flag] : m_shares)
            m_anyShared = m_anyShared || flag->load();
    }

    // The settings that match: no unchoke slots and no allowed-fast pieces
    // while uploads are off, libtorrent's defaults while on.
    static void apply(lt::settings_pack& pack, bool allowed)
    {
        pack.set_int(lt::settings_pack::unchoke_slots_limit, allowed ? 8 : 0);
        pack.set_int(lt::settings_pack::allowed_fast_set_size, allowed ? 5 : 0);
    }

    std::shared_ptr<lt::torrent_plugin> new_torrent(const lt::torrent_handle&, lt::client_data_t userdata) override
    {
        const std::string* id = userdata.get<std::string>();
        return std::make_shared<Torrent>(m_allowed, id ? flagOf(*id) : std::make_shared<std::atomic<bool>>(false));
    }

private:
    using Flag = std::shared_ptr<std::atomic<bool>>;

    Flag flagOf(const std::string& id)
    {
        std::lock_guard lock(m_mutex);
        Flag& f = m_shares[id];
        if (!f)
            f = std::make_shared<std::atomic<bool>>(false);
        return f;
    }

    struct Peer : lt::peer_plugin
    {
        Peer(const lt::peer_connection_handle& pc, Flag allowed, Flag own)
            : m_pc(pc), m_allowed(std::move(allowed)), m_own(std::move(own)) {}
        bool ok() const { return m_allowed->load() || m_own->load(); }
        bool on_request(const lt::peer_request&) override
        {
            if (ok())
                return false;
            m_pc.choke_this_peer();
            return true;  // handled: libtorrent does not read or send the block
        }
        // An optimistic unchoke, or one from before uploads were turned off.
        void tick() override
        {
            if (!ok() && !m_pc.is_choked())
                m_pc.choke_this_peer();
        }
        lt::peer_connection_handle m_pc;
        Flag m_allowed;
        Flag m_own;
    };

    struct Torrent : lt::torrent_plugin
    {
        Torrent(Flag allowed, Flag own) : m_allowed(std::move(allowed)), m_own(std::move(own)) {}
        std::shared_ptr<lt::peer_plugin> new_connection(const lt::peer_connection_handle& pc) override
        {
            return std::make_shared<Peer>(pc, m_allowed, m_own);
        }
        Flag m_allowed;
        Flag m_own;
    };

    Flag m_allowed;
    mutable std::mutex m_mutex;
    std::map<std::string, Flag> m_shares;
    bool m_anyShared = false;
};

} // namespace cj::broker_detail
