// Step 5 end to end: things picked in the browse model play in a real
// (headless) mpv. An add-on offers a web stream (served here, with the
// request headers the add-on asked for) and a torrent (local seeder, found
// through a tracker the add-on names); a local folder offers a file with its
// subtitle next to it.

#include "Browser.h"
#include "FakeHttpServer.h"
#include "LocalLibrary.h"
#include "MpvTarget.h"
#include "Session.h"
#include "StremioAdapter.h"
#include "TorrentHarness.h"
#include "TorrentTransport.h"

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <clocale>

using namespace cj;
using harness::Seeder;
using harness::TestTorrent;
using test::FakeHttpServer;

namespace {

const QString kClip = QStringLiteral(CJ_TEST_DATA_DIR "/testsrc-10s.mkv");

// The app's wiring from main.cpp.
struct App
{
    explicit App(const QString& dataDir)
        : transport(settings(dataDir))
        , target(MpvTarget::Mode::Headless)
        , session(&target)
        , addons(QDir(dataDir).filePath("addons.json"), false)
        , local(QDir(dataDir).filePath("local-folders.json"))
        , browser(&session)
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
        browser.setStremio(&addons);
        browser.setLocal(&local);
    }

    static BrokerSettings settings(const QString& dataDir)
    {
        BrokerSettings s;
        s.cacheDir = QDir(dataDir).filePath("cache");
        s.loopbackOnly = true;
        return s;
    }

    int catalogOf(const QString& adapter) const
    {
        const QList<CatalogRef> list = browser.catalogRefs();
        for (int i = 0; i < list.size(); ++i)
            if (list[i].adapter == adapter)
                return i;
        return -1;
    }

    int streamOf(PlayableSource::Kind kind) const
    {
        const QList<StreamOption> list = browser.streamList();
        for (int i = 0; i < list.size(); ++i)
            if (list[i].source.kind == kind)
                return i;
        return -1;
    }

    TorrentTransport transport;
    MpvTarget target;
    Session session;
    StremioAdapter addons;
    LocalLibrary local;
    Browser browser;
};

} // namespace

class TestBrowsePlay : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase() { std::setlocale(LC_NUMERIC, "C"); }

    // The plan's check: a movie and a series episode from an add-on each
    // play from a web result and from a torrent result.
    void addonStreamsPlay_data()
    {
        QTest::addColumn<QString>("type");
        QTest::newRow("movie") << "movie";
        QTest::newRow("series episode") << "series";
    }

    void addonStreamsPlay()
    {
        QFETCH(QString, type);
        const bool series = type == QLatin1String("series");
        QTemporaryDir dir;
        const TestTorrent t = harness::makeTorrentOfFile(dir.filePath("seed"), "Test Pattern.mkv", kClip, 16 * 1024);
        Seeder seeder(t, t.root);

        FakeHttpServer server;
        server.setTracker(seeder.port());
        FakeHttpServer::Answer clip;
        clip.filePath = kClip;
        clip.contentType = "video/x-matroska";
        server.set("/clip.mkv", clip);
        server.setJson("/addon/manifest.json",
                       R"({"id":"org.test","name":"Test add-on","version":"1.0.0","types":["movie","series"],"idPrefixes":["tt"],
                           "resources":["catalog","meta","stream"],"catalogs":[{"type":"movie","id":"all","name":"All"},{"type":"series","id":"all","name":"All"}]})");
        server.setJson("/addon/catalog/movie/all.json", R"({"metas":[{"id":"tt1","type":"movie","name":"Test Pattern"}]})");
        server.setJson("/addon/catalog/series/all.json", R"({"metas":[{"id":"tt2","type":"series","name":"Pattern Show"}]})");
        server.setJson("/addon/meta/movie/tt1.json", R"({"meta":{"id":"tt1","type":"movie","name":"Test Pattern"}})");
        server.setJson("/addon/meta/series/tt2.json", R"({"meta":{"id":"tt2","type":"series","name":"Pattern Show",
            "videos":[{"id":"tt2:1:2","season":1,"episode":2,"title":"Second"},{"id":"tt2:1:1","season":1,"episode":1,"title":"First"}]}})");
        const QByteArray streams = QByteArray(R"({"streams":[
            {"name":"Web","title":"From the web","url":"BASE/clip.mkv",
             "behaviorHints":{"proxyHeaders":{"request":{"X-Test":"from-addon"}}}},
            {"name":"Torrent","title":"Test Pattern.mkv","infoHash":"HASH","fileIdx":0,"sources":["tracker:BASE/announce"]}
        ]})").replace("BASE", server.base().toUtf8()).replace("HASH", harness::infoHash(t).toUtf8());
        server.setJson("/addon/stream/movie/tt1.json", streams);
        server.setJson("/addon/stream/series/tt2%3A1%3A1.json", streams);

        App app(dir.filePath("data"));
        bool installed = false;
        app.addons.install(server.base() + "/addon/manifest.json", [&](const QString& e) {
            QVERIFY2(e.isEmpty(), qPrintable(e));
            installed = true;
        });
        QTRY_VERIFY(installed);

        int catalog = -1;
        const QList<CatalogRef> catalogs = app.browser.catalogRefs();
        for (int i = 0; i < catalogs.size(); ++i)
            if (catalogs[i].type == type)
                catalog = i;
        app.browser.openCatalog(catalog);
        QTRY_COMPARE(app.browser.itemList().size(), 1);
        app.browser.openItem(0);
        QTRY_VERIFY(!app.browser.detailLoading());
        QCOMPARE(app.browser.videos().size(), series ? 2 : 0);
        app.browser.openVideo(series ? 0 : -1);  // episodes are in order: 1x01 first
        QTRY_VERIFY_WITH_TIMEOUT(!app.browser.streamsLoading(), 10000);
        QCOMPARE(app.browser.streamList().size(), 2);
        const QString title = series ? QStringLiteral("Pattern Show 1x01") : QStringLiteral("Test Pattern");

        // The web stream, with the add-on's request header.
        app.browser.play(app.streamOf(PlayableSource::Kind::Http));
        QTRY_VERIFY_WITH_TIMEOUT(app.session.hasMedia() && app.session.position() > 1.0, 20000);
        QCOMPARE(app.session.title(), title);
        bool sawHeader = false;
        for (const auto& r : server.requests)
            if (r.path == "/clip.mkv")
                sawHeader = sawHeader || r.headers.value("x-test") == "from-addon";
        QVERIFY(sawHeader);

        // The torrent, whose peer is found through the add-on's tracker.
        app.browser.play(app.streamOf(PlayableSource::Kind::Torrent));
        QTRY_VERIFY_WITH_TIMEOUT(app.session.source().scheme() == QLatin1String("torrent"), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(app.session.hasMedia() && app.session.position() > 1.0, 40000);
        QVERIFY(server.paths().filter("/announce").size() > 0);

        // Switching to the other source of the same video keeps the place.
        QTRY_VERIFY_WITH_TIMEOUT(app.session.position() > 3.0, 20000);
        const double before = app.session.position();
        app.browser.play(app.streamOf(PlayableSource::Kind::Http));
        QTRY_VERIFY_WITH_TIMEOUT(app.session.source().scheme() == QLatin1String("http"), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(app.session.hasMedia() && app.session.position() > 0, 20000);
        QVERIFY2(app.session.position() >= before - 0.5, qPrintable(QString::number(app.session.position())));
    }

    // Two torrents of the same movie: picking the second one while the first
    // plays must play the second, not restart the first.
    void switchesToAnotherTorrentOfTheSameMovie()
    {
        QTemporaryDir dir;
        const TestTorrent a = harness::makeTorrentOfFile(dir.filePath("seedA"), "Movie.A.mkv", kClip, 16 * 1024);
        const TestTorrent b = harness::makeTorrentOfFile(dir.filePath("seedB"), "Movie.B.mkv", kClip, 16 * 1024);
        Seeder seederA(a, a.root);
        Seeder seederB(b, b.root);
        QVERIFY(harness::infoHash(a) != harness::infoHash(b));

        App app(dir.filePath("data"));
        app.session.playSource(PlayableSource::fromUrl(QUrl(harness::magnet(a, {seederA.port()})), "Movie"));
        QTRY_VERIFY_WITH_TIMEOUT(app.session.hasMedia() && app.session.position() > 2.0, 30000);
        QCOMPARE(TorrentTransport::infoHashOf(app.session.source()), harness::infoHash(a));

        app.session.playSource(PlayableSource::fromUrl(QUrl(harness::magnet(b, {seederB.port()})), "Movie"));
        QTRY_COMPARE_WITH_TIMEOUT(TorrentTransport::infoHashOf(app.session.source()), harness::infoHash(b), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(app.session.hasMedia() && app.session.position() > 1.0, 30000);
        // The second torrent is the one being read.
        QTRY_VERIFY_WITH_TIMEOUT(app.session.streamPhase() == StreamPhase::Ready, 10000);
    }

    void localFilePlaysWithItsSubtitle()
    {
        QTemporaryDir dir;
        const QString folder = dir.filePath("videos/Clip (2020)");
        QDir().mkpath(folder);
        QVERIFY(QFile::copy(kClip, folder + "/Clip.2020.720p.mkv"));
        QFile srt(folder + "/Clip.2020.720p.en.srt");
        QVERIFY(srt.open(QIODevice::WriteOnly));
        srt.write("1\n00:00:00,500 --> 00:00:09,000\nHello from the subtitle file\n");
        srt.close();

        App app(dir.filePath("data"));
        QSignalSpy scanned(&app.local, &LocalLibrary::libraryChanged);
        QVERIFY(app.browser.addFolder(QUrl::fromLocalFile(dir.filePath("videos"))));
        QVERIFY(scanned.wait(10000));

        app.browser.openCatalog(app.catalogOf("local"));
        QTRY_COMPARE(app.browser.itemList().size(), 1);
        QCOMPARE(app.browser.itemList()[0].name, QStringLiteral("Clip"));
        app.browser.openItem(0);
        QTRY_VERIFY(!app.browser.detailLoading());
        app.browser.openVideo(-1);
        QTRY_VERIFY(!app.browser.streamsLoading());
        QCOMPARE(app.browser.streamList().size(), 1);
        app.browser.play(0);
        QTRY_VERIFY_WITH_TIMEOUT(app.session.hasMedia() && app.session.position() > 0.5, 20000);
        QCOMPARE(app.session.title(), QStringLiteral("Clip (2020)"));

        // The subtitle is there as a track and shows, as players show a
        // subtitle file found next to a video.
        QTRY_VERIFY_WITH_TIMEOUT(!app.session.subtitleTracks().isEmpty(), 5000);
        const QList<Track> tracks = app.session.tracks();
        const auto sub = std::find_if(tracks.begin(), tracks.end(), [](const Track& t) { return t.type == Track::Type::Subtitle; });
        QVERIFY(sub != tracks.end());
        QVERIFY(sub->external);
        QCOMPARE(sub->language, QStringLiteral("en"));
        QVERIFY(sub->selected);
    }
};

QTEST_MAIN(TestBrowsePlay)
#include "tst_browseplay.moc"
