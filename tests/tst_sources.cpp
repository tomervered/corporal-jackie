// (JSON in raw strings writes "://" as ":\/\/": moc reads "//" there as a comment.)
// Step 5 without a player: the Stremio protocol, stream ranking, file name
// parsing, the local library, and the add-on adapter and browse model
// against a fake add-on server on 127.0.0.1.

#include "Browser.h"
#include "FakeHttpServer.h"
#include "FakeTarget.h"
#include "LocalLibrary.h"
#include "LocalNames.h"
#include "Session.h"
#include "StreamRanking.h"
#include "StremioAdapter.h"
#include "StremioProtocol.h"

#include <QJsonDocument>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using namespace cj;
using test::FakeHttpServer;

namespace {

QJsonObject json(const char* text)
{
    QJsonParseError e{};
    const QJsonObject o = QJsonDocument::fromJson(text, &e).object();
    if (e.error != QJsonParseError::NoError)
        qFatal("bad test JSON: %s", qPrintable(e.errorString()));
    return o;
}

void touch(const QString& path, qint64 size = 1)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.resize(size);
}

QByteArray manifest(const QByteArray& id, const QByteArray& name, const QByteArray& resources,
                    const QByteArray& catalogs = "[]", const QByteArray& extra = "")
{
    return "{\"id\":\"" + id + "\",\"version\":\"1.0.0\",\"name\":\"" + name
         + "\",\"types\":[\"movie\",\"series\"],\"idPrefixes\":[\"tt\"],\"resources\":" + resources
         + ",\"catalogs\":" + catalogs + extra + "}";
}

// Waits for an adapter callback.
template <class T>
struct Waiter
{
    std::optional<T> value;
    bool wait(int ms = 10000)
    {
        QElapsedTimer t;
        t.start();
        while (!value && t.elapsed() < ms)
            QTest::qWait(10);
        return bool(value);
    }
};

} // namespace

class TestSources : public QObject
{
    Q_OBJECT

private slots:
    // --- Protocol -----------------------------------------------------------

    void parsesManifests()
    {
        QString error;
        auto m = stremio::parseManifest(json(R"({
            "id": "org.example", "version": "1.2.0", "name": "Example",
            "types": ["movie", "series"], "idPrefixes": ["tt"],
            "resources": ["catalog", {"name": "stream", "types": ["movie"], "idPrefixes": ["tt", "kitsu"]}, "subtitles"],
            "catalogs": [
                {"type": "movie", "id": "new", "name": "New", "extra": [{"name": "search", "isRequired": true}]},
                {"type": "series", "id": "old", "extraSupported": ["search", "skip"]},
                {"type": "movie"}
            ],
            "behaviorHints": {"configurationRequired": false}
        })"), &error);
        QVERIFY2(m, qPrintable(error));
        QCOMPARE(m->catalogs.size(), 2);  // the one without an id is dropped
        QVERIFY(m->catalogs[0].searchable && m->catalogs[0].searchRequired);
        QVERIFY(m->catalogs[1].searchable && !m->catalogs[1].searchRequired);
        QCOMPARE(m->catalogs[1].name, QStringLiteral("old"));
        QVERIFY(m->serves("stream", "movie", "tt123"));
        QVERIFY(m->serves("stream", "movie", "kitsu:1"));
        QVERIFY(!m->serves("stream", "series", "tt123"));    // the resource's own types
        QVERIFY(m->serves("subtitles", "series", "tt1:1:1"));
        QVERIFY(!m->serves("subtitles", "series", "kitsu:1")); // the manifest's prefixes
        QVERIFY(!m->serves("meta", "movie", "tt1"));

        QVERIFY(!stremio::parseManifest(json(R"({"name": "No id"})"), &error));
        QVERIFY(!error.isEmpty());

        // Catalogs without "catalog" in resources still count.
        auto m2 = stremio::parseManifest(json(R"({"id":"a","name":"A","resources":[],"catalogs":[{"type":"movie","id":"x"}]})"), &error);
        QVERIFY(m2 && m2->serves("catalog", "movie", "anything"));
    }

    void normalizesLinks()
    {
        QCOMPARE(stremio::normalizeManifestUrl(" stremio://addon.example/abc/manifest.json ").toString(),
                 QStringLiteral("https://addon.example/abc/manifest.json"));
        QVERIFY(stremio::normalizeManifestUrl("https://addon.example/manifest.json").isValid());
        QVERIFY(!stremio::normalizeManifestUrl("https://addon.example/").isValid());
        QVERIFY(!stremio::normalizeManifestUrl("ftp://addon.example/manifest.json").isValid());
        QVERIFY(!stremio::normalizeManifestUrl("magnet:?xt=urn:btih:abc").isValid());
        QCOMPARE(stremio::baseOf(QUrl("https://a.example/conf%7Cig/manifest.json?x=1")),
                 QStringLiteral("https://a.example/conf%7Cig"));
    }

    void buildsResourceUrls()
    {
        QCOMPARE(stremio::resourceUrl("https://a.example/x", "stream", "series", "tt0944947:1:1").toString(QUrl::FullyEncoded),
                 QStringLiteral("https://a.example/x/stream/series/tt0944947%3A1%3A1.json"));
        QCOMPARE(stremio::resourceUrl("https://a.example", "catalog", "movie", "top",
                                      {{"search", "the office & co"}, {"skip", "100"}}).toString(QUrl::FullyEncoded),
                 QStringLiteral("https://a.example/catalog/movie/top/search=the%20office%20%26%20co&skip=100.json"));
    }

    void parsesMetaWithEpisodesInOrder()
    {
        auto d = stremio::parseMeta(json(R"({"meta": {"id": "tt1", "type": "series", "name": "Show", "year": 2011,
            "genres": ["Drama"], "videos": [
                {"id": "tt1:0:1", "title": "Special", "season": 0, "episode": 1},
                {"id": "tt1:2:1", "title": "Two-one", "season": 2, "episode": 1},
                {"id": "tt1:1:2", "name": "One-two", "season": 1, "number": 2, "released": "2011-04-24T00:00:00.000Z"},
                {"id": "tt1:1:1", "title": "One-one", "season": 1, "episode": 1}
            ]}})"));
        QVERIFY(d);
        QCOMPARE(d->preview.releaseInfo, QStringLiteral("2011"));
        QStringList ids;
        for (const VideoEntry& v : d->videos)
            ids << v.id;
        QCOMPARE(ids, (QStringList{"tt1:1:1", "tt1:1:2", "tt1:2:1", "tt1:0:1"}));
        QCOMPARE(d->videos[1].title, QStringLiteral("One-two"));
        QCOMPARE(d->videos[1].released, QStringLiteral("2011-04-24"));
        QVERIFY(!stremio::parseMeta(json(R"({"meta": {}})")));
    }

    void turnsStreamsIntoSources()
    {
        const auto streams = stremio::parseStreams(json(R"({"streams": [
            {"name": "Torrentio\n4k", "title": "Movie.2160p.mkv\n👤 41 💾 15.2 GB", "infoHash": "ABCDEF0123456789ABCDEF0123456789ABCDEF01",
             "fileIdx": 2, "sources": ["tracker:udp:\/\/tr.example:80/announce", "dht:abcdef"],
             "behaviorHints": {"bingeGroup": "torrentio|4k", "filename": "Movie.2160p.mkv"}},
            {"name": "Web 720p", "url": "https:\/\/cdn.example/v.mp4",
             "behaviorHints": {"proxyHeaders": {"request": {"Referer": "https:\/\/site.example/"}}, "videoSize": 1048576},
             "subtitles": [{"url": "https:\/\/cdn.example/en.vtt", "lang": "eng"}]},
            {"name": "Trailer", "ytId": "abc"},
            {"name": "Elsewhere", "externalUrl": "https:\/\/example.com"}
        ]})"), "org.example", "Example");
        QCOMPARE(streams.size(), 2);

        const StreamOption& t = streams[0];
        QCOMPARE(t.source.kind, PlayableSource::Kind::Torrent);
        const QUrlQuery q(t.source.url);
        QCOMPARE(t.source.url.scheme(), QStringLiteral("magnet"));
        QCOMPARE(q.queryItemValue("xt"), QStringLiteral("urn:btih:abcdef0123456789abcdef0123456789abcdef01"));
        QCOMPARE(q.queryItemValue("so"), QStringLiteral("2"));
        QCOMPARE(q.queryItemValue("dn", QUrl::FullyDecoded), QStringLiteral("Movie.2160p.mkv"));
        QCOMPARE(q.allQueryItemValues("tr", QUrl::FullyDecoded), QStringList{"udp://tr.example:80/announce"});
        QCOMPARE(t.quality, 2160);
        QCOMPARE(t.seeders, 41);
        QCOMPARE(t.sizeBytes, qint64(15.2 * 1024 * 1024 * 1024));
        QCOMPARE(t.bingeGroup, QStringLiteral("torrentio|4k"));
        QCOMPARE(t.source.adapter, QStringLiteral("stremio:org.example"));
        QCOMPARE(t.providerName, QStringLiteral("Example"));

        const StreamOption& h = streams[1];
        QCOMPARE(h.source.kind, PlayableSource::Kind::Http);
        QCOMPARE(h.source.headers.value("Referer"), QStringLiteral("https://site.example/"));
        QCOMPARE(h.sizeBytes, qint64(1048576));
        QCOMPARE(h.quality, 720);
        QCOMPARE(h.source.subtitles.size(), 1);
        QCOMPARE(h.source.subtitles[0].language, QStringLiteral("eng"));
    }

    // --- Ranking ----------------------------------------------------------------

    void readsLabels()
    {
        QCOMPARE(ranking::qualityOf("Torrentio\n4k DV"), 2160);
        QCOMPARE(ranking::qualityOf("Movie.1080p.WEB"), 1080);
        QCOMPARE(ranking::qualityOf("720p"), 720);
        QCOMPARE(ranking::qualityOf("CAM"), 0);
        QCOMPARE(ranking::sizeOf("💾 1,5 GB"), qint64(1.5 * 1024 * 1024 * 1024));
        QCOMPARE(ranking::sizeOf("700 MB"), qint64(700) * 1024 * 1024);
        QCOMPARE(ranking::sizeOf("no size"), qint64(-1));
        QCOMPARE(ranking::seedersOf("👤 12 💾 3 GB"), 12);
        QCOMPARE(ranking::seedersOf("Seeders: 7"), 7);
        QCOMPARE(ranking::seedersOf("nothing"), -1);
        QCOMPARE(ranking::qualityLabel(2160), QStringLiteral("4K"));
        QCOMPARE(ranking::qualityLabel(0), QStringLiteral("Other"));
    }

    void ranksBestFirst()
    {
        auto make = [](const char* name, int q, int seeders, qint64 size) {
            StreamOption s;
            s.name = name;
            s.quality = q;
            s.seeders = seeders;
            s.sizeBytes = size;
            return s;
        };
        QList<StreamOption> list = {make("sd", 480, 900, 1), make("hd-few", 1080, 2, 5), make("unknown", 0, 50, 9),
                                    make("hd-many-small", 1080, 40, 2), make("hd-many-big", 1080, 40, 8),
                                    make("4k", 2160, 1, 20)};
        ranking::sortBest(list);
        QStringList order;
        for (const auto& s : list)
            order << s.name;
        QCOMPARE(order, (QStringList{"4k", "hd-many-big", "hd-many-small", "hd-few", "sd", "unknown"}));
    }

    // --- File names -----------------------------------------------------------------

    void parsesFileNames_data()
    {
        QTest::addColumn<QString>("path");
        QTest::addColumn<QString>("title");
        QTest::addColumn<int>("year");
        QTest::addColumn<int>("season");
        QTest::addColumn<int>("episode");
        QTest::addColumn<int>("quality");
        QTest::newRow("release movie") << "/v/The.Matrix.1999.1080p.BluRay.x264-GRP.mkv" << "The Matrix" << 1999 << 0 << 0 << 1080;
        QTest::newRow("plain movie") << "/v/Spirited Away (2001).mp4" << "Spirited Away" << 2001 << 0 << 0 << 0;
        QTest::newRow("year in title") << "/v/2001.A.Space.Odyssey.1968.720p.mkv" << "2001 A Space Odyssey" << 1968 << 0 << 0 << 720;
        QTest::newRow("no year") << "/v/Some_Home_Video.avi" << "Some Home Video" << 0 << 0 << 0 << 0;
        QTest::newRow("tagged") << "/v/[Group] Movie Name 2010 [1080p].mkv" << "Movie Name" << 2010 << 0 << 0 << 1080;
        QTest::newRow("SxxEyy") << "/v/Show.Name.S01E02.Pilot.720p.WEB-DL.mkv" << "Show Name" << 0 << 1 << 2 << 720;
        QTest::newRow("lower s e") << "/v/show name - s3e10 - the end.mkv" << "show name" << 0 << 3 << 10 << 0;
        QTest::newRow("NxNN") << "/v/Show 2x05 Title.mkv" << "Show" << 0 << 2 << 5 << 0;
        QTest::newRow("words") << "/v/Show Season 4 Episode 7.mp4" << "Show" << 0 << 4 << 7 << 0;
        QTest::newRow("show in folders") << "/v/Great Show (2015)/Season 2/03 - Night.mkv" << "Great Show" << 0 << 2 << 3 << 0;
        QTest::newRow("bare SxxEyy") << "/v/Another Show/S01E04.mkv" << "Another Show" << 0 << 1 << 4 << 0;
        QTest::newRow("resolution is not an episode") << "/v/Clip 1920x1080.mp4" << "Clip 1920x1080" << 0 << 0 << 0 << 0;
    }

    void parsesFileNames()
    {
        QFETCH(QString, path);
        QFETCH(QString, title);
        QFETCH(int, year);
        QFETCH(int, season);
        QFETCH(int, episode);
        QFETCH(int, quality);
        const local::ParsedName p = local::parseVideoPath(path);
        QCOMPARE(p.title, title);
        QCOMPARE(p.year, year);
        QCOMPARE(p.season, season);
        QCOMPARE(p.episode, episode);
        QCOMPARE(p.quality, quality);
    }

    void findsSubtitlesNextToVideos()
    {
        QTemporaryDir dir;
        const QString video = dir.filePath("Movie.2010.mkv");
        touch(video);
        touch(dir.filePath("Movie.2010.srt"));
        touch(dir.filePath("Movie.2010.en.srt"));
        touch(dir.filePath("Movie.2010.English.forced.ass"));
        touch(dir.filePath("Other.srt"));                 // another video's
        touch(dir.filePath("Movie.2010.nfo"));            // not a subtitle
        touch(dir.filePath("Subs/2_English.srt"));        // single-video folder: belongs to it
        touch(dir.filePath("Subs/Movie.2010/3_French.srt"));
        const auto subs = local::findSubtitlesFor(video);
        QStringList names, langs;
        for (const SubtitleSource& s : subs) {
            names << QFileInfo(s.url.toLocalFile()).fileName();
            langs << s.language;
        }
        // Subs/<video name>/ wins over loose files in Subs/.
        QCOMPARE(names, (QStringList{"Movie.2010.en.srt", "Movie.2010.English.forced.ass", "Movie.2010.srt", "3_French.srt"}));
        QCOMPARE(langs, (QStringList{"en", "English", "", "French"}));

        // A season folder: loose Subs/ files only count when named for the video.
        QTemporaryDir season;
        touch(season.filePath("Show.S01E01.mkv"));
        touch(season.filePath("Show.S01E02.mkv"));
        touch(season.filePath("Subs/Show.S01E02.en.srt"));
        touch(season.filePath("Subs/Random.srt"));
        QCOMPARE(local::findSubtitlesFor(season.filePath("Show.S01E01.mkv")).size(), 0);
        QCOMPARE(local::findSubtitlesFor(season.filePath("Show.S01E02.mkv")).size(), 1);
    }

    // --- Local library -------------------------------------------------------------

    void localLibraryScansWatchesAndServes()
    {
        QTemporaryDir data, videos;
        touch(videos.filePath("Movies/Alpha.2001.1080p.mkv"), 1000);
        touch(videos.filePath("Movies/Alpha.2001.1080p.en.srt"));
        touch(videos.filePath("Movies/Beta (1999).mp4"));
        touch(videos.filePath("Movies/Beta (1999)/sample.mkv"));  // a sample: skipped
        touch(videos.filePath("Movies/notes.txt"));
        touch(videos.filePath("Shows/Gamma/Season 1/01 - Start.mkv"));
        touch(videos.filePath("Shows/Gamma/Season 1/02 - Middle.mkv"));
        touch(videos.filePath("Shows/Gamma.S02E01.mkv"));
        touch(videos.filePath(".hidden/Delta.mkv"));

        const QString store = data.filePath("local-folders.json");
        LocalLibrary lib(store);
        lib.setRescanDelay(100);
        QSignalSpy changed(&lib, &LocalLibrary::libraryChanged);
        QVERIFY(lib.catalogs().isEmpty());
        QVERIFY(lib.addFolder(videos.path()));
        QVERIFY(!lib.addFolder(videos.filePath("Movies")));  // inside a folder already there
        QVERIFY(changed.wait(10000));
        QStringList found;
        for (const LocalVideo& v : lib.videos())
            found << v.path;
        QVERIFY2(lib.videos().size() == 5, qPrintable(found.join(", ")));
        QCOMPARE(lib.catalogs().size(), 2);

        Waiter<QList<MetaPreview>> movies;
        lib.fetchCatalog(lib.catalogs()[0], {}, 0, [&](const QList<MetaPreview>& items, const QString&) { movies.value = items; });
        QVERIFY(movies.wait());
        QCOMPARE(movies.value->size(), 2);
        QCOMPARE(movies.value->at(0).name, QStringLiteral("Alpha"));
        QCOMPARE(movies.value->at(0).releaseInfo, QStringLiteral("2001"));

        Waiter<QList<MetaPreview>> searched;
        lib.fetchCatalog(lib.catalogs()[0], "bet", 0, [&](const QList<MetaPreview>& items, const QString&) { searched.value = items; });
        QVERIFY(searched.wait());
        QCOMPARE(searched.value->size(), 1);

        Waiter<QList<MetaPreview>> shows;
        lib.fetchCatalog(lib.catalogs()[1], {}, 0, [&](const QList<MetaPreview>& items, const QString&) { shows.value = items; });
        QVERIFY(shows.wait());
        QCOMPARE(shows.value->size(), 1);  // three files, one show
        QCOMPARE(shows.value->at(0).name, QStringLiteral("Gamma"));

        Waiter<MetaDetail> meta;
        lib.fetchMeta(shows.value->at(0), [&](const std::optional<MetaDetail>& d, const QString&) { meta.value = *d; });
        QVERIFY(meta.wait());
        QCOMPARE(meta.value->videos.size(), 3);
        QCOMPARE(meta.value->videos[0].title, QStringLiteral("Start"));
        QCOMPARE(meta.value->videos[2].season, 2);

        // The movie plays from its file, with its subtitle.
        Waiter<QList<StreamOption>> streams;
        bool finished = false;
        lib.fetchStreams("movie", movies.value->at(0).id,
                         [&](const QList<StreamOption>& s, const QString&, const QString&) { streams.value = s; },
                         [&] { finished = true; });
        QVERIFY(streams.wait());
        QTRY_VERIFY(finished);
        QCOMPARE(streams.value->size(), 1);
        const StreamOption s = streams.value->first();
        QCOMPARE(s.source.kind, PlayableSource::Kind::LocalFile);
        QCOMPARE(s.source.url, QUrl::fromLocalFile(videos.filePath("Movies/Alpha.2001.1080p.mkv")));
        QCOMPARE(s.source.title, QStringLiteral("Alpha (2001)"));
        QCOMPARE(s.source.subtitles.size(), 1);
        QCOMPARE(s.quality, 1080);
        QCOMPARE(s.sizeBytes, qint64(1000));

        // An id that is not a scanned file is refused.
        QString refused;
        lib.fetchStreams("movie", "local:/etc/passwd",
                         [&](const QList<StreamOption>&, const QString&, const QString& e) { refused = e; }, [] {});
        QTRY_VERIFY(!refused.isEmpty());
        // Other adapters' ids are not this adapter's business.
        bool otherFinished = false;
        int parts = 0;
        lib.fetchStreams("movie", "tt123", [&](auto&&...) { ++parts; }, [&] { otherFinished = true; });
        QTRY_VERIFY(otherFinished);
        QCOMPARE(parts, 0);

        // A new file appears without asking.
        changed.clear();
        touch(videos.filePath("Movies/Epsilon.2020.mkv"));
        QVERIFY(changed.wait(10000));
        QCOMPARE(lib.videos().size(), 6);

        // The folder list survives a restart.
        LocalLibrary again(store);
        QCOMPARE(again.folders(), QStringList{QDir::cleanPath(videos.path())});
        QSignalSpy againChanged(&again, &LocalLibrary::libraryChanged);
        QVERIFY(againChanged.wait(10000));
        QCOMPARE(again.videos().size(), 6);
        QVERIFY(again.removeFolder(videos.path()));
        QVERIFY(againChanged.wait(10000));
        QCOMPARE(again.videos().size(), 0);
        QVERIFY(again.catalogs().isEmpty());
    }

    // --- Stremio adapter against a fake add-on server ----------------------------------

    void installsAndRemembersAddons()
    {
        FakeHttpServer server;
        server.setJson("/a/manifest.json", manifest("org.a", "Addon A", R"(["catalog","meta","stream"])",
                                                     R"([{"type":"movie","id":"top","name":"Top","extra":[{"name":"search"}]}])"));
        server.setJson("/conf/manifest.json", manifest("org.c", "Needs setup", R"(["stream"])", "[]",
                                                        R"(,"behaviorHints":{"configurationRequired":true})"));
        server.setJson("/notjson/manifest.json", "<html>");
        QTemporaryDir data;
        const QString store = data.filePath("addons.json");

        {
            StremioAdapter addons(store, /*preinstall*/ true);
            QCOMPARE(addons.addons().size(), 1);  // Cinemeta, from the built-in manifest
            QCOMPARE(addons.addons()[0].manifest.id, QStringLiteral("com.linvo.cinemeta"));
            QVERIFY(addons.uninstall("com.linvo.cinemeta"));

            auto install = [&](const QString& link) {
                Waiter<QString> w;
                addons.install(link, [&](const QString& e) { w.value = e; });
                w.wait();
                return w.value.value_or("no answer");
            };
            QSignalSpy catalogs(&addons, &SourceAdapter::catalogsChanged);
            QCOMPARE(install(server.base() + "/a/manifest.json"), QString());
            QCOMPARE(catalogs.size(), 1);
            QVERIFY(install(server.base() + "/a/").contains("manifest.json"));
            QVERIFY(install(server.base() + "/conf/manifest.json").contains("set up"));
            QVERIFY(install(server.base() + "/notjson/manifest.json").contains("not JSON"));
            QVERIFY(install(server.base() + "/missing/manifest.json").contains("HTTP 404"));
            QCOMPARE(addons.addons().size(), 1);
            // Installing again updates in place.
            QCOMPARE(install(server.base() + "/a/manifest.json"), QString());
            QCOMPARE(addons.addons().size(), 1);
            QCOMPARE(addons.catalogs().size(), 1);
            QCOMPARE(addons.catalogs()[0].name, QStringLiteral("Addon A · Top movies"));
        }
        // A restart keeps the list, without asking the network, and does
        // not bring back the removed Cinemeta.
        const int asked = int(server.requests.size());
        StremioAdapter again(store, true);
        QCOMPARE(again.addons().size(), 1);
        QCOMPARE(again.addons()[0].manifest.name, QStringLiteral("Addon A"));
        QCOMPARE(int(server.requests.size()), asked);
    }

    void asksTheRightAddonsAndSurvivesBadOnes()
    {
        FakeHttpServer server;
        server.setJson("/list/manifest.json", manifest("org.list", "Lists", R"(["catalog"])",
            R"([{"type":"series","id":"pop","name":"Popular","extra":[{"name":"search"},{"name":"skip"}]}])"));
        server.setJson("/meta/manifest.json", manifest("org.meta", "Details", R"(["meta"])"));
        server.setJson("/fast/manifest.json", manifest("org.fast", "Fast", R"(["stream"])"));
        server.setJson("/slow/manifest.json", manifest("org.slow", "Slow", R"(["stream"])"));
        server.setJson("/broken/manifest.json", manifest("org.broken", "Broken", R"(["stream","subtitles"])"));
        server.setJson("/kitsu/manifest.json", R"({"id":"org.kitsu","name":"Anime","types":["series"],"idPrefixes":["kitsu"],"resources":["stream"]})");
        server.setJson("/subs/manifest.json", manifest("org.subs", "Subs", R"(["subtitles"])"));

        server.setJson("/list/catalog/series/pop.json", R"({"metas":[{"id":"tt1","type":"series","name":"Show One"},{"id":"tt2","type":"series","name":"Show Two"}]})");
        server.setJson("/list/catalog/series/pop/search=Show%20One.json", R"({"metas":[{"id":"tt1","type":"series","name":"Show One"}]})");
        server.setJson("/meta/meta/series/tt1.json", R"({"meta":{"id":"tt1","type":"series","name":"Show One","videos":[{"id":"tt1:1:1","season":1,"episode":1,"title":"Pilot"}]}})");
        server.setJson("/fast/stream/series/tt1%3A1%3A1.json", R"({"streams":[
            {"name":"Fast 720p","infoHash":"0123456789abcdef0123456789abcdef01234567","fileIdx":0},
            {"name":"Fast 1080p","url":"http:\/\/127.0.0.1:1/v.mkv"}]})");
        server.setJson("/slow/stream/series/tt1%3A1%3A1.json", R"({"streams":[]})", /*delay*/ -1);
        server.set("/broken/stream/series/tt1%3A1%3A1.json", {500, "oops", "text/plain", 0, {}});
        server.setJson("/subs/subtitles/series/tt1%3A1%3A1.json", R"({"subtitles":[{"url":"http:\/\/127.0.0.1:1/en.srt","lang":"eng"}]})");

        QTemporaryDir data;
        StremioAdapter addons(data.filePath("addons.json"), false);
        addons.fetcher().setTimeout(1500);
        for (const char* name : {"list", "meta", "fast", "slow", "broken", "kitsu", "subs"}) {
            Waiter<QString> w;
            addons.install(server.base() + "/" + name + "/manifest.json", [&](const QString& e) { w.value = e; });
            QVERIFY(w.wait());
            QCOMPARE(*w.value, QString());
        }

        FakeTarget target;
        Session session(&target);
        Browser browser(&session);
        browser.setStremio(&addons);
        QCOMPARE(browser.catalogRefs().size(), 1);
        QCOMPARE(browser.catalogs()[0].toMap().value("name").toString(), QStringLiteral("Lists · Popular series"));

        browser.openCatalog(0);
        QTRY_COMPARE(browser.itemList().size(), 2);
        QVERIFY(browser.canLoadMore());
        browser.openCatalog(0, "Show One");
        QTRY_COMPARE(browser.itemList().size(), 1);
        QVERIFY(server.paths().contains("/list/catalog/series/pop/search=Show%20One.json"));

        // Details come from the add-on that has them, not the list's.
        browser.openItem(0);
        QTRY_VERIFY(!browser.detailLoading());
        QCOMPARE(browser.videos().size(), 1);
        QVERIFY(server.paths().contains("/meta/meta/series/tt1.json"));

        // Streams: Fast answers, Broken fails, Slow times out, Anime is not asked.
        QElapsedTimer clock;
        clock.start();
        browser.openVideo(0);
        QVERIFY(browser.streamsLoading());
        QTRY_VERIFY_WITH_TIMEOUT(!browser.streamsLoading(), 10000);
        QVERIFY(clock.elapsed() < 8000);
        QCOMPARE(browser.streamList().size(), 2);
        QCOMPARE(browser.streamList()[0].quality, 1080);  // ranked
        QCOMPARE(browser.streamsTitle(), QStringLiteral("Show One 1x01"));
        QStringList problems = browser.streamProblems();
        problems.sort();
        QCOMPARE(problems, (QStringList{"Broken: HTTP 500", "Slow: timed out"}));
        for (const QString& p : server.paths())
            QVERIFY2(!p.startsWith("/kitsu/stream"), qPrintable(p));
        // Each add-on shows its last error.
        for (const QVariant& a : browser.addons()) {
            const QVariantMap m = a.toMap();
            if (m["id"] == "org.slow")
                QCOMPARE(m["error"].toString(), QStringLiteral("timed out"));
            if (m["id"] == "org.fast")
                QCOMPARE(m["error"].toString(), QString());
        }

        // Playing hands the source to the Session, then adds subtitle add-on
        // files as tracks to that item.
        browser.play(1);  // the torrent
        QVERIFY(target.sent("load magnet:?xt=urn:btih:0123456789abcdef0123456789abcdef01234567"));
        QTRY_VERIFY(target.sent("subtitleTrack+ http://127.0.0.1:1/en.srt eng"));
    }

    void staleAnswersAreDropped()
    {
        FakeHttpServer server;
        server.setJson("/a/manifest.json", manifest("org.a", "A", R"(["catalog"])",
            R"([{"type":"movie","id":"slow","name":"Slow"},{"type":"movie","id":"quick","name":"Quick"}])"));
        server.setJson("/a/catalog/movie/slow.json", R"({"metas":[{"id":"tt9","type":"movie","name":"Late"}]})", 800);
        server.setJson("/a/catalog/movie/quick.json", R"({"metas":[{"id":"tt1","type":"movie","name":"On time"}]})");
        QTemporaryDir data;
        StremioAdapter addons(data.filePath("addons.json"), false);
        Waiter<QString> w;
        addons.install(server.base() + "/a/manifest.json", [&](const QString& e) { w.value = e; });
        QVERIFY(w.wait());

        Browser browser(nullptr);
        browser.setStremio(&addons);
        browser.openCatalog(0);
        browser.openCatalog(1);
        QTRY_COMPARE(browser.itemList().size(), 1);
        QTest::qWait(1200);  // the slow answer has arrived by now
        QCOMPARE(browser.itemList().size(), 1);
        QCOMPARE(browser.itemList()[0].name, QStringLiteral("On time"));
    }

    void boundsResponseSize()
    {
        FakeHttpServer server;
        server.setJson("/big.json", "{\"x\":\"" + QByteArray(300 * 1024, 'a') + "\"}");
        JsonFetcher fetcher;
        fetcher.setMaxBytes(100 * 1024);
        Waiter<QString> w;
        fetcher.get(QUrl(server.base() + "/big.json"), [&](const QJsonObject&, const QString& e) { w.value = e; });
        QVERIFY(w.wait());
        QCOMPARE(*w.value, QStringLiteral("the answer was too large"));
    }
};

QTEST_MAIN(TestSources)
#include "tst_sources.moc"
