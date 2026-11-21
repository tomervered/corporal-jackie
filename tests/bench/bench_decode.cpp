// The decoding side of the playback benchmark: libmpv, headless (no window,
// no sound), decoding the bundled clip as fast as it can. It answers "did a
// change to mpv, FFmpeg or their build flags make playback slower or
// heavier?". Run it with the old and the new libraries and compare the two
// outputs with tests/bench/compare_decode.py.
//
//   bench_decode <clip> [loops] [seeks]
//
// Prints one JSON object on stdout:
//   decode_seconds, decode_fps   wall time and frames per second over `loops` plays
//   cpu_seconds                  process CPU time over the whole run
//   peak_bytes                   the most memory the process held
//   seek_ms_median / _max        time from a seek to the first frame after it
//   libs                         what mpv and FFmpeg report about themselves

#include <mpv/client.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#ifdef _WIN32
#  include <windows.h>
#  include <psapi.h>
#else
#  include <sys/resource.h>
#  include <cstring>
#endif

namespace {

using Clock = std::chrono::steady_clock;

double secondsSince(Clock::time_point t)
{
    return std::chrono::duration<double>(Clock::now() - t).count();
}

double cpuSeconds()
{
#ifdef _WIN32
    FILETIME c, e, k, u;
    if (!GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &u))
        return -1;
    auto v = [](FILETIME f) {
        ULARGE_INTEGER u;
        u.LowPart = f.dwLowDateTime;
        u.HighPart = f.dwHighDateTime;
        return double(u.QuadPart) / 1e7;
    };
    return v(k) + v(u);
#else
    rusage r{};
    getrusage(RUSAGE_SELF, &r);
    return double(r.ru_utime.tv_sec + r.ru_stime.tv_sec) + double(r.ru_utime.tv_usec + r.ru_stime.tv_usec) / 1e6;
#endif
}

long long peakBytes()
{
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS pmc{};
    return GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc)) ? (long long)pmc.PeakWorkingSetSize : -1;
#else
    rusage r{};
    getrusage(RUSAGE_SELF, &r);
#  ifdef __APPLE__
    return r.ru_maxrss;
#  else
    return (long long)r.ru_maxrss * 1024;
#  endif
#endif
}

// Waits for `wanted`; false on a timeout or when playback ends without it.
bool waitFor(mpv_handle* h, mpv_event_id wanted, double timeout)
{
    const auto start = Clock::now();
    while (secondsSince(start) < timeout) {
        mpv_event* ev = mpv_wait_event(h, 0.2);
        if (ev->event_id == wanted)
            return true;
        if (ev->event_id == MPV_EVENT_SHUTDOWN)
            return false;
        if (ev->event_id == MPV_EVENT_END_FILE && wanted != MPV_EVENT_END_FILE)
            return false;
    }
    return false;
}

std::string property(mpv_handle* h, const char* name)
{
    char* s = mpv_get_property_string(h, name);
    std::string out = s ? s : "";
    if (s)
        mpv_free(s);
    return out;
}

std::string jsonEscape(const std::string& in)
{
    std::string out;
    for (char c : in) {
        if (c == '"' || c == '\\') {
            out += '\\';
            out += c;
        } else if (c == '\n' || c == '\r' || c == '\t') {
            out += ' ';
        } else {
            out += c;
        }
    }
    return out;
}

mpv_handle* makeMpv(int loops)
{
    mpv_handle* h = mpv_create();
    if (!h)
        return nullptr;
    mpv_set_option_string(h, "config", "no");
    mpv_set_option_string(h, "terminal", "no");
    mpv_set_option_string(h, "vo", "null");
    mpv_set_option_string(h, "ao", "null");
    mpv_set_option_string(h, "audio", "no");
    mpv_set_option_string(h, "hwdec", "no");           // software decoding: the same on every runner
    mpv_set_option_string(h, "untimed", "yes");        // no frame pacing: decode as fast as possible
    mpv_set_option_string(h, "input-default-bindings", "no");
    mpv_set_option_string(h, "idle", "yes");
    mpv_set_option_string(h, "keep-open", "no");
    mpv_set_option_string(h, "loop-file", std::to_string(loops - 1).c_str());
    if (mpv_initialize(h) < 0) {
        mpv_terminate_destroy(h);
        return nullptr;
    }
    return h;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 2) {
        std::fprintf(stderr, "usage: bench_decode <clip> [loops] [seeks]\n");
        return 2;
    }
    const std::string clip = argv[1];
    const int loops = argc > 2 ? std::max(1, std::atoi(argv[2])) : 30;
    const int seeks = argc > 3 ? std::max(1, std::atoi(argv[3])) : 40;

    // Phase 1: decode `loops` plays of the clip, back to back.
    mpv_handle* h = makeMpv(loops);
    if (!h) {
        std::fprintf(stderr, "mpv did not start\n");
        return 1;
    }
    const std::string version = property(h, "mpv-version");
    const std::string configuration = property(h, "mpv-configuration");
    const std::string ffmpeg = property(h, "ffmpeg-version");

    const char* load[] = {"loadfile", clip.c_str(), nullptr};
    const auto decodeStart = Clock::now();
    if (mpv_command(h, load) < 0 || !waitFor(h, MPV_EVENT_FILE_LOADED, 30)) {
        std::fprintf(stderr, "could not open %s\n", clip.c_str());
        return 1;
    }
    double frames = std::atof(property(h, "estimated-frame-count").c_str()) * loops;
    if (!waitFor(h, MPV_EVENT_END_FILE, 300)) {
        std::fprintf(stderr, "decoding did not finish\n");
        return 1;
    }
    const double decodeSeconds = secondsSince(decodeStart);
    mpv_terminate_destroy(h);

    // Phase 2: seeks. Paused, so each one ends at its first frame.
    h = makeMpv(1);
    if (!h)
        return 1;
    mpv_set_option_string(h, "pause", "yes");
    mpv_set_property_string(h, "pause", "yes");
    if (mpv_command(h, load) < 0 || !waitFor(h, MPV_EVENT_PLAYBACK_RESTART, 30)) {
        std::fprintf(stderr, "could not open %s for seeking\n", clip.c_str());
        return 1;
    }
    const double duration = std::atof(property(h, "duration").c_str());
    std::vector<double> ms;
    unsigned x = 12345;  // a fixed sequence, so every run asks for the same places
    for (int i = 0; i < seeks; ++i) {
        x = x * 1103515245u + 12345u;
        const double pos = duration > 0 ? (x >> 8) % 1000 / 1000.0 * (duration - 0.5) : 0;
        const std::string target = std::to_string(pos);
        const char* seek[] = {"seek", target.c_str(), "absolute+exact", nullptr};
        const auto t = Clock::now();
        if (mpv_command(h, seek) < 0 || !waitFor(h, MPV_EVENT_PLAYBACK_RESTART, 10))
            continue;
        ms.push_back(secondsSince(t) * 1000);
    }
    mpv_terminate_destroy(h);
    std::sort(ms.begin(), ms.end());

    std::printf("{\"decode_seconds\":%.3f,\"decode_fps\":%.1f,\"cpu_seconds\":%.3f,\"peak_bytes\":%lld,"
                "\"seek_count\":%zu,\"seek_ms_median\":%.2f,\"seek_ms_max\":%.2f,"
                "\"libs\":{\"mpv\":\"%s\",\"ffmpeg\":\"%s\",\"mpv_configuration\":\"%s\"}}\n",
                decodeSeconds, decodeSeconds > 0 ? frames / decodeSeconds : 0.0, cpuSeconds(), peakBytes(), ms.size(),
                ms.empty() ? -1.0 : ms[ms.size() / 2], ms.empty() ? -1.0 : ms.back(),
                jsonEscape(version).c_str(), jsonEscape(ffmpeg).c_str(), jsonEscape(configuration).c_str());
    return ms.empty() ? 1 : 0;
}
