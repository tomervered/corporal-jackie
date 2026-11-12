// Diagnostic for the Windows CI crash in Boost.Asio's timer thread (an
// access violation writing io_context::dispatch_required_ after the
// io_context was freed). Starts and ends many libtorrent sessions the way
// the broker does, while other threads keep every core busy, and ends each
// one at a random moment: right after the first timers are armed, while a
// tracker host name is being resolved, or while peers connect. A crash or
// hang here points at libtorrent's or Asio's teardown, not at the broker.
//
// Usage: session_churn_probe [sessions=400] [busy threads=cores] [seed=1]
#include <libtorrent/add_torrent_params.hpp>
#include <libtorrent/magnet_uri.hpp>
#include <libtorrent/session.hpp>
#include <libtorrent/session_params.hpp>
#include <libtorrent/settings_pack.hpp>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <thread>
#include <vector>

using namespace std::chrono_literals;

namespace {

lt::settings_pack makeSettings()
{
    lt::settings_pack pack;
    pack.set_str(lt::settings_pack::listen_interfaces, "127.0.0.1:0");
    pack.set_bool(lt::settings_pack::enable_dht, false);
    pack.set_bool(lt::settings_pack::enable_lsd, false);
    pack.set_bool(lt::settings_pack::enable_upnp, false);
    pack.set_bool(lt::settings_pack::enable_natpmp, false);
    pack.set_int(lt::settings_pack::alert_mask, 0);
    pack.set_int(lt::settings_pack::tick_interval, 100);
    pack.set_int(lt::settings_pack::stop_tracker_timeout, 1);
    return pack;
}

} // namespace

int main(int argc, char** argv)
{
    const int sessions = argc > 1 ? std::atoi(argv[1]) : 400;
    const unsigned cores = std::max(1u, std::thread::hardware_concurrency());
    const unsigned busy = argc > 2 ? unsigned(std::atoi(argv[2])) : cores;
    std::mt19937 rng(argc > 3 ? unsigned(std::atoi(argv[3])) : 1u);

    std::atomic<bool> stop{false};
    std::vector<std::thread> hogs;
    for (unsigned i = 0; i < busy; ++i)
        hogs.emplace_back([&stop] {
            volatile unsigned long x = 0;
            while (!stop)
                x = x + 1;
        });

    for (int i = 0; i < sessions; ++i) {
        {
            auto session = std::make_unique<lt::session>(lt::session_params(makeSettings()));
            // A tracker name to resolve, a dead peer to connect to, and a
            // second torrent so there is more than one timer in flight.
            for (int t = 0; t < 2; ++t) {
                lt::error_code ec;
                lt::add_torrent_params atp = lt::parse_magnet_uri(
                    "magnet:?xt=urn:btih:" + std::string(t == 0 ? "3ed8def0cc5fc7055eb989a1d982e981d561ce3c"
                                                                : "54346a99e70cbc87aad5f36f2780011f9f054e49")
                        + "&tr=udp://tracker.invalid:6969/announce&tr=http://localhost:1/announce&x.pe=127.0.0.1:1",
                    ec);
                atp.save_path = ".";
                session->add_torrent(std::move(atp), ec);
            }
            std::this_thread::sleep_for(std::chrono::microseconds(rng() % 200000));
            // As Engine::shutdown does it.
            if (i % 2 == 0) {
                lt::session_proxy proxy = session->abort();
                session.reset();
            } else {
                session.reset();
            }
        }
        if (i % 50 == 0) {
            std::printf("%d of %d sessions ended cleanly\n", i, sessions);
            std::fflush(stdout);
        }
    }
    stop = true;
    for (auto& h : hogs)
        h.join();
    std::printf("done: %d sessions\n", sessions);
    return 0;
}
