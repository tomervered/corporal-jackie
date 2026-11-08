// The player side of the torrent benchmark, in a process of its own so its
// peak memory is the player's alone (the seeder runs in tst_bench).
//
//   bench_player <magnet> <cache dir> [ram cap in bytes]
//
// Opens the magnet, reads the start, plays a stretch at a steady rate, then
// seeks to fixed places and times each one. Each seek is labelled by what the
// broker held there just before: cached, never downloaded, or evicted (RAM
// only: read before, dropped since). Prints one JSON object on stdout.

#include "StreamBroker.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <atomic>
#include <cstdio>
#include <set>
#include <thread>

#ifdef Q_OS_WIN
#  include <windows.h>
#  include <psapi.h>
#elif defined(Q_OS_MACOS)
#  include <sys/resource.h>
#else
#  include <QFile>
#endif

using namespace cj;

namespace {

constexpr qint64 MiB = 1024 * 1024;
constexpr qint64 kChunk = 256 * 1024;

// The most memory this process has held at once, in bytes.
qint64 peakProcessMemory()
{
#ifdef Q_OS_WIN
    PROCESS_MEMORY_COUNTERS pmc{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc)))
        return qint64(pmc.PeakWorkingSetSize);
    return -1;
#elif defined(Q_OS_MACOS)
    rusage u{};
    getrusage(RUSAGE_SELF, &u);
    return qint64(u.ru_maxrss);  // bytes on macOS
#else
    QFile f(QStringLiteral("/proc/self/status"));
    if (!f.open(QIODevice::ReadOnly))
        return -1;
    for (const QByteArray& line : f.readAll().split('\n'))
        if (line.startsWith("VmHWM:"))
            return line.mid(6).trimmed().split(' ').value(0).toLongLong() * 1024;
    return -1;
#endif
}

// For the watchdog: when a read last returned, and where the player waits.
QElapsedTimer g_clock;
std::atomic<qint64> g_progressAt{0};
std::atomic<qint64> g_waitingAt{-1};
std::atomic<const char*> g_phase{"opening"};

// Reads `length` bytes at `offset`, across piece boundaries. False on error.
bool readFully(StreamHandle& h, qint64 offset, qint64 length)
{
    static thread_local QByteArray buffer(int(kChunk), Qt::Uninitialized);
    while (length > 0) {
        g_waitingAt = offset;
        const qint64 n = h.read(offset, buffer.data(), qMin<qint64>(length, buffer.size()));
        g_progressAt = g_clock.elapsed();
        if (n <= 0)
            return false;
        offset += n;
        length -= n;
    }
    return true;
}

const char* stateName(StreamState s)
{
    switch (s) {
    case StreamState::FindingPeers: return "finding peers";
    case StreamState::FetchingMetadata: return "fetching metadata";
    case StreamState::Buffering: return "buffering";
    case StreamState::Ready: return "ready";
    case StreamState::Stalled: return "stalled";
    }
    return "?";
}

const char* pieceName(PieceState s)
{
    switch (s) {
    case PieceState::Missing: return "missing";
    case PieceState::Held: return "held";
    case PieceState::Evicted: return "evicted";
    }
    return "?";
}

const char* kindName(PieceState s, bool readBefore)
{
    if (s == PieceState::Held)
        return "cached";
    if (s == PieceState::Evicted || readBefore)
        return "evicted";
    return "missing";
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    const QStringList args = app.arguments();
    if (args.size() < 3) {
        std::fprintf(stderr, "usage: bench_player <magnet> <cache dir> [ram cap bytes]\n");
        return 2;
    }

    BrokerSettings settings;
    settings.cacheDir = args[2];
    settings.loopbackOnly = true;
    settings.metadataTimeoutMs = 30000;
    if (args.size() > 3) {
        settings.memoryOnly = true;
        settings.memoryCapBytes = args[3].toLongLong();
    }

    // Sample the broker's own counters in the background, for their peaks.
    // A read stuck for a minute ends the run with what the broker saw, so a
    // hang shows its cause instead of a timeout.
    g_clock.start();
    StreamBroker broker(settings);
    std::atomic<bool> done{false};
    std::atomic<qint64> peakStore{0};
    std::atomic<qint64> peakReadCache{0};
    std::atomic<StreamHandle*> handle{nullptr};
    std::thread sampler([&] {
        while (!done) {
            peakStore = qMax(peakStore.load(), broker.memoryUsed());
            peakReadCache = qMax(peakReadCache.load(), broker.readCacheBytes());
            if (g_clock.elapsed() - g_progressAt > 60000) {
                const qint64 at = g_waitingAt;
                std::fprintf(stderr, "bench_player: stuck for 60 s while %s, at byte %lld\n", g_phase.load(),
                             static_cast<long long>(at));
                if (StreamHandle* hp = handle.load()) {
                    const StreamStats st = hp->stats();
                    std::fprintf(stderr,
                                 "bench_player: state %s, %d peers, %lld KiB/s, piece there %s, "
                                 "store %lld MiB, read cache %lld MiB\n",
                                 stateName(st.state), st.peers, static_cast<long long>(st.downloadRate / 1024),
                                 pieceName(hp->stateAt(at)), static_cast<long long>(broker.memoryUsed() / MiB),
                                 static_cast<long long>(broker.readCacheBytes() / MiB));
                }
                std::fflush(stderr);
                std::_Exit(3);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    });
    auto fail = [&](const char* what) {
        done = true;
        sampler.join();
        handle = nullptr;
        std::fprintf(stderr, "bench_player: %s\n", what);
        return 1;
    };

    QJsonObject out;
    QElapsedTimer clock;
    clock.start();

    OpenRequest req;
    req.uri = args[1];
    BrokerError error = BrokerError::None;
    const std::shared_ptr<StreamHandle> h = broker.open(req, &error);
    if (!h)
        return fail("open failed");
    handle = h.get();
    g_phase = "reading the start";
    out[QStringLiteral("openMs")] = clock.elapsed();

    // The player's first read: the container's headers.
    if (!readFully(*h, 0, 64 * 1024))
        return fail("first read failed");
    out[QStringLiteral("firstByteMs")] = clock.elapsed();

    // Play the first stretch at the file's bitrate, as a player would, so
    // the broker sees a reader moving and (in RAM) drops what lies behind.
    const qint64 size = h->size();
    const double bitrate = 8.0 * MiB;  // bytes/s: fast enough to keep the test short
    h->setDuration(double(size) / bitrate);
    const qint64 played = qMin<qint64>(size / 2, 48 * MiB);
    std::set<qint64> readChunks;
    g_phase = "playing";
    QElapsedTimer playClock;
    playClock.start();
    qint64 stallMs = 0;
    int stalls = 0;
    qint64 longestStallMs = 0;
    for (qint64 pos = 0; pos < played; pos += kChunk) {
        const qint64 due = qint64(double(pos) / bitrate * 1000);
        const qint64 now = playClock.elapsed();
        if (now < due)
            std::this_thread::sleep_for(std::chrono::milliseconds(due - now));
        QElapsedTimer wait;
        wait.start();
        if (!readFully(*h, pos, kChunk))
            return fail("playback read failed");
        // A read that took longer than a frame's slack is time the picture froze.
        if (const qint64 w = wait.elapsed(); w > 50) {
            stallMs += w;
            ++stalls;
            longestStallMs = qMax(longestStallMs, w);
        }
        readChunks.insert(pos / kChunk);
    }
    out[QStringLiteral("playedBytes")] = played;
    out[QStringLiteral("playStallMs")] = stallMs;
    out[QStringLiteral("playStalls")] = stalls;
    out[QStringLiteral("longestStallMs")] = longestStallMs;

    // Fixed seeks: back to the start, behind, at and ahead of the reader,
    // and far ahead. Fractions of the file, so any file size works.
    const double targets[] = {0.02, 0.45, 0.10, 0.55, 0.30, 0.90, 0.05, 0.60, 0.20, 0.75,
                              0.40, 0.95, 0.15, 0.65, 0.35, 0.85, 0.25, 0.70, 0.48, 0.80};
    QJsonArray seeks;
    g_phase = "seeking";
    for (double f : targets) {
        const qint64 at = (qint64(double(size) * f) / kChunk) * kChunk;
        const PieceState before = h->stateAt(at);
        const char* kind = kindName(before, readChunks.count(at / kChunk) > 0);
        QElapsedTimer t;
        t.start();
        if (!readFully(*h, at, kChunk))
            return fail("seek read failed");
        QJsonObject s;
        s[QStringLiteral("at")] = at;
        s[QStringLiteral("kind")] = QString::fromLatin1(kind);
        s[QStringLiteral("ms")] = t.elapsed();
        seeks.append(s);
        readChunks.insert(at / kChunk);
        // A viewer watches a little after each seek.
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    out[QStringLiteral("seeks")] = seeks;

    done = true;
    sampler.join();
    handle = nullptr;
    out[QStringLiteral("ramOnly")] = settings.memoryOnly;
    out[QStringLiteral("memoryCap")] = settings.memoryOnly ? settings.memoryCapBytes : 0;
    out[QStringLiteral("peakStoreBytes")] = peakStore.load();
    out[QStringLiteral("peakReadCacheBytes")] = peakReadCache.load();
    out[QStringLiteral("peakProcessBytes")] = peakProcessMemory();
    out[QStringLiteral("totalMs")] = clock.elapsed();
    std::fputs(QJsonDocument(out).toJson(QJsonDocument::Compact).constData(), stdout);
    std::fputs("\n", stdout);
    return 0;
}
