// End to end: a magnet link opened through the Session plays in a real
// (headless) mpv, with the bytes coming from local seeders through the broker
// and the torrent:// stream protocol. No HTTP server anywhere.

#include "MpvTarget.h"
#include "Session.h"
#include "StreamBroker.h"
#include "TorrentHarness.h"
#include "TorrentTransport.h"

#include <QElapsedTimer>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <clocale>

using namespace cj;
using harness::Seeder;
using harness::TestTorrent;

namespace {

const QString kClip = QStringLiteral(CJ_TEST_DATA_DIR "/testsrc-10s.mkv");  // 85 KB, 10 s
constexpr int kPiece = 16 * 1024;

// The app's wiring from main.cpp: Session, mpv and the torrent transport.
struct Player
{
    explicit Player(const QString& cacheDir, int metadataTimeoutMs = 30000)
        : transport(settings(cacheDir, metadataTimeoutMs))
        , target(MpvTarget::Mode::Headless)
        , session(&target)
    {
        transport.registerWith(target.handle());
        session.addSourceResolver([this](const QString& input) { return transport.resolve(input); });
        QObject::connect(&session, &Session::sourceChanged, &transport, [this] { transport.setCurrent(session.source()); });
        QObject::connect(&session, &Session::stateChanged, &transport, [this] {
            if (session.state() == PlaybackState::Idle)
                transport.setCurrent({});
        });
        QObject::connect(&session, &Session::durationChanged, &transport, [this] { transport.setDuration(session.duration()); });
        QObject::connect(&transport, &TorrentTransport::statusChanged, &session, &Session::setStreamStatus);
        QObject::connect(&transport, &TorrentTransport::openFailed, &session, &Session::reportSourceError);
        QObject::connect(&session, &Session::streamStatusChanged, &session, [this] { phases.insert(session.streamPhase()); });
    }

    static BrokerSettings settings(const QString& cacheDir, int metadataTimeoutMs = 30000)
    {
        BrokerSettings s;
        s.cacheDir = cacheDir;
        s.loopbackOnly = true;
        s.metadataTimeoutMs = metadataTimeoutMs;
        return s;
    }

    TorrentTransport transport;  // outlives mpv, as in the app
    MpvTarget target;
    Session session;
    QSet<StreamPhase> phases;
};

} // namespace

class TestTorrentStream : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase() { std::setlocale(LC_NUMERIC, "C"); }

    void resolvesTorrentInputs()
    {
        QTemporaryDir dir;
        TorrentTransport transport(Player::settings(dir.filePath("cache")));
        const QString hash = QStringLiteral("0123456789abcdef0123456789abcdef01234567");

        auto magnet = transport.resolve("magnet:?xt=urn:btih:" + hash.toUpper() + "&dn=Big+Buck+Bunny&tr=udp%3A%2F%2Fx");
        QVERIFY(magnet);
        QCOMPARE(magnet->url.toString(), "torrent://" + hash);
        QCOMPARE(magnet->title, QStringLiteral("Big Buck Bunny"));

        // Base32 info hashes, as older magnet links have them.
        auto base32 = transport.resolve("magnet:?xt=urn:btih:AERUKZ4JVPG66AJDIVTYTK6N54ASGRLH");
        QVERIFY(base32);
        QCOMPARE(base32->url.host(), hash);

        auto bare = transport.resolve(hash);
        QVERIFY(bare);
        QCOMPARE(bare->url.host(), hash);

        QVERIFY(!transport.resolve(kClip));
        QVERIFY(!transport.resolve("https://example.com/video.mp4"));
        QVERIFY(!transport.resolve("magnet:?dn=no-hash"));
    }

    void magnetPlaysToTheEnd()
    {
        QTemporaryDir dir;
        const TestTorrent t = harness::makeTorrentOfFile(dir.filePath("seed"), "Test Pattern.mkv", kClip, kPiece);
        Seeder seeder(t, t.root, 64 * 1024);  // slow enough that buffering shows

        Player p(dir.filePath("cache"));
        QElapsedTimer clock;
        clock.start();
        p.session.openPath(harness::magnet(t, {seeder.port()}));
        QTRY_VERIFY_WITH_TIMEOUT(p.session.hasMedia(), 30000);
        qInfo() << "Magnet to first frame:" << clock.elapsed() << "ms";
        QCOMPARE(p.session.title(), QStringLiteral("Test Pattern.mkv"));
        QTRY_COMPARE_WITH_TIMEOUT(qRound(p.session.duration()), 10, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(p.phases.contains(StreamPhase::Ready), 5000);
        QTRY_COMPARE_WITH_TIMEOUT(p.session.state(), PlaybackState::Ended, 30000);
    }

    void seeksAndSurvivesALongPause()
    {
        QTemporaryDir dir;
        const TestTorrent t = harness::makeTorrentOfFile(dir.filePath("seed"), "clip.mkv", kClip, kPiece);
        Seeder seeder(t, t.root, 32 * 1024);

        Player p(dir.filePath("cache"));
        p.session.openPath(harness::magnet(t, {seeder.port()}));
        QTRY_VERIFY_WITH_TIMEOUT(p.session.hasMedia(), 30000);

        p.session.pause();
        QTRY_VERIFY_WITH_TIMEOUT(p.session.paused(), 5000);
        QTest::qWait(5000);  // peers may drop meanwhile; playback must carry on
        p.session.seek(7.0);
        QTRY_VERIFY_WITH_TIMEOUT(qAbs(p.session.position() - 7.0) < 0.6, 15000);
        p.session.seek(2.0);
        QTRY_VERIFY_WITH_TIMEOUT(qAbs(p.session.position() - 2.0) < 0.6, 15000);
        p.session.play();
        QTRY_VERIFY_WITH_TIMEOUT(p.session.position() > 3.0, 15000);
    }

    // The first torrent has no peers at all; switching to a local file must
    // not wait for it.
    void switchingAwayMidBufferDoesNotHang()
    {
        QTemporaryDir dir;
        const TestTorrent t = harness::makeTorrentOfFile(dir.filePath("seed"), "nobody.mkv", kClip, kPiece);

        Player p(dir.filePath("cache"));
        p.session.openPath(harness::magnet(t, {}));
        QTRY_COMPARE_WITH_TIMEOUT(p.session.streamPhase(), StreamPhase::FindingPeers, 5000);

        QElapsedTimer clock;
        clock.start();
        p.session.openPath(kClip);
        QTRY_VERIFY_WITH_TIMEOUT(p.session.hasMedia(), 10000);
        QVERIFY2(clock.elapsed() < 5000, qPrintable(QString::number(clock.elapsed())));
        QCOMPARE(p.session.streamPhase(), StreamPhase::Inactive);
        QCOMPARE(p.session.title(), QStringLiteral("testsrc-10s"));
    }

    // The next episode in the same torrent is an item of its own: its
    // buffering shows, not dropped as a report for the last one.
    void nextFileInTheSameTorrentReportsItsStatus()
    {
        QTemporaryDir dir;
        TestTorrent t;
        t.root = dir.filePath("seed");
        const QString season = QDir(t.root).filePath(QStringLiteral("Season 1"));
        for (const char* name : {"e1.mkv", "e2.mkv"}) {
            QDir().mkpath(season);
            QVERIFY(QFile::copy(kClip, QDir(season).filePath(QString::fromLatin1(name))));
        }
        t.info = harness::createTorrent(season, t.root, kPiece);
        Seeder seeder(t, t.root, 24 * 1024);  // the second file takes a few seconds

        Player p(dir.filePath("cache"));
        auto source = p.transport.resolve(harness::magnet(t, {seeder.port()}));
        QVERIFY(source);
        const auto fileUrl = [&](const QString& name) {
            PlayableSource s = *source;
            s.url.setPath(QStringLiteral("/%1").arg(t.indexOf(name)));
            return s;
        };
        const PlayableSource first = fileUrl(QStringLiteral("e1.mkv"));
        const PlayableSource second = fileUrl(QStringLiteral("e2.mkv"));
        QVERIFY(t.indexOf(QStringLiteral("e1.mkv")) >= 0 && t.indexOf(QStringLiteral("e2.mkv")) >= 0);

        p.session.playSource(first);
        QTRY_VERIFY_WITH_TIMEOUT(p.session.hasMedia(), 30000);

        p.phases.clear();
        p.session.playSource(second);
        // The Session drops reports about any other item, so a phase here
        // was reported for the second file.
        QTRY_VERIFY_WITH_TIMEOUT(p.phases.contains(StreamPhase::Buffering) || p.phases.contains(StreamPhase::Ready), 15000);
        QTRY_VERIFY_WITH_TIMEOUT(p.session.hasMedia(), 30000);
    }

    // Nobody has it: the user is told exactly that, not "could not open".
    void noPeersIsItsOwnError()
    {
        QTemporaryDir dir;
        const TestTorrent t = harness::makeTorrentOfFile(dir.filePath("seed"), "lonely.mkv", kClip, kPiece);
        Player p(dir.filePath("cache"), 3000);
        QSignalSpy errors(&p.session, &Session::errorOccurred);
        p.session.openPath(harness::magnet(t, {}));
        QTRY_COMPARE_WITH_TIMEOUT(errors.count(), 1, 15000);
        QCOMPARE(errors.at(0).at(0).value<PlaybackError>(), PlaybackError::NoPeers);
        QTest::qWait(1000);  // mpv's own "loading failed" must not follow it
        QCOMPARE(errors.count(), 1);
        QTRY_COMPARE_WITH_TIMEOUT(p.session.state(), PlaybackState::Idle, 5000);
    }

    // A torrent whose bytes trickle in: stopping while mpv waits for them
    // returns at once, and so does switching to another torrent.
    void stopAndSwitchWhileWaitingForData()
    {
        QTemporaryDir dir;
        const TestTorrent slow = harness::makeTorrentOfFile(dir.filePath("slow"), "slow.mkv", kClip, kPiece);
        const TestTorrent fast = harness::makeTorrentOfFile(dir.filePath("fast"), "fast.mkv", kClip, kPiece);
        Seeder slowSeeder(slow, slow.root, 1024);  // 1 KB/s: a piece takes 16 s
        Seeder fastSeeder(fast, fast.root);

        Player p(dir.filePath("cache"));
        p.session.openPath(harness::magnet(slow, {slowSeeder.port()}));
        QTRY_COMPARE_WITH_TIMEOUT(p.session.streamPhase(), StreamPhase::Buffering, 15000);

        // Nothing new for 10 s: the UI says so, and it keeps trying.
        QTRY_COMPARE_WITH_TIMEOUT(p.session.streamPhase(), StreamPhase::Stalled, 20000);

        QElapsedTimer clock;
        clock.start();
        p.session.openPath(harness::magnet(fast, {fastSeeder.port()}));
        QTRY_VERIFY_WITH_TIMEOUT(p.session.hasMedia(), 15000);
        QCOMPARE(p.session.title(), QStringLiteral("fast.mkv"));
        qInfo() << "Switch from a waiting torrent to another:" << clock.elapsed() << "ms";

        p.session.openPath(harness::magnet(slow, {slowSeeder.port()}));
        QTRY_COMPARE_WITH_TIMEOUT(p.session.state(), PlaybackState::Loading, 5000);
        clock.restart();
        p.session.stop();
        QTRY_COMPARE_WITH_TIMEOUT(p.session.state(), PlaybackState::Idle, 5000);
        qInfo() << "Stop while waiting:" << clock.elapsed() << "ms";
    }
};

QTEST_GUILESS_MAIN(TestTorrentStream)
#include "tst_torrentstream.moc"
