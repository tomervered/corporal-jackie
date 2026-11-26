#include "BrokerEngine.h"

namespace cj {
namespace broker_detail {

// Adds the open trackers to a public torrent, or takes them off a private one
// or when the user turned them off. t.mutex must be held and the metadata
// known. Off removes every tracker in the built-in list, even one the link
// also names: the choice is about the app's own trackers, and a link that
// needs its tracker still has the DHT, its other trackers and peers.
void Engine::syncOpenTrackers(Torrent& t)
{
    if (!t.info)
        return;
    if (t.info->priv() || !publicTrackers) {
        lt::add_torrent_params own;
        for (const lt::announce_entry& e : t.handle.trackers()) {
            own.trackers.push_back(e.url);
            own.tracker_tiers.push_back(e.tier);
        }
        const std::size_t before = own.trackers.size();
        dropExtraTrackers(own);
        if (own.trackers.size() != before) {
            std::vector<lt::announce_entry> keep;
            for (std::size_t i = 0; i < own.trackers.size(); ++i) {
                lt::announce_entry e(own.trackers[i]);
                e.tier = std::uint8_t(own.tracker_tiers[i]);
                keep.push_back(e);
            }
            t.handle.replace_trackers(keep);
            qCInfo(lcBroker).noquote() << QString::fromStdString(t.id)
                                       << (t.info->priv() ? "is private: open trackers removed" : "open trackers removed");
        }
    } else {
        for (const std::string& tracker : extraTrackers)
            t.handle.add_tracker(lt::announce_entry(tracker));
    }
}

void Engine::applyNetwork(const BrokerSettings& s)
{
    const bool trackersChanged = publicTrackers.exchange(s.publicTrackers) != s.publicTrackers;
    if (!settings.loopbackOnly && m_session) {
        lt::settings_pack pack;
        pack.set_bool(lt::settings_pack::enable_dht, s.dht);
        pack.set_bool(lt::settings_pack::enable_lsd, s.localDiscovery);
        pack.set_bool(lt::settings_pack::enable_upnp, s.portMapping);
        pack.set_bool(lt::settings_pack::enable_natpmp, s.portMapping);
        m_session->apply_settings(std::move(pack));
    }
    if (m_session) {
        // Before the settings, so no request slips through while they apply.
        if (!s.upload)
            uploadGuard->setAllowed(false);
        lt::settings_pack pack;
        UploadGuard::apply(pack, s.upload || uploadGuard->anySharing());
        m_session->apply_settings(std::move(pack));
        uploadGuard->setAllowed(s.upload);
    }
    qCInfo(lcBroker).noquote() << "Network choices: DHT" << s.dht << "LAN discovery" << s.localDiscovery
                               << "port mapping" << s.portMapping << "open trackers" << s.publicTrackers
                               << "uploads" << s.upload;
    if (!trackersChanged)
        return;
    std::vector<std::shared_ptr<Torrent>> torrents;
    {
        std::lock_guard lock(m_mutex);
        for (auto& [id, t] : m_torrents)
            torrents.push_back(t);
    }
    for (const auto& t : torrents) {
        std::lock_guard lock(t->mutex);
        if (t->hasMetadata)
            syncOpenTrackers(*t);
    }
}

void Engine::setSharing(const std::string& id, bool on)
{
    if (!on)
        uploadGuard->setSharing(id, false);
    if (m_session) {
        lt::settings_pack pack;
        UploadGuard::apply(pack, uploadGuard->allowed() || on || uploadGuard->anySharing());
        m_session->apply_settings(std::move(pack));
    }
    if (on)
        uploadGuard->setSharing(id, true);
    qCInfo(lcBroker) << "Sharing one torrent:" << (on ? "on" : "off");
}

void Engine::dropExtraTrackers(lt::add_torrent_params& atp) const
{
    const auto isExtra = [this](const std::string& url) {
        return std::find(extraTrackers.begin(), extraTrackers.end(), url) != extraTrackers.end()
            || std::find(std::begin(kPublicTrackers), std::end(kPublicTrackers), url) != std::end(kPublicTrackers);
    };
    std::vector<std::string> trackers;
    std::vector<int> tiers;
    for (std::size_t i = 0; i < atp.trackers.size(); ++i) {
        if (isExtra(atp.trackers[i]))
            continue;
        trackers.push_back(atp.trackers[i]);
        if (i < atp.tracker_tiers.size())
            tiers.push_back(atp.tracker_tiers[i]);
    }
    atp.trackers = std::move(trackers);
    atp.tracker_tiers = std::move(tiers);
}

} // namespace broker_detail
} // namespace cj
