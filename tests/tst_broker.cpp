// Stream broker tests against a local swarm (tests/TorrentHarness.h).
// Each test makes its own torrents and seeders in a temporary folder.

#include "StreamBroker.h"
#include "TorrentHarness.h"

#include <QDirIterator>
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <QtTest>

#include <algorithm>
#include <random>
#include <thread>

using namespace cj;
using harness::Seeder;
using harness::TestTorrent;

namespace {

// How long a random seek may take before its first bytes arrive, in the
// harness (one seeder limited to 8 MB/s). Requests already queued at the
// seeder and libtorrent's once-a-second tick put a typical seek at about
// 1 s here, with rare ones near 3 s. Real-world numbers come with Step 4.
constexpr qint64 kSeekMedianTargetMs = 1500;
constexpr qint64 kSeekWorstTargetMs = 4000;

constexpr int kPiece = 256 * 1024;
constexpr qint64 MiB = 1024 * 1024;

BrokerSettings testSettings(const QString& cacheDir)
{
    BrokerSettings s;
    s.cacheDir = cacheDir;
    s.loopbackOnly = true;
    s.metadataTimeoutMs = 15000;
    return s;
}

// Reads [offset, offset + length) through the handle, in 64 KiB chunks.
QByteArray readRange(StreamHandle& h, qint64 offset, qint64 length)
{
    QByteArray out;
    out.reserve(int(length));
    QByteArray chunk(64 * 1024, Qt::Uninitialized);
    while (length > 0) {
        const qint64 n = h.read(offset, chunk.data(), qMin<qint64>(chunk.size(), length));
        if (n <= 0)
            break;
        out.append(chunk.constData(), int(n));
        offset += n;
        length -= n;
    }
    return out;
}

} // namespace

class TestBroker : public QObject
{
    Q_OBJECT

private slots:
    void readAheadFollowsBitrateAndSpeed()
    {
        const qint64 piece = 4 * MiB;
        const double bitrate = 2 * MiB;  // a 16 Mbit/s video
        QCOMPARE(readAheadBytes(bitrate, 0, piece), qint64(bitrate * 30));
        QCOMPARE(readAheadBytes(bitrate, 10 * bitrate, piece), qint64(bitrate * 60));   // fast swarm
        QCOMPARE(readAheadBytes(bitrate, 0.5 * bitrate, piece), qint64(bitrate * 20));  // slow swarm
        QCOMPARE(readAheadBytes(1000, 0, piece), 4 * piece);                             // at least 4 pieces
        QCOMPARE(readAheadBytes(1e9, 1e10, piece), 512 * MiB);                           // at most 512 MB
        QCOMPARE(readAheadBytes(0, 0, piece), qint64(kDefaultBitrate * 30));             // unknown duration
    }

    void opensMagnetAndReadsTheWholeFile()
    {
        QTemporaryDir dir;
        const TestTorrent t = harness::makeTorrent(dir.filePath("seed"), "movie.mkv", {24 * MiB}, kPiece, 1);
        Seeder seeder(t, t.root);
        StreamBroker broker(testSettings(dir.filePath("cache")));

        BrokerError error;
        auto h = broker.open({harness::magnet(t, {seeder.port()})}, &error);
        QVERIFY2(h, "open failed");
        QCOMPARE(error, BrokerError::None);
        QCOMPARE(h->size(), 24 * MiB);
        QCOMPARE(h->fileName(), QStringLiteral("movie.mkv"));
        QTRY_VERIFY_WITH_TIMEOUT(h->stats().peers >= 1, 5000);

        QElapsedTimer timer;
        timer.start();
        const QByteArray data = readRange(*h, 0, h->size());
        qInfo("Read 24 MiB in %lld ms", timer.elapsed());
        QCOMPARE(data.size(), t.data[0].size());
        QVERIFY(data == t.data[0]);
        QCOMPARE(h->read(h->size(), nullptr, 10), 0);  // end of file

        QCOMPARE(h->stats().state, StreamState::Ready);
    }

    // A "4K" file: 96 MiB over 15 seconds is about 50 Mbit/s.
    void opensBigFileAndSeeksFast()
    {
        QTemporaryDir dir;
        const TestTorrent t = harness::makeTorrent(dir.filePath("seed"), "4k.mkv", {96 * MiB}, kPiece, 2);
        Seeder seeder(t, t.root, 8 * MiB);
        StreamBroker broker(testSettings(dir.filePath("cache")));

        OpenRequest req{harness::magnet(t, {seeder.port()})};
        req.durationSeconds = 15;
        QElapsedTimer timer;
        timer.start();
        auto h = broker.open(req);
        QVERIFY(h);
        // The first bytes and the index at the end, as a player reads them.
        QVERIFY(readRange(*h, 0, 64 * 1024) == t.data[0].left(64 * 1024));
        QVERIFY(readRange(*h, h->size() - 64 * 1024, 64 * 1024) == t.data[0].right(64 * 1024));
        qInfo("Open to first frame data: %lld ms", timer.elapsed());

        std::mt19937 rng(7);
        std::uniform_int_distribution<qint64> anywhere(0, h->size() - 64 * 1024);
        std::vector<qint64> times;
        for (int i = 0; i < 12; ++i) {
            const qint64 offset = anywhere(rng);
            timer.restart();
            const QByteArray got = readRange(*h, offset, 64 * 1024);
            times.push_back(timer.elapsed());
            QVERIFY(got == t.data[0].mid(int(offset), 64 * 1024));
        }
        std::sort(times.begin(), times.end());
        const qint64 median = times[times.size() / 2];
        const qint64 worst = times.back();
        qInfo("12 random seeks: median %lld ms, slowest %lld ms (targets %lld and %lld ms)", median, worst,
              kSeekMedianTargetMs, kSeekWorstTargetMs);
        QVERIFY(median <= kSeekMedianTargetMs);
        QVERIFY(worst <= kSeekWorstTargetMs);
    }

    void twoReadersAtOnce()
    {
        QTemporaryDir dir;
        const TestTorrent t = harness::makeTorrent(dir.filePath("seed"), "movie.mkv", {32 * MiB}, kPiece, 3);
        Seeder seeder(t, t.root, 16 * MiB);
        StreamBroker broker(testSettings(dir.filePath("cache")));
        const QString uri = harness::magnet(t, {seeder.port()});
        auto a = broker.open({uri});
        auto b = broker.open({uri});
        QVERIFY(a && b);

        QByteArray first, second;
        std::thread ta([&] { first = readRange(*a, 0, 8 * MiB); });
        std::thread tb([&] { second = readRange(*b, 20 * MiB, 8 * MiB); });
        ta.join();
        tb.join();
        QVERIFY(first == t.data[0].mid(0, int(8 * MiB)));
        QVERIFY(second == t.data[0].mid(int(20 * MiB), int(8 * MiB)));
    }

    void downloadsOnlyTheChosenFile()
    {
        QTemporaryDir dir;
        const TestTorrent t = harness::makeTorrent(dir.filePath("seed"), "pack", {8 * MiB, 8 * MiB, 8 * MiB}, kPiece, 4);
        Seeder seeder(t, t.root);
        const QString cache = dir.filePath("cache");
        StreamBroker broker(testSettings(cache));

        OpenRequest req{harness::magnet(t, {seeder.port()})};
        req.fileIndex = t.indexOf(QStringLiteral("file1.bin"));
        auto h = broker.open(req);
        QVERIFY(h);
        QCOMPARE(h->fileName(), QStringLiteral("file1.bin"));
        QVERIFY(readRange(*h, 0, h->size()) == t.data[1]);

        const QString content = QDir(cache).filePath(harness::infoHash(t) + "/pack");
        const qint64 other0 = StreamBroker::diskUsage(content + "/file0.bin");
        const qint64 other2 = StreamBroker::diskUsage(content + "/file2.bin");
        qInfo("Other files on disk: %lld and %lld bytes", other0, other2);
        // Files are piece-aligned here, so nothing of the others is needed.
        QVERIFY(other0 < kPiece);
        QVERIFY(other2 < kPiece);
    }

    void largestFileIsTheDefault()
    {
        QTemporaryDir dir;
        const TestTorrent t = harness::makeTorrent(dir.filePath("seed"), "pack", {1 * MiB, 6 * MiB, 2 * MiB}, kPiece, 5);
        Seeder seeder(t, t.root);
        StreamBroker broker(testSettings(dir.filePath("cache")));
        auto h = broker.open({harness::magnet(t, {seeder.port()})});
        QVERIFY(h);
        QCOMPARE(h->fileName(), QStringLiteral("file1.bin"));  // the 6 MiB one
        QCOMPARE(h->fileIndex(), t.indexOf(QStringLiteral("file1.bin")));

        BrokerError error;
        OpenRequest bad{harness::magnet(t, {seeder.port()})};
        bad.fileIndex = 9;
        QVERIFY(!broker.open(bad, &error));
        QCOMPARE(error, BrokerError::NoSuchFile);
    }

    void cancelUnblocksAWaitingRead()
    {
        QTemporaryDir dir;
        const TestTorrent t = harness::makeTorrent(dir.filePath("seed"), "movie.mkv", {16 * MiB}, kPiece, 6);
        Seeder seeder(t, t.root, 32 * 1024);  // a very slow swarm
        StreamBroker broker(testSettings(dir.filePath("cache")));
        auto h = broker.open({harness::magnet(t, {seeder.port()})});
        QVERIFY(h);

        qint64 result = 0;
        QElapsedTimer timer;
        std::thread reader([&] {
            QByteArray buf(1024, 0);
            result = h->read(10 * MiB, buf.data(), buf.size());
        });
        QTest::qWait(300);
        StreamStats stats = h->stats();
        h->setDuration(60);
        QVERIFY(stats.state == StreamState::Buffering || stats.state == StreamState::Ready);
        timer.start();
        h->cancel();
        reader.join();
        QCOMPARE(result, qint64(-1));
        QVERIFY(timer.elapsed() < 1000);
        QByteArray buf(16, 0);
        QCOMPARE(h->read(0, buf.data(), buf.size()), qint64(-1));  // stays cancelled
    }

    void metadataTimeoutIsAnError()
    {
        QTemporaryDir dir;
        BrokerSettings s = testSettings(dir.filePath("cache"));
        s.metadataTimeoutMs = 1500;
        StreamBroker broker(s);
        BrokerError error;
        QElapsedTimer timer;
        timer.start();
        QVERIFY(!broker.open({QStringLiteral("0123456789abcdef0123456789abcdef01234567")}, &error));
        QCOMPARE(error, BrokerError::MetadataTimeout);
        QVERIFY(timer.elapsed() >= 1400);

        QVERIFY(!broker.open({QStringLiteral("not a torrent")}, &error));
        QCOMPARE(error, BrokerError::InvalidUri);

        std::atomic<bool> stop{false};
        std::thread stopper([&] { QThread::msleep(200); stop = true; });
        const bool opened = bool(broker.open({QStringLiteral("1123456789abcdef0123456789abcdef01234567")}, &error,
                                             [&] { return stop.load(); }));
        stopper.join();
        QVERIFY(!opened);
        QCOMPARE(error, BrokerError::Cancelled);
    }

    // Two seeders, each with half of the pieces; neither can serve alone.
    void sparseSwarmIsCombined()
    {
        QTemporaryDir dir;
        const TestTorrent t = harness::makeTorrent(dir.filePath("seed"), "movie.mkv", {16 * MiB}, kPiece, 8);
        QByteArray firstHalf = t.data[0], secondHalf = t.data[0];
        std::fill(firstHalf.begin() + int(8 * MiB), firstHalf.end(), 0);
        std::fill(secondHalf.begin(), secondHalf.begin() + int(8 * MiB), 0);
        harness::writeFile(dir.filePath("a/movie.mkv"), firstHalf);
        harness::writeFile(dir.filePath("b/movie.mkv"), secondHalf);
        Seeder a(t, dir.filePath("a"), 0, true);
        Seeder b(t, dir.filePath("b"), 0, true);
        QTRY_VERIFY_WITH_TIMEOUT(a.status().state == lt::torrent_status::downloading
                                     && b.status().state == lt::torrent_status::downloading, 10000);
        QVERIFY(!a.status().pieces.all_set());

        StreamBroker broker(testSettings(dir.filePath("cache")));
        auto h = broker.open({harness::magnet(t, {a.port(), b.port()})});
        QVERIFY(h);
        QVERIFY(readRange(*h, 0, h->size()) == t.data[0]);
    }

    void cacheStaysUnderItsCap()
    {
        QTemporaryDir dir;
        const TestTorrent first = harness::makeTorrent(dir.filePath("seed1"), "one.mkv", {12 * MiB}, kPiece, 9);
        const TestTorrent second = harness::makeTorrent(dir.filePath("seed2"), "two.mkv", {12 * MiB}, kPiece, 10);
        Seeder s1(first, first.root);
        Seeder s2(second, second.root);
        const QString cache = dir.filePath("cache");
        BrokerSettings settings = testSettings(cache);
        settings.cacheCapBytes = 20 * MiB;
        StreamBroker broker(settings);

        auto h1 = broker.open({harness::magnet(first, {s1.port()})});
        QVERIFY(h1);
        QVERIFY(readRange(*h1, 0, h1->size()) == first.data[0]);
        h1->close();
        QThread::msleep(20);  // a distinct last-used time

        auto h2 = broker.open({harness::magnet(second, {s2.port()})});
        QVERIFY(h2);
        QVERIFY(readRange(*h2, 0, h2->size()) == second.data[0]);
        // The open torrent is never evicted, even while over the cap.
        QVERIFY(QFileInfo::exists(QDir(cache).filePath(harness::infoHash(second))));
        h2->close();

        QTRY_VERIFY_WITH_TIMEOUT(StreamBroker::diskUsage(cache) <= settings.cacheCapBytes, 5000);
        const QString firstDir = QDir(cache).filePath(harness::infoHash(first));
        QTRY_VERIFY_WITH_TIMEOUT(!QFileInfo::exists(firstDir), 5000);
        if (QFileInfo::exists(firstDir)) {
            QDirIterator it(firstDir, QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
            while (it.hasNext())
                qInfo("left: %s", qPrintable(it.next()));
        }
        QVERIFY(QFileInfo::exists(QDir(cache).filePath(harness::infoHash(second))));
        qInfo("Cache: %lld MiB of %lld MiB", StreamBroker::diskUsage(cache) / MiB, settings.cacheCapBytes / MiB);
    }

    // A torrent watched before opens without peers: the file list and the
    // downloaded bytes are in the cache.
    void reopensFromCacheWithoutPeers()
    {
        QTemporaryDir dir;
        const TestTorrent t = harness::makeTorrent(dir.filePath("seed"), "movie.mkv", {8 * MiB}, kPiece, 11);
        const QString cache = dir.filePath("cache");
        {
            Seeder seeder(t, t.root);
            StreamBroker broker(testSettings(cache));
            auto h = broker.open({harness::magnet(t, {seeder.port()})});
            QVERIFY(h);
            QVERIFY(readRange(*h, 0, 2 * MiB) == t.data[0].left(int(2 * MiB)));
        }
        StreamBroker broker(testSettings(cache));
        auto h = broker.open({harness::infoHash(t)});
        QVERIFY(h);
        QCOMPARE(h->size(), 8 * MiB);
        QVERIFY(readRange(*h, 0, 2 * MiB) == t.data[0].left(int(2 * MiB)));
    }
};

QTEST_GUILESS_MAIN(TestBroker)
#include "tst_broker.moc"
