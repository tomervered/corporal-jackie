// Diagnostic for Windows CI: do Boost.Asio timers keep firing, and does
// tearing an io_context down with timers pending end cleanly? libtorrent
// starts its connections from timers, so a timer that never fires shows up
// as "no metadata" in the broker tests.
#include <boost/asio.hpp>
#include <boost/version.hpp>

#include <chrono>
#include <cstdio>
#include <functional>
#include <thread>

using namespace std::chrono_literals;

namespace {

// One io_context with a timer that re-arms itself; true if it fired five times.
bool fiveTimers()
{
    boost::asio::io_context io;
    boost::asio::steady_timer timer(io);
    int fired = 0;
    std::function<void(const boost::system::error_code&)> again = [&](const boost::system::error_code& ec) {
        if (!ec && ++fired < 5) {
            timer.expires_after(20ms);
            timer.async_wait(again);
        } else {
            io.stop();
        }
    };
    timer.expires_after(20ms);
    timer.async_wait(again);
    io.run_for(2s);
    return fired >= 5;
}

} // namespace

int main()
{
    std::printf("Boost %s\n", BOOST_LIB_VERSION);
    int late = 0;
    for (int i = 0; i < 300; ++i) {
        // On the main thread, then with the io_context run from another thread
        // (libtorrent runs its network thread that way).
        if (!fiveTimers())
            ++late;
        if (i % 10 == 0) {
            boost::asio::io_context io;
            boost::asio::steady_timer pending(io);
            pending.expires_after(1h);
            pending.async_wait([](const boost::system::error_code&) {});
            std::thread worker([&io] { io.run(); });
            std::this_thread::sleep_for(30ms);
            io.stop();
            worker.join();
        }
    }
    std::printf("timers that did not finish: %d of 300\n", late);
    return late == 0 ? 0 : 1;
}
