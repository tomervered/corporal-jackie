// Diagnostic for the Windows CI tests that never get metadata from a seeder
// on 127.0.0.1 ("No metadata ... 0 peers"). Each round starts a seeder set up
// like the test harness and a client set up like the broker, hands the
// client the seeder's port in a magnet link (x.pe), and waits for the
// metadata. Both sessions log every alert; a round that gets no metadata in
// time prints both logs, so the failing connection shows its error.
//
// Usage: loopback_probe [rounds=200] [timeout ms=15000] [busy threads=0] [reuse seeder=0]
#include <libtorrent/alert_types.hpp>
#include <libtorrent/create_torrent.hpp>
#include <libtorrent/load_torrent.hpp>
#include <libtorrent/magnet_uri.hpp>
#include <libtorrent/session.hpp>
#include <libtorrent/session_params.hpp>
#include <libtorrent/settings_pack.hpp>
#include <libtorrent/torrent_info.hpp>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <vector>

using namespace std::chrono_literals;
using Clock = std::chrono::steady_clock;

namespace {

const lt::alert_category_t kAll = lt::alert_category::all;

struct Logged
{
    std::unique_ptr<lt::session> session;
    std::vector<std::string> lines;
    Clock::time_point start = Clock::now();
    bool acceptFailed = false;
    bool listenFailed = false;
    bool gotMetadata = false;

    void drain(const char* who)
    {
        std::vector<lt::alert*> alerts;
        session->pop_alerts(&alerts);
        for (lt::alert* a : alerts) {
            const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count();
            lines.push_back(std::to_string(ms) + " ms " + who + " [" + a->what() + "] " + a->message());
            if (auto* lf = lt::alert_cast<lt::listen_failed_alert>(a)) {
                listenFailed = true;
                if (lf->op == lt::operation_t::sock_accept)
                    acceptFailed = true;
            }
            if (lt::alert_cast<lt::metadata_received_alert>(a))
                gotMetadata = true;
        }
    }
};

lt::settings_pack common()
{
    lt::settings_pack pack;
    pack.set_str(lt::settings_pack::listen_interfaces, "127.0.0.1:0");
    pack.set_bool(lt::settings_pack::enable_dht, false);
    pack.set_bool(lt::settings_pack::enable_lsd, false);
    pack.set_bool(lt::settings_pack::enable_upnp, false);
    pack.set_bool(lt::settings_pack::enable_natpmp, false);
    pack.set_bool(lt::settings_pack::allow_multiple_connections_per_ip, true);
    pack.set_int(lt::settings_pack::alert_mask, kAll);
    pack.set_int(lt::settings_pack::alert_queue_size, 100000);
    pack.set_int(lt::settings_pack::tick_interval, 100);
    return pack;
}

// As the broker sets it up (BrokerEngine.cpp), loopback only.
lt::settings_pack clientSettings()
{
    lt::settings_pack pack = common();
    pack.set_bool(lt::settings_pack::enable_outgoing_utp, false);
    pack.set_bool(lt::settings_pack::enable_incoming_utp, true);
    pack.set_bool(lt::settings_pack::close_redundant_connections, false);
    pack.set_int(lt::settings_pack::request_timeout, 10);
    pack.set_int(lt::settings_pack::request_queue_time, 1);
    pack.set_int(lt::settings_pack::whole_pieces_threshold, 0);
    pack.set_int(lt::settings_pack::piece_timeout, 5);
    pack.set_bool(lt::settings_pack::strict_end_game_mode, false);
    pack.set_int(lt::settings_pack::connection_speed, 100);
    pack.set_int(lt::settings_pack::torrent_connect_boost, 60);
    pack.set_int(lt::settings_pack::stop_tracker_timeout, 1);
    return pack;
}

std::shared_ptr<lt::torrent_info> makeTorrent(const std::filesystem::path& root)
{
    std::filesystem::create_directories(root);
    const auto file = root / "movie.mkv";
    {
        std::mt19937 rng(7);
        std::ofstream out(file, std::ios::binary);
        for (int i = 0; i < 4 * 1024 * 1024 / 4; ++i) {
            const auto w = rng();
            out.write(reinterpret_cast<const char*>(&w), 4);
        }
    }
    lt::create_torrent ct(lt::list_files(file.string()), 256 * 1024, lt::create_torrent::v1_only);
    lt::set_piece_hashes(ct, root.string());
    std::vector<char> buffer;
    lt::bencode(std::back_inserter(buffer), ct.generate());
    return lt::load_torrent_buffer(buffer).ti;
}

std::unique_ptr<Logged> startSeeder(const std::shared_ptr<lt::torrent_info>& ti, const std::string& root)
{
    auto s = std::make_unique<Logged>();
    s->session = std::make_unique<lt::session>(lt::session_params(common()));
    lt::add_torrent_params atp;
    atp.ti = std::make_shared<lt::torrent_info>(*ti);
    atp.save_path = root;
    atp.flags |= lt::torrent_flags::seed_mode;
    atp.flags &= ~(lt::torrent_flags::auto_managed | lt::torrent_flags::paused);
    s->session->add_torrent(std::move(atp));
    return s;
}

int waitPort(lt::session& s)
{
    for (int i = 0; i < 100 && s.listen_port() == 0; ++i)
        std::this_thread::sleep_for(20ms);
    return s.listen_port();
}

} // namespace

int main(int argc, char** argv)
{
    const int rounds = argc > 1 ? std::atoi(argv[1]) : 200;
    const int timeoutMs = argc > 2 ? std::atoi(argv[2]) : 15000;
    const int busy = argc > 3 ? std::atoi(argv[3]) : 0;
    const bool reuseSeeder = argc > 4 && std::atoi(argv[4]) != 0;

    std::atomic<bool> stop{false};
    std::vector<std::thread> hogs;
    for (int i = 0; i < busy; ++i)
        hogs.emplace_back([&stop] {
            volatile unsigned long x = 0;
            while (!stop)
                x = x + 1;
        });

    const auto root = std::filesystem::temp_directory_path()
        / ("loopback_probe-" + std::to_string(std::random_device{}()));
    const auto ti = makeTorrent(root / "seed");

    std::unique_ptr<Logged> keptSeeder;
    int failures = 0, zeroPorts = 0, acceptFailures = 0;
    long long slowest = 0;
    for (int r = 0; r < rounds; ++r) {
        std::unique_ptr<Logged> seeder = reuseSeeder && keptSeeder ? std::move(keptSeeder)
                                                                   : startSeeder(ti, (root / "seed").string());
        const int port = waitPort(*seeder->session);
        if (port == 0)
            ++zeroPorts;

        Logged client;
        client.start = seeder->start = Clock::now();
        client.session = std::make_unique<lt::session>(lt::session_params(clientSettings()));
        lt::add_torrent_params atp;
        atp.ti = ti;
        atp.name = ti->name();
        std::string uri = lt::make_magnet_uri(atp) + "&x.pe=127.0.0.1:" + std::to_string(port);
        lt::add_torrent_params p = lt::parse_magnet_uri(uri);
        p.save_path = (root / ("client-" + std::to_string(r))).string();
        p.flags &= ~(lt::torrent_flags::auto_managed | lt::torrent_flags::paused);
        client.session->add_torrent(std::move(p));

        const auto deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);
        while (!client.gotMetadata && Clock::now() < deadline) {
            client.session->wait_for_alert(50ms);
            client.drain("client");
            seeder->drain("seeder");
        }
        const long long took = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - client.start).count();
        slowest = std::max(slowest, client.gotMetadata ? took : 0LL);
        if (seeder->acceptFailed)
            ++acceptFailures;
        if (!client.gotMetadata || seeder->listenFailed || client.listenFailed) {
            if (!client.gotMetadata)
                ++failures;
            std::printf("=== round %d: %s, seeder port %d, %lld ms\n", r,
                        client.gotMetadata ? "metadata, but a listen error" : "NO METADATA", port, took);
            std::vector<std::string> all = seeder->lines;
            all.insert(all.end(), client.lines.begin(), client.lines.end());
            for (const auto& line : all)
                std::printf("  %s\n", line.c_str());
            std::fflush(stdout);
        }
        if (reuseSeeder && !seeder->acceptFailed) {
            seeder->lines.clear();
            keptSeeder = std::move(seeder);
        }
        if ((r + 1) % 25 == 0) {
            std::printf("%d rounds: %d without metadata, %d seeder ports 0, %d accept failures, slowest %lld ms\n",
                        r + 1, failures, zeroPorts, acceptFailures, slowest);
            std::fflush(stdout);
        }
    }
    stop = true;
    for (auto& t : hogs)
        t.join();
    keptSeeder.reset();
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::printf("done: %d rounds, %d without metadata, %d seeder ports 0, %d accept failures\n", rounds, failures,
                zeroPorts, acceptFailures);
    return failures > 0 ? 1 : 0;
}
