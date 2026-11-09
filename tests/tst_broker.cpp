// Stream broker tests against a local swarm (tests/TorrentHarness.h).
// Each test makes its own torrents and seeders in a temporary folder.

#include "FakeHttpServer.h"
#include "StreamBroker.h"
#include "TorrentHarness.h"

#include <QDirIterator>
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <QtTest>

#include <algorithm>
#include <random>
#include <future>
#include <thread>

using namespace cj;
using harness::Seeder;
using harness::TestTorrent;
using test::FakeHttpServer;

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

// Settings for the tracker tests: `openTracker` stands in for the built-in
// open trackers, and the DHT setting is on, as in the app.
BrokerSettings trackerSettings(const QString& cacheDir, const FakeHttpServer& openTracker)
{
    BrokerSettings s = testSettings(cacheDir);
    s.extraTrackers = {openTracker.base() + QStringLiteral("/announce")};
    s.dht = true;
    return s;
}

// A short idle delay, so the pause shows within a test.
BrokerSettings idleSettings(const QString& cacheDir)
{
    BrokerSettings s = testSettings(cacheDir);
    s.idlePauseMs = 300;
    return s;
}

// Opens on another thread: open() waits for metadata, and the fake trackers
// answer on this one.
std::future<std::shared_ptr<StreamHandle>> openLater(StreamBroker& broker, const QString& uri)
{
    return std::async(std::launch::async, [&broker, uri] { return broker.open({uri}); });
}

bool ready(const std::future<std::shared_ptr<StreamHandle>>& f)
{
    return f.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
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

    // Past the urgent window, spare bandwidth keeps filling the buffer at low
    // priority, up to ten minutes of video ahead.
    void buffersAheadInTheBackground()
    {
        QTemporaryDir dir;
        const TestTorrent t = harness::makeTorrent(dir.filePath("seed"), "movie.mkv", {32 * MiB}, kPiece, 4);
        Seeder seeder(t, t.root, 16 * MiB);
        StreamBroker broker(testSettings(dir.filePath("cache")));
        OpenRequest req{harness::magnet(t, {seeder.port()})};
        req.durationSeconds = 320;  // 100 KiB/s: the urgent window is 3 to 6 MiB
        auto h = broker.open(req);
        QVERIFY(h);
        QVERIFY(readRange(*h, 0, 64 * 1024) == t.data[0].left(64 * 1024));
        QTRY_VERIFY_WITH_TIMEOUT(h->stats().bufferedBytes >= 30 * MiB, 30000);
    }

    // RAM only: the video is never written to disk, only the file list.
    void memoryOnlyWritesNoVideoToDisk()
    {
        QTemporaryDir dir;
        const TestTorrent t = harness::makeTorrent(dir.filePath("seed"), "movie.mkv", {24 * MiB}, kPiece, 21);
        Seeder seeder(t, t.root);
        const QString cache = dir.filePath("cache");
        BrokerSettings settings = testSettings(cache);
        settings.memoryOnly = true;
        settings.memoryCapBytes = 256 * MiB;
        StreamBroker broker(settings);
        QVERIFY(broker.memoryOnly());

        auto h = broker.open({harness::magnet(t, {seeder.port()})});
        QVERIFY(h);
        QVERIFY(readRange(*h, 0, h->size()) == t.data[0]);
        QVERIFY(broker.memoryUsed() >= 24 * MiB);
        h->close();
        qInfo("Cache folder after a RAM-only stream: %lld bytes", StreamBroker::diskUsage(cache));
        QVERIFY(StreamBroker::diskUsage(cache) < MiB);
        QVERIFY(!QFileInfo::exists(QDir(cache).filePath(harness::infoHash(t) + "/movie.mkv")));
    }

    // RAM only, with a cap smaller than the file: memory stays near the cap
    // while playing through, and seeking back to a dropped part downloads it
    // again (the torrent reloads) with the right bytes.
    void memoryOnlyStaysUnderItsCapAndRefetches()
    {
        QTemporaryDir dir;
        const TestTorrent t = harness::makeTorrent(dir.filePath("seed"), "movie.mkv", {64 * MiB}, kPiece, 22);
        Seeder seeder(t, t.root, 16 * MiB);
        BrokerSettings settings = testSettings(dir.filePath("cache"));
        settings.memoryOnly = true;
        settings.memoryCapBytes = 32 * MiB;
        StreamBroker broker(settings);

        OpenRequest req{harness::magnet(t, {seeder.port()})};
        req.durationSeconds = 64;  // 1 MiB/s: 16 MiB ahead at most
        auto h = broker.open(req);
        QVERIFY(h);
        qint64 peak = 0;
        for (qint64 offset = 0; offset < h->size(); offset += 2 * MiB) {
            QVERIFY(readRange(*h, offset, 2 * MiB) == t.data[0].mid(int(offset), int(2 * MiB)));
            peak = qMax(peak, broker.memoryUsed());
        }
        qInfo("Peak memory %lld MiB, cap %lld MiB", peak / MiB, settings.memoryCapBytes / MiB);
        // Over by at most a few pieces between two trims.
        QVERIFY(peak <= settings.memoryCapBytes + 8 * MiB);
        QTRY_VERIFY_WITH_TIMEOUT(broker.memoryUsed() <= settings.memoryCapBytes, 5000);

        // The middle was dropped long ago: it comes back from the swarm.
        QElapsedTimer timer;
        timer.start();
        QVERIFY(readRange(*h, 20 * MiB, MiB) == t.data[0].mid(int(20 * MiB), int(MiB)));
        qInfo("Seek back to a dropped part: %lld ms", timer.elapsed());
        QVERIFY(timer.elapsed() < 10000);
        // And reading on from there still works.
        QVERIFY(readRange(*h, 21 * MiB, 4 * MiB) == t.data[0].mid(int(21 * MiB), int(4 * MiB)));
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
        // Nor are they created: libtorrent sizes every file it may write,
        // and macOS then allocated all of an unread one.
        QVERIFY(!QFileInfo::exists(content + "/file0.bin"));
        QVERIFY(!QFileInfo::exists(content + "/file2.bin"));
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
        // What the first run left, for when this fails on one system only.
        QDirIterator files(cache, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
        while (files.hasNext()) {
            const QFileInfo f(files.next());
            qInfo("Left in the cache: %s, %lld bytes, %lld on disk", qPrintable(f.fileName()), f.size(),
                  StreamBroker::diskUsage(f.filePath()));
        }
        StreamBroker broker(testSettings(cache));
        auto h = broker.open({harness::infoHash(t)});
        QVERIFY(h);
        QCOMPARE(h->size(), 8 * MiB);
        QVERIFY(readRange(*h, 0, 2 * MiB) == t.data[0].left(int(2 * MiB)));
    }

    // The open trackers hear of a torrent only once its metadata says it is
    // public (BEP 27: a private one is announced to its own trackers only).
    // The DHT setting is on, as in the app; the harness keeps real DHT
    // networking off, so all of this stays on this computer.
    void openTrackersWaitForPublicMetadata()
    {
        QTemporaryDir dir;
        FakeHttpServer openTracker;  // stands in for the well-known open trackers
        openTracker.setTracker(1);
        BrokerSettings s = trackerSettings(dir.filePath("cache"), openTracker);
        const TestTorrent t = harness::makeTorrent(dir.filePath("seed"), "public.mkv", {2 * MiB}, kPiece, 3);
        {
            StreamBroker broker(s);
            // A magnet link with no peers: its metadata cannot come yet.
            auto waiting = openLater(broker, harness::magnet(t, {}));
            QTest::qWait(1500);
            QVERIFY2(openTracker.requests.isEmpty(), qPrintable(openTracker.paths().join(' ')));

            // A peer turns up (a second link to the same torrent names it).
            Seeder seeder(t, t.root);
            auto withPeer = openLater(broker, harness::magnet(t, {seeder.port()}));
            QTRY_VERIFY_WITH_TIMEOUT(ready(waiting) && ready(withPeer), 15000);
            QVERIFY(waiting.get());
            QVERIFY(withPeer.get());
            // Public: the open trackers are asked now.
            QTRY_VERIFY_WITH_TIMEOUT(!openTracker.requests.isEmpty(), 10000);
        }

        // Next run: the metadata comes from the cache, already public.
        openTracker.requests.clear();
        StreamBroker broker(s);
        QVERIFY(broker.open({harness::infoHash(t)}));
        QTRY_VERIFY_WITH_TIMEOUT(!openTracker.requests.isEmpty(), 10000);
    }

    void privateTorrentNeverMeetsTheOpenTrackers()
    {
        QTemporaryDir dir;
        FakeHttpServer openTracker;
        FakeHttpServer ownTracker;  // the private torrent's own
        openTracker.setTracker(1);
        ownTracker.setTracker(1);
        const BrokerSettings s = trackerSettings(dir.filePath("cache"), openTracker);

        TestTorrent t;
        t.root = dir.filePath("private");
        harness::writeFile(QDir(t.root).filePath("private.mkv"), harness::testBytes(2 * MiB, 9));
        const std::vector<char> torrent = harness::createTorrentFile(
            QDir(t.root).filePath("private.mkv"), t.root, kPiece, true, (ownTracker.base() + "/announce").toStdString());
        t.info = lt::load_torrent_buffer(torrent).ti;
        QVERIFY(t.info->priv());
        const QString file = dir.filePath("private.torrent");
        {
            QFile f(file);
            QVERIFY(f.open(QIODevice::WriteOnly));
            QCOMPARE(f.write(torrent.data(), qint64(torrent.size())), qint64(torrent.size()));
        }
        const QString cache = QDir(s.cacheDir).filePath(harness::infoHash(t));

        {
            StreamBroker broker(s);
            // A bare magnet link: nobody knows yet that it is private.
            auto waiting = openLater(broker, harness::magnet(t, {}));
            QTest::qWait(1500);
            QVERIFY2(openTracker.requests.isEmpty(), qPrintable(openTracker.paths().join(' ')));

            // Its metadata arrives (the .torrent file of the same torrent), private.
            auto fromFile = openLater(broker, file);
            QTRY_VERIFY_WITH_TIMEOUT(ready(waiting) && ready(fromFile), 15000);
            QVERIFY(waiting.get());
            QVERIFY(fromFile.get());
            // Its own tracker is asked; give the open ones every chance too.
            QTRY_VERIFY_WITH_TIMEOUT(!ownTracker.requests.isEmpty(), 10000);
            QTest::qWait(1500);
            QVERIFY2(openTracker.requests.isEmpty(), qPrintable(openTracker.paths().join(' ')));
        }
        // Closing saved the file list and the resume data.
        QVERIFY(QFileInfo::exists(QDir(cache).filePath("metadata.torrent")));
        QVERIFY(QFileInfo::exists(QDir(cache).filePath("resume.dat")));

        // Next run: reopened from the cached metadata and resume data.
        StreamBroker broker(s);
        QVERIFY(broker.open({harness::magnet(t, {})}));
        QTest::qWait(2000);
        QVERIFY2(openTracker.requests.isEmpty(), qPrintable(openTracker.paths().join(' ')));
    }

    // A torrent pauses only once its last reader has gone, and opening it
    // again resumes it and fetches what it never had.
    void idlePauseWaitsForTheLastReader()
    {
        QTemporaryDir dir;
        const TestTorrent t = harness::makeTorrent(dir.filePath("seed"), "movie.mkv", {16 * MiB}, kPiece, 4);
        Seeder seeder(t, t.root, 1024 * 1024);  // slow: the middle is still missing at the pause
        StreamBroker broker(idleSettings(dir.filePath("cache")));
        const QString link = harness::magnet(t, {seeder.port()});
        const QString hash = harness::infoHash(t);

        auto a = broker.open({link});
        auto b = broker.open({link});
        QVERIFY(a && b);
        QVERIFY(readRange(*a, 0, kPiece) == t.data[0].left(int(kPiece)));
        a.reset();
        QTest::qWait(1500);  // several times the idle delay: one reader is left
        QVERIFY(!broker.torrentStats(hash).paused);
        b.reset();
        QTRY_VERIFY_WITH_TIMEOUT(broker.torrentStats(hash).paused, 5000);

        auto c = broker.open({link});
        QVERIFY(c);
        QVERIFY(!broker.torrentStats(hash).paused);
        const qint64 middle = 8 * MiB;
        QVERIFY(readRange(*c, middle, kPiece) == t.data[0].mid(int(middle), int(kPiece)));
    }

    // An open that ends without a reader leaves the torrent unused.
    void failedOpensLeaveNothingInUse()
    {
        QTemporaryDir dir;
        BrokerSettings s = idleSettings(dir.filePath("cache"));
        s.metadataTimeoutMs = 1000;
        StreamBroker broker(s);

        const TestTorrent lonely = harness::makeTorrent(dir.filePath("a"), "lonely.mkv", {MiB}, kPiece, 5);
        BrokerError error;
        QVERIFY(!broker.open({harness::magnet(lonely, {})}, &error));
        QCOMPARE(error, BrokerError::MetadataTimeout);
        QTRY_VERIFY_WITH_TIMEOUT(broker.torrentStats(harness::infoHash(lonely)).paused, 5000);

        const TestTorrent dropped = harness::makeTorrent(dir.filePath("b"), "dropped.mkv", {MiB}, kPiece, 6);
        QElapsedTimer clock;
        clock.start();
        QVERIFY(!broker.open({harness::magnet(dropped, {})}, &error, [&] { return clock.elapsed() > 300; }));
        QCOMPARE(error, BrokerError::Cancelled);
        QTRY_VERIFY_WITH_TIMEOUT(broker.torrentStats(harness::infoHash(dropped)).paused, 5000);
    }

    // The cache never deletes a torrent someone is still opening. A waits for
    // its metadata while B's reader closes and the cache is far over its cap.
    void cacheKeepsATorrentBeingOpened()
    {
        QTemporaryDir dir;
        BrokerSettings s = idleSettings(dir.filePath("cache"));
        s.cacheCapBytes = 1;  // everything not in use must go
        StreamBroker broker(s);

        const TestTorrent b = harness::makeTorrent(dir.filePath("b"), "b.mkv", {2 * MiB}, kPiece, 7);
        Seeder seederB(b, b.root);
        auto readerB = broker.open({harness::magnet(b, {seederB.port()})});
        QVERIFY(readerB);
        QVERIFY(readRange(*readerB, 0, 2 * MiB) == b.data[0]);

        const TestTorrent a = harness::makeTorrent(dir.filePath("a"), "a.mkv", {2 * MiB}, kPiece, 8);
        auto opening = openLater(broker, harness::magnet(a, {}));  // no peers yet: waits
        QTRY_VERIFY_WITH_TIMEOUT(QDir(s.cacheDir).exists(harness::infoHash(a)), 5000);

        readerB.reset();  // the last close runs the cache cleanup
        QTRY_VERIFY_WITH_TIMEOUT(!QDir(s.cacheDir).exists(harness::infoHash(b)), 5000);
        QVERIFY(QDir(s.cacheDir).exists(harness::infoHash(a)));

        // A's metadata arrives: the open that was waiting succeeds.
        Seeder seederA(a, a.root);
        auto second = openLater(broker, harness::magnet(a, {seederA.port()}));
        QTRY_VERIFY_WITH_TIMEOUT(ready(opening) && ready(second), 15000);
        auto readerA = opening.get();
        QVERIFY(readerA);
        QVERIFY(second.get());
        QVERIFY(readRange(*readerA, 0, kPiece) == a.data[0].left(int(kPiece)));
    }

    // Evicted, then opened again before libtorrent has finished deleting the
    // old copy: the late "deleted" alert must not delete the new copy's
    // folder. The hook holds the old deletion back, so the reopen always
    // comes first.
    void reopenWhileItsDeletionIsPending()
    {
        QTemporaryDir dir;
        BrokerSettings s = testSettings(dir.filePath("cache"));
        s.deleteDelayMsForTests = 1500;
        StreamBroker broker(s);
        const TestTorrent t = harness::makeTorrent(dir.filePath("seed"), "movie.mkv", {4 * MiB}, kPiece, 21);
        Seeder seeder(t, t.root);
        const QString folder = QDir(s.cacheDir).filePath(harness::infoHash(t));
        {
            auto h = broker.open({harness::magnet(t, {seeder.port()})});
            QVERIFY(h);
            QVERIFY(readRange(*h, 0, 4 * MiB) == t.data[0]);
        }
        broker.clearCache();  // not in use: evicted, its deletion pending

        auto again = broker.open({harness::magnet(t, {seeder.port()})});
        QVERIFY(again);
        QVERIFY(readRange(*again, 0, 4 * MiB) == t.data[0]);
        QTest::qWait(2500);  // past the old deletion
        QVERIFY2(QDir(folder).exists(), "the old deletion removed the reopened torrent's folder");
        // Still readable from its own files, and its resume data saves on close.
        QVERIFY(readRange(*again, 0, 4 * MiB) == t.data[0]);
        again.reset();
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(QDir(folder).filePath("resume.dat")), 5000);
    }

    // The same, but the old folder's removal outlasts the open's wait. The
    // open must not go ahead while the removal runs, or it removes the new
    // copy's files: it waits for the removal instead of giving up on it.
    void reopenWhileItsFolderIsBeingRemoved()
    {
        QTemporaryDir dir;
        BrokerSettings s = testSettings(dir.filePath("cache"));
        s.deleteDelayMsForTests = 1500;  // the removal takes 1.5 s
        s.deleteWaitMsForTests = 300;    // the open gives up waiting after 0.3 s
        StreamBroker broker(s);
        const TestTorrent t = harness::makeTorrent(dir.filePath("seed"), "movie.mkv", {4 * MiB}, kPiece, 22);
        Seeder seeder(t, t.root);
        const QString folder = QDir(s.cacheDir).filePath(harness::infoHash(t));
        {
            auto h = broker.open({harness::magnet(t, {seeder.port()})});
            QVERIFY(h);
            QVERIFY(readRange(*h, 0, 4 * MiB) == t.data[0]);
        }
        broker.clearCache();
        QTest::qWait(200);  // libtorrent's "deleted" alert arrives; the removal starts

        QElapsedTimer opening;
        opening.start();
        auto again = broker.open({harness::magnet(t, {seeder.port()})});
        QVERIFY(again);
        QVERIFY2(opening.elapsed() >= 1000, "the open went ahead while the old folder was being removed");
        QVERIFY(readRange(*again, 0, 4 * MiB) == t.data[0]);
        QTest::qWait(500);
        QVERIFY2(QDir(folder).exists(), "the old removal took the reopened torrent's folder");
        QVERIFY(readRange(*again, 0, 4 * MiB) == t.data[0]);
    }

    // Many things at once, as a user switching sources fast would cause:
    // opens, reads, cancels, closes and cache clears on two torrents from
    // several threads, then shutdown with reads still waiting. Nothing may
    // hang or crash (the sanitizer jobs run this too), and every byte read
    // must be right.
    void concurrentOpensReadsCancelsAndClears()
    {
        QTemporaryDir dir;
        BrokerSettings s = idleSettings(dir.filePath("cache"));
        auto broker = std::make_unique<StreamBroker>(s);
        const TestTorrent a = harness::makeTorrent(dir.filePath("a"), "a.mkv", {3 * MiB}, kPiece, 31);
        const TestTorrent b = harness::makeTorrent(dir.filePath("b"), "b.mkv", {3 * MiB}, kPiece, 32);
        Seeder seederA(a, a.root);
        Seeder seederB(b, b.root);
        const QString links[] = {harness::magnet(a, {seederA.port()}), harness::magnet(b, {seederB.port()})};
        const TestTorrent* torrents[] = {&a, &b};

        std::atomic<bool> stop{false};
        std::atomic<int> wrongBytes{0}, reads{0};
        std::vector<std::thread> workers;
        for (int w = 0; w < 4; ++w) {
            workers.emplace_back([&, w] {
                std::mt19937 rng(unsigned(w) + 1);
                while (!stop) {
                    const int which = int(rng() % 2);
                    auto h = broker->open({links[which]}, nullptr, [&] { return stop.load(); });
                    if (!h)
                        continue;
                    const qint64 at = qint64(rng() % (3 * MiB - 64 * 1024));
                    QByteArray chunk(64 * 1024, Qt::Uninitialized);
                    const qint64 n = h->read(at, chunk.data(), chunk.size());
                    if (n > 0) {
                        ++reads;
                        if (QByteArray(chunk.constData(), int(n)) != torrents[which]->data[0].mid(int(at), int(n)))
                            ++wrongBytes;
                    }
                    if (rng() % 3 == 0)
                        h->cancel();  // a source switch mid-read
                }
            });
        }
        std::thread clearer([&] {
            while (!stop) {
                broker->clearCache();
                std::this_thread::sleep_for(std::chrono::milliseconds(150));
            }
        });
        QTest::qWait(6000);
        stop = true;
        for (auto& t : workers)
            t.join();
        clearer.join();
        QCOMPARE(wrongBytes.load(), 0);
        QVERIFY2(reads.load() > 0, "no read finished");

        // Shutdown while a read waits for a piece nobody has. The torrent is
        // opened from its .torrent file, so open() returns at once with no
        // peers, and the read then blocks. Only the handle is used across
        // the shutdown: a handle may outlive its broker, a call into the
        // broker may not.
        const TestTorrent c = harness::makeTorrent(dir.filePath("c"), "c.mkv", {2 * MiB}, kPiece, 33);
        const QString torrentFile = dir.filePath("c.torrent");
        {
            const std::vector<char> bytes = harness::createTorrentFile(QDir(c.root).filePath("c.mkv"), c.root, kPiece);
            QFile f(torrentFile);
            QVERIFY(f.open(QIODevice::WriteOnly));
            QCOMPARE(f.write(bytes.data(), qint64(bytes.size())), qint64(bytes.size()));
        }
        std::shared_ptr<StreamHandle> blocked = broker->open({torrentFile});
        QVERIFY(blocked);
        std::atomic<bool> reading{false};
        auto waiting = std::async(std::launch::async, [blocked, &reading] {
            reading = true;
            char byte;
            return blocked->read(0, &byte, 1);
        });
        QTRY_VERIFY_WITH_TIMEOUT(reading.load(), 10000);
        QTest::qWait(300);
        QVERIFY(waiting.wait_for(std::chrono::seconds(0)) == std::future_status::timeout);
        QElapsedTimer clock;
        clock.start();
        broker.reset();
        QVERIFY(waiting.wait_for(std::chrono::seconds(20)) == std::future_status::ready);
        QVERIFY2(waiting.get() < 0, "a read after shutdown returned data");
        QVERIFY2(clock.elapsed() < 20000, "shutdown hung");
    }

    // Resume data saved on close means a reopened torrent knows its verified
    // pieces at once, with no re-check of the files and no peers.
    void reopensFromResumeDataAtOnce()
    {
        QTemporaryDir dir;
        const TestTorrent t = harness::makeTorrent(dir.filePath("seed"), "movie.mkv", {16 * MiB}, kPiece, 12);
        const QString cache = dir.filePath("cache");
        {
            Seeder seeder(t, t.root);
            StreamBroker broker(testSettings(cache));
            auto h = broker.open({harness::magnet(t, {seeder.port()})});
            QVERIFY(h);
            QVERIFY(readRange(*h, 0, 4 * MiB) == t.data[0].left(int(4 * MiB)));
        }
        QVERIFY(QFileInfo::exists(QDir(cache).filePath(harness::infoHash(t) + "/resume.dat")));

        StreamBroker broker(testSettings(cache));
        auto h = broker.open({harness::infoHash(t)});
        QVERIFY(h);
        // Right away, not after a check: the first 4 MiB are known to be here.
        QVERIFY2(h->stats().bufferedBytes >= 4 * MiB, qPrintable(QString::number(h->stats().bufferedBytes)));
        QVERIFY(readRange(*h, 0, 4 * MiB) == t.data[0].left(int(4 * MiB)));
    }
};

QTEST_GUILESS_MAIN(TestBroker)
#include "tst_broker.moc"
