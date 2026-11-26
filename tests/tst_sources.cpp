// (JSON in raw strings writes "://" as ":\/\/": moc reads "//" there as a comment.)
// Step 5 without a player: the Stremio protocol, stream ranking, file name
// parsing, the local library, and the add-on adapter and browse model
// against a fake add-on server on 127.0.0.1.

#include "Browser.h"
#include "FakeHttpServer.h"
#include "FakeTarget.h"
#include "LocalLibrary.h"
#include "JspClient.h"
#include "JspProtocol.h"
#include "LocalNames.h"
#include "Session.h"
#include "StreamRanking.h"
#include "StremioAdapter.h"
#include "StremioProtocol.h"
#include "WikiMazeSearch.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QUrlQuery>
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
    QVERIFY(f.resize(size));
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
        QCOMPARE(stremio::resourceUrl("https://a.example/x", "stream", "series", "tt1234567:1:1").toString(QUrl::FullyEncoded),
                 QStringLiteral("https://a.example/x/stream/series/tt1234567%3A1%3A1.json"));
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
            {"name": "Example\n4k", "title": "Movie.2160p.mkv\n👤 41 💾 15.2 GB", "infoHash": "ABCDEF0123456789ABCDEF0123456789ABCDEF01",
             "fileIdx": 2, "sources": ["tracker:udp:\/\/tr.example:80/announce", "dht:abcdef"],
             "behaviorHints": {"bingeGroup": "example|4k", "filename": "Movie.2160p.mkv"}},
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
        QCOMPARE(t.bingeGroup, QStringLiteral("example|4k"));
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
        QCOMPARE(ranking::qualityOf("Example\n4k DV"), 2160);
        QCOMPARE(ranking::qualityOf("Movie.1080p.WEB"), 1080);
        QCOMPARE(ranking::qualityOf("720p"), 720);
        QCOMPARE(ranking::qualityOf("CAM"), 0);
        QCOMPARE(ranking::sizeOf("💾 1,5 GB"), qint64(1.5 * 1024 * 1024 * 1024));
        QCOMPARE(ranking::sizeOf("700 MB"), qint64(700) * 1024 * 1024);
        QCOMPARE(ranking::sizeOf("no size"), qint64(-1));
        // Found by tests/fuzz: a huge number overflowed qint64.
        QCOMPARE(ranking::sizeOf(QStringLiteral("%1 MB").arg(QString(80, QLatin1Char('9')))), qint64(-1));
        QCOMPARE(ranking::sizeOf("1025 TB"), qint64(-1));
        QCOMPARE(ranking::sizeOf("1024 TB"), qint64(1024) * 1024 * 1024 * 1024 * 1024);
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
        QTest::newRow("release movie") << "/v/Night.of.the.Living.Dead.1968.1080p.BluRay.x264-GRP.mkv" << "Night of the Living Dead" << 1968 << 0 << 0 << 1080;
        QTest::newRow("plain movie") << "/v/Nosferatu (1922).mp4" << "Nosferatu" << 1922 << 0 << 0 << 0;
        QTest::newRow("year in title") << "/v/1984.Garden.Tour.2010.720p.mkv" << "1984 Garden Tour" << 2010 << 0 << 0 << 720;
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

    // Pages of a big local catalog come in name order, with nothing twice
    // and nothing missing, searched or not.
    void localCatalogPagesInOrder()
    {
        QTemporaryDir videos;
        QStringList names;
        for (int i = 0; i < 600; ++i) {
            const QString name = QStringLiteral("Title %1").arg((i * 7919) % 1000);
            names << name;
            touch(videos.filePath(QStringLiteral("Movies/%1.mkv").arg(name)));
        }
        LocalLibrary lib;
        QSignalSpy changed(&lib, &LocalLibrary::libraryChanged);
        QVERIFY(lib.addFolder(videos.path()));
        QVERIFY(changed.wait(10000));
        QCOMPARE(lib.videos().size(), 600);

        const auto all = [&](const QString& search) {
            QStringList got;
            for (int skip = 0;; skip += LocalLibrary::kPageSize) {
                Waiter<QList<MetaPreview>> page;
                lib.fetchCatalog(lib.catalogs()[0], search, skip,
                                 [&](const QList<MetaPreview>& items, const QString&) { page.value = items; });
                if (!page.wait())
                    return QStringList{QStringLiteral("(no answer)")};
                for (const MetaPreview& m : *page.value)
                    got << m.name;
                if (page.value->size() < LocalLibrary::kPageSize)
                    return got;
            }
        };
        const auto sorted = [](QStringList list) {
            std::stable_sort(list.begin(), list.end(),
                             [](const QString& a, const QString& b) { return QString::localeAwareCompare(a, b) < 0; });
            return list;
        };
        QCOMPARE(all({}), sorted(names));
        QStringList withSeven;
        for (const QString& n : names)
            if (n.contains('7'))
                withSeven << n;
        QVERIFY(withSeven.size() > LocalLibrary::kPageSize);  // the search spans pages too
        QCOMPARE(all(QStringLiteral("7")), sorted(withSeven));
    }

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
            // WikiMaze and Public Domain Movies, from the built-in manifests.
            QCOMPARE(addons.addons().size(), 2);
            QCOMPARE(addons.addons()[0].manifest.id, QStringLiteral("community.wikimaze"));
            QCOMPARE(addons.addons()[1].manifest.id, QStringLiteral("org.stremio.pubdomainmovies"));
            QCOMPARE(addons.addons()[1].manifestUrl.toString(),
                     QStringLiteral("https://caching.stremio.net/publicdomainmovies.now.sh/manifest.json"));
            QCOMPARE(addons.catalogs().size(), 10);   // WikiMaze's 9 and the public-domain films
            QVERIFY(addons.uninstallDefaults());
            QVERIFY(!addons.uninstallDefaults());

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
            // Switched off: kept, but gives no lists and is asked for nothing.
            QVERIFY(addons.setEnabled("org.a", false));
            QVERIFY(!addons.setEnabled("org.a", false));
            QVERIFY(addons.catalogs().isEmpty());
            const auto before = server.requests.size();
            Waiter<bool> done;
            addons.fetchStreams("movie", "tt1", [](const QList<StreamOption>&, const QString&, const QString&) {}, [&] { done.value = true; });
            done.wait();
            QCOMPARE(server.requests.size(), before);
        }
        // A restart keeps the list, without asking the network, and does
        // not bring back the removed defaults.
        const int asked = int(server.requests.size());
        StremioAdapter again(store, true);
        QCOMPARE(again.addons().size(), 1);
        QCOMPARE(again.addons()[0].manifest.name, QStringLiteral("Addon A"));
        QCOMPARE(int(server.requests.size()), asked);
        // ...and that it was switched off.
        QVERIFY(again.addons()[0].disabled);
        QVERIFY(again.setEnabled("org.a", true));
        QCOMPARE(again.catalogs().size(), 1);
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
        FakeHttpServer::Answer broken;
        broken.status = 500;
        broken.body = "oops";
        broken.contentType = "text/plain";
        server.set("/broken/stream/series/tt1%3A1%3A1.json", broken);
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
        // A refused stream must leave the playing episode and watch history alone.
        QSignalSpy historySelection(&browser, &Browser::aboutToPlay);
        QSignalSpy started(&browser, &Browser::playStarted);
        session.setSourceCheck([](const PlayableSource&) -> Session::SourceCheck::result_type {
            return std::pair{PlaybackError::TorrentsOff, QStringLiteral("Torrents are off")};
        });
        browser.play(1);
        QCOMPARE(started.count(), 0);
        QCOMPARE(historySelection.count(), 0);
        QVERIFY(browser.playingEpisodes().isEmpty());
        session.setSourceCheck({});

        browser.play(1);  // the torrent
        QVERIFY(target.sent("load magnet:?xt=urn:btih:0123456789abcdef0123456789abcdef01234567"));
        QTRY_VERIFY(target.sent("subtitleTrack+ http://127.0.0.1:1/en.srt eng"));
    }

    // An add-on's subtitle must be a web link: not a file on this computer,
    // not a relative path, not another protocol mpv would try.
    void remoteSubtitleSchemesAreRestricted_data()
    {
        QTest::addColumn<QString>("url");
        QTest::addColumn<bool>("kept");
        QTest::newRow("local-file") << "file:///tmp/private.srt" << false;
        QTest::newRow("relative") << "private.srt" << false;
        QTest::newRow("absolute-path") << "/etc/passwd" << false;
        QTest::newRow("unsupported") << "ftp://example.com/private.srt" << false;
        QTest::newRow("mpv-protocol") << "memory://hello" << false;
        QTest::newRow("no-host") << "https:///x.srt" << false;
        QTest::newRow("https-control") << "https://example.com/en.srt" << true;
        QTest::newRow("http") << "http://example.com/en.srt" << true;
    }

    void remoteSubtitleSchemesAreRestricted()
    {
        QFETCH(QString, url);
        QFETCH(bool, kept);
        const QJsonObject json{{"subtitles", QJsonArray{QJsonObject{{"url", url}, {"lang", "en"}}}}};
        QCOMPARE(stremio::parseSubtitles(json).size(), kept ? 1 : 0);
        // The same rule for subtitles that come with a stream.
        const QJsonObject stream{{"streams", QJsonArray{QJsonObject{{"url", "https://example.com/v.mkv"},
                                                                    {"subtitles", json.value("subtitles")}}}}};
        QCOMPARE(stremio::parseStreams(stream, "a", "A").value(0).source.subtitles.size(), kept ? 1 : 0);
    }

    // Header values from an add-on cannot carry a line break (a header or a
    // request of the add-on's choosing) or any other control character.
    void streamHeadersRejectControlCharacters()
    {
        const auto parse = [](const QString& name, const QString& value) {
            const QJsonObject hints{{"proxyHeaders", QJsonObject{{"request", QJsonObject{{name, value}, {"Referer", "https://ok.example/"}}}}}};
            const QJsonObject json{{"streams", QJsonArray{QJsonObject{{"url", "https://example.com/v.mkv"}, {"behaviorHints", hints}}}}};
            return stremio::parseStreams(json, "a", "A").value(0).source.headers;
        };
        QCOMPARE(parse("X-Token", "abc").value("X-Token"), "abc");
        QVERIFY(!parse("X-Token", "abc\r\nX-Evil: 1").contains("X-Token"));
        QVERIFY(!parse("X-Token", QString("a") + QChar(0) + "b").contains("X-Token"));
        QVERIFY(!parse("X-Bad\r\nName", "v").contains("X-Bad\r\nName"));
        QVERIFY(!parse("Bad Name", "v").contains("Bad Name"));
        QCOMPARE(parse("X-Token", "a\tb").value("X-Token"), "a\tb");  // tab is allowed in values
        QCOMPARE(parse("X-Token", "abc\r\nX-Evil: 1").value("Referer"), "https://ok.example/");
    }

    // An add-on's videoSize is a number of any size; beyond qint64 the
    // conversion would be undefined.
    void streamSizesAreBounded()
    {
        const auto size = [](const QJsonValue& v) {
            const QJsonObject json{{"streams", QJsonArray{QJsonObject{{"url", "https://example.com/v.mkv"},
                                                                       {"behaviorHints", QJsonObject{{"videoSize", v}}}}}}};
            return stremio::parseStreams(json, "a", "A").value(0).sizeBytes;
        };
        QCOMPARE(size(1048576), qint64(1048576));
        QCOMPARE(size(1e300), qint64(-1));
        QCOMPARE(size(-5), qint64(-1));
        QCOMPARE(size("12"), qint64(-1));
    }

    // A local video's id is its path. Add-ons, even ones that take any id,
    // must not hear it: it holds the user's folder and file names.
    void localPathsStayOnThisComputer()
    {
        FakeHttpServer server;
        // No idPrefixes: it takes any id.
        server.setJson("/any/manifest.json", R"({"id":"org.any","version":"1.0.0","name":"Any","types":["movie","series"],
                                                 "resources":["stream","subtitles"],"catalogs":[]})");
        QTemporaryDir data, videos;
        touch(videos.filePath("Private Folder/Alpha.2001.mkv"), 1000);

        StremioAdapter addons(data.filePath("addons.json"), false);
        Waiter<QString> installed;
        addons.install(server.base() + "/any/manifest.json", [&](const QString& e) { installed.value = e; });
        QVERIFY(installed.wait());
        LocalLibrary lib(data.filePath("local-folders.json"));
        QSignalSpy changed(&lib, &LocalLibrary::libraryChanged);
        QVERIFY(lib.addFolder(videos.path()));
        QVERIFY(changed.wait(10000));

        FakeTarget target;
        Session session(&target);
        Browser browser(&session);
        browser.setStremio(&addons);
        browser.setLocal(&lib);
        Waiter<QList<MetaPreview>> movies;
        lib.fetchCatalog(lib.catalogs()[0], {}, 0, [&](const QList<MetaPreview>& items, const QString&) { movies.value = items; });
        QVERIFY(movies.wait());
        QCOMPARE(movies.value->size(), 1);

        browser.openPreview(movies.value->first());
        QTRY_VERIFY(!browser.detailLoading());
        browser.openVideo(-1);
        QTRY_VERIFY_WITH_TIMEOUT(!browser.streamsLoading(), 10000);
        QCOMPARE(browser.streamList().size(), 1);  // the file itself
        browser.play(0);
        QTRY_VERIFY(target.sent("load"));
        QTest::qWait(300);
        for (const QString& p : server.paths())
            QVERIFY2(p == "/any/manifest.json", qPrintable(p));
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

    void homeRowsSearchGenresAndOrder()
    {
        FakeHttpServer server;
        server.setJson("/a/manifest.json", manifest("org.a", "A", R"(["catalog"])",
            R"([{"type":"movie","id":"top","name":"Top","extra":[{"name":"search"},{"name":"genre","options":["Drama","Comedy"]}]},
                {"type":"series","id":"find","name":"Find","extra":[{"name":"search","isRequired":true}]}])"));
        server.setJson("/b/manifest.json", manifest("org.b", "B", R"(["catalog"])",
            R"([{"type":"series","id":"new","name":"New","extra":[{"name":"search"}]}])"));
        server.setJson("/a/catalog/movie/top.json", R"({"metas":[{"id":"tt1","type":"movie","name":"One"}]})");
        server.setJson("/a/catalog/movie/top/genre=Comedy.json", R"({"metas":[{"id":"tt5","type":"movie","name":"Funny"}]})");
        server.setJson("/a/catalog/movie/top/search=fox.json", R"({"metas":[{"id":"tt2","type":"movie","name":"Fox movie"}]})");
        server.setJson("/a/catalog/series/find/search=fox.json", R"({"metas":[{"id":"tt3","type":"series","name":"Fox show"}]})");
        server.setJson("/b/catalog/series/new.json", R"({"metas":[{"id":"tt4","type":"series","name":"Four"}]})");
        server.setJson("/b/catalog/series/new/search=fox.json", R"({"metas":[]})");
        QTemporaryDir data;
        StremioAdapter addons(data.filePath("addons.json"), false);
        for (const char* name : {"a", "b"}) {
            Waiter<QString> w;
            addons.install(server.base() + "/" + name + "/manifest.json", [&](const QString& e) { w.value = e; });
            QVERIFY(w.wait());
        }
        Browser browser(nullptr);
        browser.setStremio(&addons);

        // Home: every list that can be browsed, not the search-only one.
        browser.loadHome();
        QCOMPARE(browser.homeRows().size(), 2);
        QTRY_VERIFY(!browser.homeRows()[1].toMap().value("loading").toBool());
        QTRY_VERIFY(!browser.homeRows()[0].toMap().value("loading").toBool());
        QCOMPARE(browser.homeRows()[0].toMap().value("items").toList().size(), 1);
        QCOMPARE(browser.homeRows()[1].toMap().value("name").toString(), QStringLiteral("B · New series"));
        browser.openRowItem(false, 1, 0);
        QCOMPARE(browser.detail().value("id").toString(), QStringLiteral("tt4"));

        // Search across every add-on, all types or one.
        browser.searchAll("fox");
        QCOMPARE(browser.searchRows().size(), 3);
        const auto searching = [&] {
            const QVariantList rows = browser.searchRows();
            return std::any_of(rows.begin(), rows.end(), [](const QVariant& r) { return r.toMap().value("loading").toBool(); });
        };
        QTRY_VERIFY(!searching());
        QCOMPARE(browser.searchRows()[0].toMap().value("error").toString(), QString());
        QCOMPARE(browser.searchRows()[0].toMap().value("items").toList().size(), 1);
        QCOMPARE(browser.searchRows()[1].toMap().value("items").toList()[0].toMap().value("name").toString(),
                 QStringLiteral("Fox show"));
        browser.searchAll("fox", "movie");
        QCOMPARE(browser.searchRows().size(), 1);

        // A genre filter.
        QCOMPARE(browser.catalogs()[0].toMap().value("genres").toStringList(), (QStringList{"Drama", "Comedy"}));
        browser.openCatalog(0, {}, "Comedy");
        QCOMPARE(browser.genre(), QStringLiteral("Comedy"));
        QTRY_COMPARE(browser.itemList().size(), 1);
        QCOMPARE(browser.itemList()[0].name, QStringLiteral("Funny"));

        // Moving B up puts its lists first, and the home rows follow.
        browser.moveAddon("org.b", -1);
        QCOMPARE(addons.addons()[0].manifest.id, QStringLiteral("org.b"));
        QTRY_COMPARE(browser.homeRows().value(0).toMap().value("name").toString(), QStringLiteral("B · New series"));
    }

    void homeFillsInWhenTheFirstAddonArrives()
    {
        FakeHttpServer server;
        server.setJson("/a/manifest.json", manifest("org.a", "A", R"(["catalog"])",
            R"([{"type":"movie","id":"top","name":"Top"}])"));
        server.setJson("/a/catalog/movie/top.json", R"({"metas":[{"id":"tt1","type":"movie","name":"One"}]})");
        QTemporaryDir data;
        StremioAdapter addons(data.filePath("addons.json"), false);
        Browser browser(nullptr);
        browser.setStremio(&addons);
        browser.loadHome();
        QVERIFY(browser.homeRows().isEmpty());  // nothing installed yet

        Waiter<QString> w;
        addons.install(server.base() + "/a/manifest.json", [&](const QString& e) { w.value = e; });
        QVERIFY(w.wait());
        QTRY_COMPARE(browser.homeRows().size(), 1);
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

    // A small gzip that inflates past the cap is caught on the inflated size.
    void boundsDecompressedSize()
    {
        FakeHttpServer server;
        const QByteArray inflated = "{\"x\":\"" + QByteArray(4 * 1024 * 1024, 'a') + "\"}";
        FakeHttpServer::Answer bomb;
        bomb.body = gzip(inflated);
        QVERIFY(bomb.body.size() < 64 * 1024);
        bomb.headers = "Content-Encoding: gzip\r\n";
        server.set("/bomb.json", bomb);
        JsonFetcher fetcher;
        fetcher.setMaxBytes(1024 * 1024);
        Waiter<QString> w;
        fetcher.get(QUrl(server.base() + "/bomb.json"), [&](const QJsonObject&, const QString& e) { w.value = e; });
        QVERIFY(w.wait());
        QCOMPARE(*w.value, QStringLiteral("the answer was too large"));
    }

    // A server that never goes quiet but never finishes still ends.
    // --- The Jackie Source Protocol (docs/PROTOCOL.md) --------------------------------

    void jspEnvelopeAndPaths()
    {
        QVERIFY(jsp::isJsp(json(R"({"protocol":1})")));
        QVERIFY(!jsp::isJsp(json(R"({"id":"x"})")));
        QVERIFY(jsp::parseEnvelope(json(R"({"ok":true,"v":1,"ttl":9999,"data":{"a":1}})")).ok);
        QCOMPARE(jsp::parseEnvelope(json(R"({"ok":true,"v":1,"ttl":9999,"data":{}})")).ttl, 3600);
        const jsp::Envelope bad = jsp::parseEnvelope(json(R"({"ok":false,"v":1,"error":{"code":"rate_limited","message":"Slow","retryAfter":30}})"));
        QVERIFY(!bad.ok);
        QCOMPARE(bad.retryAfter, 30);
        QVERIFY(!jsp::isDefinitive(bad));
        QVERIFY(jsp::isDefinitive(jsp::parseEnvelope(json(R"({"ok":false,"v":1,"error":{"code":"not_found"}})"))));
        // Anything that is not an envelope is "unavailable", never a crash.
        QCOMPARE(jsp::parseEnvelope(json(R"({"metas":[]})")).code, QStringLiteral("unavailable"));
        QCOMPARE(jsp::catalogPath("movie", "top", "a b", "", "2020s", 0), QStringLiteral("/catalog/movie/top?q=a%20b&year=2020s"));
        QCOMPARE(jsp::catalogPath("movie", "top", "", "", "", 40), QStringLiteral("/catalog/movie/top?skip=40"));
        const QStringList m = jsp::mirrors(json(R"({"mirrors":["https:\/\/a.example/","http:\/\/evil.example","http:\/\/127.0.0.1:1/x","https:\/\/a.example"]})"));
        QCOMPARE(m, (QStringList{"https://a.example", "http://127.0.0.1:1/x"}));
    }

    void jspStreamsBecomePlayableSources()
    {
        const QJsonObject data = json(R"({"streams":[
            {"kind":"torrent","infoHash":"ABCDEF0123456789ABCDEF0123456789ABCDEF01","fileIdx":3,"trackers":["udp:\/\/t.example:80"],"name":"Ex 4K","detail":"HDR","size":1000,"group":"g"},
            {"kind":"http","url":"https:\/\/cdn.example/a.mp4","headers":{"Referer":"https:\/\/x.example"},"name":"Web"},
            {"kind":"http","url":"file:\/\/\/etc\/passwd","name":"Bad"},
            {"kind":"from-the-future","name":"Unknown"}]})");
        const QList<StreamOption> s = stremio::parseStreams(jsp::streamsAsStremio(data), "src", "Source");
        QCOMPARE(s.size(), 2);   // only the unknown and the unsafe items are dropped
        QCOMPARE(s[0].source.kind, PlayableSource::Kind::Torrent);
        QVERIFY(s[0].source.url.toString().contains("btih:abcdef"));
        QVERIFY(s[0].source.url.toString().contains("so=3"));
        QVERIFY(s[0].source.url.toString().contains("tr="));
        QCOMPARE(s[0].sizeBytes, qint64(1000));
        QCOMPARE(s[1].source.kind, PlayableSource::Kind::Http);
        QCOMPARE(s[1].source.headers.value("Referer"), QStringLiteral("https://x.example"));
    }

    void jspSourceServesCatalogsThroughTheAdapter()
    {
        FakeHttpServer server;
        server.setJson("/j/manifest.json", R"({"protocol":1,"id":"org.jsp","name":"Jackie Src","version":"1.0.0",
            "resources":["catalog","meta","streams"],"types":["movie"],"idPrefixes":["tt"],
            "catalogs":[{"type":"movie","id":"top","name":"Top","search":true,"genres":["drama"],"years":["2026","Older"]}]})");
        server.setJson("/j/catalog/movie/top", R"({"ok":true,"v":1,"ttl":60,"data":{"items":[{"id":"tt1","type":"movie","name":"One","year":2020}],"total":1}})");
        server.setJson("/j/streams/movie/tt1", R"({"ok":true,"v":1,"ttl":60,"data":{"streams":[{"kind":"http","url":"https:\/\/cdn.example/a.mp4","name":"Web"}]}})");
        FakeHttpServer::Answer notFound;
        notFound.status = 404;   // a real HTTP 404 with the typed envelope
        notFound.body = R"({"ok":false,"v":1,"error":{"code":"not_found","message":"no such title"}})";
        server.set("/j/meta/movie/tt9", notFound);
        QTemporaryDir data;
        StremioAdapter addons(data.filePath("a.json"), false);
        Waiter<QString> installed;
        addons.install(server.base() + "/j/manifest.json", [&](const QString& e) { installed.value = e; });
        QVERIFY(installed.wait());
        QCOMPARE(*installed.value, QString());
        QVERIFY(addons.addons()[0].jsp);
        const QList<CatalogRef> catalogs = addons.catalogs();
        QCOMPARE(catalogs.size(), 1);
        QVERIFY(catalogs[0].searchable);
        QCOMPARE(catalogs[0].years, (QStringList{"2026", "Older"}));

        using Items = std::pair<QList<MetaPreview>, QString>;
        Waiter<Items> got;
        addons.fetchCatalog(catalogs[0], "", 0, [&](const QList<MetaPreview>& items, const QString& e) { got.value = Items{items, e}; });
        QVERIFY(got.wait());
        QCOMPARE(got.value->second, QString());
        QCOMPARE(got.value->first.size(), 1);
        QCOMPARE(got.value->first[0].releaseInfo, QStringLiteral("2020"));

        Waiter<QList<StreamOption>> streams;
        addons.fetchStreams("movie", "tt1", [&](const QList<StreamOption>& s, const QString&, const QString&) { streams.value = s; }, [] {});
        QVERIFY(streams.wait());
        QCOMPARE(streams.value->size(), 1);

        // A definitive error is final and in words; the mirror list is not asked.
        MetaPreview missing;
        missing.adapter = "stremio";
        missing.provider = "org.jsp";
        missing.type = "movie";
        missing.id = "tt9";
        Waiter<QString> metaError;
        addons.fetchMeta(missing, [&](const std::optional<MetaDetail>& m, const QString& e) { metaError.value = m ? QString() : e; });
        QVERIFY(metaError.wait());
        QVERIFY(metaError.value->contains("not_found"));
        QCOMPARE(server.paths().count(QStringLiteral("/j/meta/movie/tt9")), 1);   // not asked again, not even the same address
    }

    void jspTriesTheMirrorWhenTheFirstFails()
    {
        FakeHttpServer primary, mirror;
        FakeHttpServer::Answer down;
        down.status = 503;
        down.body = R"({"ok":false,"v":1,"error":{"code":"upstream_down","message":"data provider failed"}})";
        primary.set("/x/catalog/movie/top", down);
        mirror.setJson("/x/catalog/movie/top", R"({"ok":true,"v":1,"data":{"items":[{"id":"tt1","type":"movie","name":"One"}]}})");
        mirror.setJson("/x/catalog/movie/top2", R"({"ok":true,"v":1,"data":{"items":[]}})");
        JspClient client;
        Waiter<QString> w;
        QJsonObject got;
        client.get("s", {primary.base() + "/x", mirror.base() + "/x"}, "/catalog/movie/top",
                   [&](const QJsonObject& d, const QString& e, bool) { got = d; w.value = e; });
        QVERIFY(w.wait());
        QCOMPARE(*w.value, QString());
        QCOMPARE(got.value("items").toArray().size(), 1);
        QVERIFY(primary.requests.size() >= 1);
        // The mirror that worked is asked first next time.
        QJsonObject again;
        Waiter<QString> w2;
        client.get("s", {primary.base() + "/x", mirror.base() + "/x"}, "/catalog/movie/top2",
                   [&](const QJsonObject& d, const QString& e, bool) { again = d; w2.value = e; });
        QVERIFY(w2.wait());
        QCOMPARE(primary.requests.size(), 1);
    }

    void jspSlowSourceIsHedgedNotWaitedFor()
    {
        FakeHttpServer slow, fast;
        FakeHttpServer::Answer hang;
        hang.delayMs = -1;
        slow.set("/catalog/movie/top", hang);
        fast.setJson("/catalog/movie/top", R"({"ok":true,"v":1,"data":{"items":[]}})");
        JspClient client;
        client.setHedgeMs(200);
        Waiter<QString> w;
        QElapsedTimer clock;
        clock.start();
        client.get("s", {slow.base(), fast.base()}, "/catalog/movie/top", [&](const QJsonObject&, const QString& e, bool) { w.value = e; });
        QVERIFY(w.wait(5000));
        QCOMPARE(*w.value, QString());
        QVERIFY2(clock.elapsed() < 3000, "the second request should not wait for the first one's 10 s limit");
    }

    void jspSameAddressIsAskedAgainWhenThereIsNoMirror()
    {
        FakeHttpServer server;
        FakeHttpServer::Answer first;
        first.delayMs = 1500;   // slower than the hedge
        first.body = R"({"ok":true,"v":1,"data":{"items":[]}})";
        server.set("/c", first);
        JspClient client;
        client.setHedgeMs(100);
        Waiter<QString> w;
        client.get("s", {server.base()}, "/c", [&](const QJsonObject&, const QString& e, bool) { w.value = e; });
        QVERIFY(w.wait(5000));
        QCOMPARE(*w.value, QString());
        QCOMPARE(server.requests.size(), 2);
    }

    void jspKeepsAnswersForTheirTtlAndShowsTheLastGoodCopyOnFailure()
    {
        FakeHttpServer server;
        server.setJson("/catalog/movie/c", R"({"ok":true,"v":1,"ttl":0,"data":{"items":[1]}})");
        JspClient client;
        auto ask = [&](QJsonObject* out, bool* stale) {
            Waiter<QString> w;
            client.get("s", {server.base()}, "/catalog/movie/c", [&](const QJsonObject& d, const QString& e, bool s) { *out = d; *stale = s; w.value = e; });
            [&] { QVERIFY(w.wait(8000)); }();
            return w.value.value_or("no answer");
        };
        QJsonObject d;
        bool stale = true;
        QCOMPARE(ask(&d, &stale), QString());
        QVERIFY(!stale);
        // The source goes down: the last good answer is used and says so.
        FakeHttpServer::Answer down;
        down.status = 503;
        down.body = R"({"ok":false,"v":1,"error":{"code":"unavailable"}})";
        server.set("/catalog/movie/c", down);
        QJsonObject d2;
        QCOMPARE(ask(&d2, &stale), QString());
        QVERIFY(stale);
        QCOMPARE(d2.value("items").toArray().size(), 1);
        // A source never asked before has no copy: the error is shown.
        Waiter<QString> w;
        client.get("other", {server.base()}, "/catalog/movie/c", [&](const QJsonObject&, const QString& e, bool) { w.value = e; });
        QVERIFY(w.wait(8000));
        QVERIFY(!w.value->isEmpty());
    }

    void jspNeverShowsOldStreamsAfterTheyExpire()
    {
        FakeHttpServer server;
        server.setJson("/streams/movie/tt1", R"({"ok":true,"v":1,"ttl":0,"data":{"streams":[{"kind":"http","url":"https:\/\/cdn.example/a.mp4?sig=1"}]}})");
        JspClient client;
        Waiter<QString> first;
        client.get("s", {server.base()}, "/streams/movie/tt1", [&](const QJsonObject&, const QString& e, bool) { first.value = e; });
        QVERIFY(first.wait(8000));
        QCOMPARE(*first.value, QString());
        FakeHttpServer::Answer down;
        down.status = 503;
        down.body = R"({"ok":false,"v":1,"error":{"code":"unavailable"}})";
        server.set("/streams/movie/tt1", down);
        Waiter<QString> second;
        bool stale = true;
        client.get("s", {server.base()}, "/streams/movie/tt1", [&](const QJsonObject&, const QString& e, bool s) { stale = s; second.value = e; });
        QVERIFY(second.wait(8000));
        QVERIFY(!second.value->isEmpty());   // an error, not the old signed link
        QVERIFY(!stale);
    }

    void jspHungSourcesCompleteOnceWithinOneBudget()
    {
        FakeHttpServer primary, mirror;
        FakeHttpServer::Answer hang;
        hang.delayMs = -1;
        primary.set("/catalog/movie/top", hang);
        mirror.set("/catalog/movie/top", hang);
        JspClient client;
        client.setBudgetMs(600);
        client.setHedgeMs(100);
        int completions = 0;
        QString error;
        QElapsedTimer clock;
        clock.start();
        client.get("s", {primary.base(), mirror.base()}, "/catalog/movie/top",
                   [&](const QJsonObject&, const QString& e, bool) { ++completions; error = e; });
        QTRY_VERIFY_WITH_TIMEOUT(completions > 0, 4000);
        QVERIFY2(clock.elapsed() < 1800, "one budget for the whole request, not one per try");
        QVERIFY(!error.isEmpty());
        QTest::qWait(900);   // later timers and the aborted tries must not answer again
        QCOMPARE(completions, 1);
        QTRY_COMPARE_WITH_TIMEOUT(client.inFlightFor("s"), 0, 2000);
    }

    void jspWinningMirrorCancelsTheLoserAndLeavesOtherRequestsAlone()
    {
        FakeHttpServer slow, fast;
        FakeHttpServer::Answer hang;
        hang.delayMs = -1;
        slow.set("/catalog/movie/a", hang);
        fast.setJson("/catalog/movie/a", R"({"ok":true,"v":1,"data":{"items":[]}})");
        // Another request of the same source, slow but alive.
        slow.setJson("/catalog/movie/b", R"({"ok":true,"v":1,"data":{"items":[{"id":"tt2"}]}})", 700);
        fast.setJson("/catalog/movie/b", R"({"ok":true,"v":1,"data":{"items":[{"id":"tt2"}]}})", 700);
        JspClient client;
        client.setHedgeMs(50);
        Waiter<QString> a, b;
        QJsonObject gotB;
        client.get("s", {slow.base(), fast.base()}, "/catalog/movie/b",
                   [&](const QJsonObject& d, const QString& e, bool) { gotB = d; b.value = e; });
        client.get("s", {slow.base(), fast.base()}, "/catalog/movie/a",
                   [&](const QJsonObject&, const QString& e, bool) { a.value = e; });
        QVERIFY(a.wait(5000));
        QCOMPARE(*a.value, QString());
        // a's hung try to `slow` is gone; b's tries are still running.
        QVERIFY(b.wait(5000));
        QCOMPARE(*b.value, QString());   // not "cancelled"
        QCOMPARE(gotB.value("items").toArray().size(), 1);
        QTRY_COMPARE_WITH_TIMEOUT(client.inFlightFor("s"), 0, 2000);
    }

    void jspRealHttp404IsFinalAndNeverAsksTheMirror()
    {
        FakeHttpServer primary, mirror;
        FakeHttpServer::Answer notFound;
        notFound.status = 404;
        notFound.body = R"({"ok":false,"v":1,"error":{"code":"not_found","message":"no such title"}})";
        primary.set("/meta/movie/tt9", notFound);
        mirror.setJson("/meta/movie/tt9", R"({"ok":true,"v":1,"data":{}})");
        JspClient client;
        Waiter<QString> w;
        client.get("s", {primary.base(), mirror.base()}, "/meta/movie/tt9", [&](const QJsonObject&, const QString& e, bool) { w.value = e; });
        QVERIFY(w.wait(5000));
        QVERIFY(w.value->contains("not_found"));
        QCOMPARE(primary.requests.size(), 1);
        QCOMPARE(mirror.requests.size(), 0);
    }

    void jspRateLimitKeepsThatAddressQuietButAMirrorMayAnswer()
    {
        FakeHttpServer limited, mirror;
        FakeHttpServer::Answer slowDown;
        slowDown.status = 429;
        slowDown.body = R"({"ok":false,"v":1,"error":{"code":"rate_limited","message":"Slow down","retryAfter":30}})";
        limited.set("/catalog/movie/top", slowDown);
        limited.set("/catalog/movie/more", slowDown);
        mirror.setJson("/catalog/movie/top", R"({"ok":true,"v":1,"data":{"items":[]}})");
        mirror.setJson("/catalog/movie/more", R"({"ok":true,"v":1,"data":{"items":[]}})");
        JspClient client;
        Waiter<QString> first;
        client.get("s", {limited.base(), mirror.base()}, "/catalog/movie/top", [&](const QJsonObject&, const QString& e, bool) { first.value = e; });
        QVERIFY(first.wait(5000));
        QCOMPARE(*first.value, QString());   // the mirror answered within the same request
        QCOMPARE(limited.requests.size(), 1);
        // A later request does not ask the quiet address at all.
        Waiter<QString> second;
        client.get("s", {limited.base(), mirror.base()}, "/catalog/movie/more", [&](const QJsonObject&, const QString& e, bool) { second.value = e; });
        QVERIFY(second.wait(5000));
        QCOMPARE(*second.value, QString());
        QCOMPARE(limited.requests.size(), 1);

        // With no mirror, the error says so, and the address is not asked again meanwhile.
        FakeHttpServer alone;
        alone.set("/catalog/movie/top", slowDown);
        Waiter<QString> third, fourth;
        client.get("t", {alone.base()}, "/catalog/movie/top", [&](const QJsonObject&, const QString& e, bool) { third.value = e; });
        QVERIFY(third.wait(5000));
        QVERIFY(third.value->contains("rate_limited"));
        QCOMPARE(alone.requests.size(), 1);
        client.get("t", {alone.base()}, "/catalog/movie/top", [&](const QJsonObject&, const QString& e, bool) { fourth.value = e; });
        QVERIFY(fourth.wait(5000));
        QVERIFY(fourth.value->contains("rate_limited"));
        QCOMPARE(alone.requests.size(), 1);
    }

    void jspServerErrorsAndHtmlMoveOnToTheMirror()
    {
        FakeHttpServer primary, mirror;
        FakeHttpServer::Answer html;
        html.status = 502;
        html.contentType = "text/html";
        html.body = "<html><body>Bad gateway</body></html>";
        primary.set("/catalog/movie/top", html);
        FakeHttpServer::Answer unavailable;
        unavailable.status = 503;
        unavailable.body = R"({"ok":false,"v":1,"error":{"code":"unavailable"}})";
        primary.set("/catalog/movie/two", unavailable);
        mirror.setJson("/catalog/movie/top", R"({"ok":true,"v":1,"data":{"items":[]}})");
        mirror.setJson("/catalog/movie/two", R"({"ok":true,"v":1,"data":{"items":[]}})");
        JspClient client;
        Waiter<QString> a, b;
        client.get("s", {primary.base(), mirror.base()}, "/catalog/movie/top", [&](const QJsonObject&, const QString& e, bool) { a.value = e; });
        QVERIFY(a.wait(5000));
        QCOMPARE(*a.value, QString());
        client.setHedgeMs(50);
        client.forget("s");   // ask the primary first again
        client.get("s", {primary.base(), mirror.base()}, "/catalog/movie/two", [&](const QJsonObject&, const QString& e, bool) { b.value = e; });
        QVERIFY(b.wait(5000));
        QCOMPARE(*b.value, QString());
        // Only an HTML page from the only address: a plain error, no page text.
        FakeHttpServer only;
        only.set("/catalog/movie/top", html);
        Waiter<QString> c;
        client.get("u", {only.base()}, "/catalog/movie/top", [&](const QJsonObject&, const QString& e, bool) { c.value = e; });
        QVERIFY(c.wait(5000));
        QVERIFY(c.value->contains("unavailable"));
        QVERIFY(!c.value->contains("Bad gateway"));
    }

    void jspRedirectsFollowTheSameAddressRules()
    {
        FakeHttpServer server;
        FakeHttpServer::Answer away;
        away.status = 302;
        away.location = "http://example.invalid/catalog/movie/top";   // plain http, not this computer
        server.set("/catalog/movie/top", away);
        FakeHttpServer::Answer within;
        within.status = 302;
        within.location = "/catalog/movie/real";
        server.set("/catalog/movie/moved", within);
        server.setJson("/catalog/movie/real", R"({"ok":true,"v":1,"data":{"items":[]}})");
        JspClient client;
        Waiter<QString> refused, followed;
        client.get("s", {server.base()}, "/catalog/movie/top", [&](const QJsonObject&, const QString& e, bool) { refused.value = e; });
        QVERIFY(refused.wait(5000));
        QVERIFY(refused.value->contains("redirect"));
        client.get("s", {server.base()}, "/catalog/movie/moved", [&](const QJsonObject&, const QString& e, bool) { followed.value = e; });
        QVERIFY(followed.wait(5000));
        QCOMPARE(*followed.value, QString());   // a redirect the rules allow still works
    }

    void jspAnOldAnswerCannotComeBackAfterTheSourceChanged()
    {
        FakeHttpServer server;
        server.setJson("/catalog/movie/x", R"({"ok":true,"v":1,"data":{"items":[{"id":"tt1"}]}})", 400);
        JspClient client;
        Waiter<QString> old;
        client.get("s", {server.base()}, "/catalog/movie/x", [&](const QJsonObject&, const QString& e, bool) { old.value = e; });
        QTRY_COMPARE_WITH_TIMEOUT(server.requests.size(), 1, 3000);   // really on the network
        client.forget("s");   // its addresses changed while the request was out
        QVERIFY(old.wait(5000));
        QCOMPARE(*old.value, QStringLiteral("cancelled"));
        QTest::qWait(600);
        // Nothing was saved from it: with the source down there is no copy to show.
        FakeHttpServer::Answer down;
        down.status = 503;
        down.body = R"({"ok":false,"v":1,"error":{"code":"unavailable"}})";
        server.set("/catalog/movie/x", down);
        Waiter<QString> after;
        bool stale = true;
        client.get("s", {server.base()}, "/catalog/movie/x", [&](const QJsonObject&, const QString& e, bool s) { stale = s; after.value = e; });
        QVERIFY(after.wait(5000));
        QVERIFY(!after.value->isEmpty());
        QVERIFY(!stale);
    }

    void jspAnOkAnswerWithBrokenDataLosesToAHealthyMirrorAndIsNeverCached()
    {
        FakeHttpServer broken, healthy;
        // "ok", but no items at all (or the wrong type): not the same as an empty list.
        broken.setJson("/catalog/movie/top", R"({"ok":true,"v":1,"data":{}})");
        broken.setJson("/catalog/movie/top2", R"({"ok":true,"v":1,"data":{"items":"none"}})");
        broken.setJson("/streams/movie/tt1", R"({"ok":true,"v":1,"data":{"streams":{}}})");
        broken.setJson("/meta/movie/tt1", R"({"ok":true,"v":1,"data":{"name":"No id"}})");
        broken.setJson("/catalog/movie/empty", R"({"ok":true,"v":1,"data":{"items":[]}})");
        healthy.setJson("/catalog/movie/top", R"({"ok":true,"v":1,"data":{"items":[{"id":"tt1","type":"movie","name":"One"}]}})");
        healthy.setJson("/catalog/movie/top2", R"({"ok":true,"v":1,"data":{"items":[{"id":"tt2","type":"movie","name":"Two"}]}})");
        healthy.setJson("/streams/movie/tt1", R"({"ok":true,"v":1,"data":{"streams":[]}})");
        healthy.setJson("/meta/movie/tt1", R"({"ok":true,"v":1,"data":{"id":"tt1","type":"movie","name":"One"}})");
        JspClient client;
        client.setHedgeMs(5000);   // only a failed try may move on, not the timer
        const QStringList bases{broken.base(), healthy.base()};
        const auto ask = [&](const QString& path, QJsonObject* out) {
            Waiter<QString> w;
            // A source of its own each time: the mirror that answered is otherwise asked first next time.
            client.get("s" + path, bases, path, [&](const QJsonObject& d, const QString& e, bool) { *out = d; w.value = e; });
            [&] { QVERIFY(w.wait(5000)); }();
            return w.value.value_or("no answer");
        };
        QJsonObject d;
        QCOMPARE(ask("/catalog/movie/top", &d), QString());
        QCOMPARE(d.value("items").toArray().size(), 1);
        QCOMPARE(ask("/catalog/movie/top2", &d), QString());
        QCOMPARE(d.value("items").toArray().size(), 1);
        QCOMPARE(ask("/streams/movie/tt1", &d), QString());
        QVERIFY(d.value("streams").isArray());
        QCOMPARE(ask("/meta/movie/tt1", &d), QString());
        QCOMPARE(d.value("id").toString(), QStringLiteral("tt1"));
        // An explicitly empty list is a real answer.
        QCOMPARE(ask("/catalog/movie/empty", &d), QString());
        QCOMPARE(d.value("items").toArray().size(), 0);

        // Alone, a broken answer is an error, and it replaces nothing that was saved.
        FakeHttpServer only;
        only.setJson("/catalog/movie/c", R"({"ok":true,"v":1,"ttl":0,"data":{"items":[{"id":"tt1"}]}})");
        JspClient other;
        Waiter<QString> first;
        other.get("o", {only.base()}, "/catalog/movie/c", [&](const QJsonObject&, const QString& e, bool) { first.value = e; });
        QVERIFY(first.wait(5000));
        QCOMPARE(*first.value, QString());
        only.setJson("/catalog/movie/c", R"({"ok":true,"v":1,"ttl":0,"data":{}})");
        QJsonObject kept;
        bool stale = false;
        Waiter<QString> second;
        other.get("o", {only.base()}, "/catalog/movie/c", [&](const QJsonObject& dd, const QString& e, bool st) { kept = dd; stale = st; second.value = e; });
        QVERIFY(second.wait(8000));
        QCOMPARE(*second.value, QString());
        QVERIFY(stale);   // the older good copy, not the empty object
        QCOMPARE(kept.value("items").toArray().size(), 1);
        // A streams answer that is broken has no old copy to stand in.
        FakeHttpServer streams;
        streams.setJson("/streams/movie/tt1", R"({"ok":true,"v":1,"data":{}})");
        Waiter<QString> bad;
        other.get("o", {streams.base()}, "/streams/movie/tt1", [&](const QJsonObject&, const QString& e, bool) { bad.value = e; });
        QVERIFY(bad.wait(8000));
        QVERIFY(bad.value->contains("malformed"));
    }

    void jspAFreshCopyDeliveredAfterTheSourceChangedIsCancelled()
    {
        FakeHttpServer server;
        server.setJson("/catalog/movie/c", R"({"ok":true,"v":1,"ttl":600,"data":{"items":[{"id":"tt1"}]}})");
        JspClient client;
        Waiter<QString> warm;
        client.get("s", {server.base()}, "/catalog/movie/c", [&](const QJsonObject&, const QString& e, bool) { warm.value = e; });
        QVERIFY(warm.wait(5000));
        Waiter<QString> hit;
        QJsonObject got;
        client.get("s", {server.base()}, "/catalog/movie/c", [&](const QJsonObject& d, const QString& e, bool) { got = d; hit.value = e; });
        client.forget("s");   // before the copy is delivered
        QVERIFY(hit.wait(5000));
        QCOMPARE(*hit.value, QStringLiteral("cancelled"));
        QVERIFY(got.isEmpty());
        QCOMPARE(server.requests.size(), 1);
    }

    void jspCachedNoticeFollowsTheViewOnScreen()
    {
        FakeHttpServer server;
        server.setJson("/manifest.json", R"({"protocol":1,"id":"org.views","name":"Views","version":"1","resources":["catalog"],"types":["movie"],
            "catalogs":[{"type":"movie","id":"a","name":"List A"},{"type":"movie","id":"b","name":"List B"}]})");
        const char* good = R"({"ok":true,"v":1,"ttl":0,"data":{"items":[{"id":"tt1","type":"movie","name":"One"}]}})";
        server.setJson("/catalog/movie/a", good);
        server.setJson("/catalog/movie/b", good);
        QTemporaryDir data;
        StremioAdapter addons(data.filePath("v.json"), false);
        Waiter<QString> installed;
        addons.install(server.base() + "/manifest.json", [&](const QString& e) { installed.value = e; });
        QVERIFY(installed.wait());
        QCOMPARE(*installed.value, QString());
        QCOMPARE(addons.catalogs().size(), 2);
        const CatalogRef a = addons.catalogs().value(0), b = addons.catalogs().value(1);
        const auto fetch = [&](const CatalogRef& c, const QString& search = {}) {
            Waiter<QString> w;
            addons.fetchCatalog(c, search, 0, [&](const QList<MetaPreview>&, const QString& e) { w.value = e; });
            [&] { QVERIFY(w.wait(8000)); }();
            return w.value.value_or("no answer");
        };
        const auto cachedViews = [&] { return addons.addons().value(0).cachedViews; };
        QCOMPARE(fetch(a), QString());
        QCOMPARE(fetch(b), QString());
        QVERIFY(cachedViews().isEmpty());
        FakeHttpServer::Answer down;
        down.status = 503;
        down.body = R"({"ok":false,"v":1,"error":{"code":"unavailable"}})";
        server.set("/catalog/movie/a", down);
        server.set("/catalog/movie/b", down);
        QCOMPARE(fetch(a), QString());
        QCOMPARE(fetch(b), QString());
        QCOMPARE(cachedViews().size(), 2);
        // List B answers again: only B's notice goes, A is still an old copy.
        server.setJson("/catalog/movie/b", good);
        QCOMPARE(fetch(b), QString());
        QCOMPARE(cachedViews(), QStringList{"catalog/movie/a"});
        // The same list, another search: the one on screen decides.
        server.setJson("/catalog/movie/a", good);
        QCOMPARE(fetch(a), QString());
        QVERIFY(cachedViews().isEmpty());
    }

    void jspManifestRefreshHoldsRedirectsToTheAddressRule()
    {
        FakeHttpServer server;
        const QByteArray manifest = R"({"protocol":1,"id":"org.moved","name":"Moved","version":"1","resources":["catalog"],"types":["movie"],
            "catalogs":[{"type":"movie","id":"a","name":"A"}]})";
        server.setJson("/manifest.json", manifest);
        QTemporaryDir data;
        StremioAdapter addons(data.filePath("m.json"), false);
        Waiter<QString> installed;
        addons.install(server.base() + "/manifest.json", [&](const QString& e) { installed.value = e; });
        QVERIFY(installed.wait());
        QCOMPARE(*installed.value, QString());
        // The manifest now redirects to plain http on another machine.
        FakeHttpServer::Answer away;
        away.status = 302;
        away.location = "http://example.invalid/manifest.json";
        server.set("/manifest.json", away);
        addons.refreshManifests();
        QTRY_VERIFY_WITH_TIMEOUT(addons.addons().value(0).lastError.contains("redirect"), 8000);
    }

    void jspTypedQualityAndSeedersBeatTheLabel()
    {
        const QJsonObject data = json(R"({"streams":[
            {"kind":"http","url":"https:\/\/cdn.example/a.mp4","name":"Fake 2160p","detail":"4K \ud83d\udc64 900 seeders","quality":720,"seeders":5},
            {"kind":"http","url":"https:\/\/cdn.example/b.mp4","name":"Plain 1080p","detail":"seeders 40"}]})");
        QList<StreamOption> s = stremio::parseStreams(jsp::streamsAsStremio(data), "src", "Source");
        QCOMPARE(s.size(), 2);
        QCOMPARE(s[0].quality, 720);   // what the source said, not what the name says
        QCOMPARE(s[0].seeders, 5);
        QCOMPARE(s[1].quality, 1080);  // no typed value: the label fills it
        QCOMPARE(s[1].seeders, 40);
        ranking::sortBest(s);
        QCOMPARE(s[0].name, QStringLiteral("Plain 1080p"));
    }

    void jspAddressRulesAndPathPrefixes()
    {
        QVERIFY(jsp::allowedUrl(QUrl("https://a.example/he")));
        QVERIFY(jsp::allowedUrl(QUrl("http://127.0.0.1:8080")));
        QVERIFY(!jsp::allowedUrl(QUrl("http://a.example")));
        QVERIFY(!jsp::allowedUrl(QUrl("https://user:pw@a.example")));
        QVERIFY(!jsp::allowedUrl(QUrl("ftp://a.example")));

        QTemporaryDir data;
        {
            // A stored Jackie source on plain http (not this computer) is not restored.
            StremioAdapter addons(data.filePath("a.json"), false);
            const QJsonObject raw = json(R"({"protocol":1,"id":"org.bad","name":"Bad","version":"1","resources":["catalog"],"types":["movie"],
                "catalogs":[{"type":"movie","id":"c","name":"C"}]})");
            addons.restore(QJsonArray{QJsonObject{{"transportUrl", "http://example.invalid/manifest.json"}, {"manifest", raw}}});
            QCOMPARE(addons.addons().size(), 0);
        }
        // A prefix such as /he is part of the base for every resource.
        FakeHttpServer server;
        server.setJson("/he/manifest.json", R"({"protocol":1,"id":"org.he","name":"He","version":"1","resources":["catalog"],"types":["movie"],
            "catalogs":[{"type":"movie","id":"top","name":"Top","search":true,"genres":["drama"]}]})");
        server.setJson("/he/catalog/movie/top", R"({"ok":true,"v":1,"data":{"items":[{"id":"tt1","type":"movie","name":"One"}]}})");
        StremioAdapter addons(data.filePath("b.json"), false);
        Waiter<QString> installed;
        addons.install(server.base() + "/he/manifest.json", [&](const QString& e) { installed.value = e; });
        QVERIFY(installed.wait());
        QCOMPARE(*installed.value, QString());
        CatalogRef catalog = addons.catalogs().value(0);
        catalog.genre = "drama";
        catalog.year = "2026";
        using Items = std::pair<QList<MetaPreview>, QString>;
        Waiter<Items> got;
        addons.fetchCatalog(catalog, "dune", 0, [&](const QList<MetaPreview>& items, const QString& e) { got.value = Items{items, e}; });
        QVERIFY(got.wait());
        QCOMPARE(got.value->second, QString());
        // Under /he/, and the search, genre and year travel in one request.
        const QString last = server.paths().last();
        QVERIFY2(last.startsWith("/he/catalog/movie/top?"), qPrintable(last));
        QVERIFY(last.contains("q=dune"));
        QVERIFY(last.contains("genre=drama"));
        QVERIFY(last.contains("year=2026"));
    }

    // --- WikiMaze's own search API and the lists of an add-on --------------------------

    void wikimazeFiltersBecomeQueryParameters()
    {
        const QUrl u = WikiMazeSearch::searchUrl("http://h/he", " dune ", "movie", "scifi", "2010s", "wikimaze-popular", true);
        QCOMPARE(u.path(), QStringLiteral("/he/api/search"));
        const QUrlQuery q(u);
        QCOMPARE(q.queryItemValue("q"), QStringLiteral("dune"));
        QCOMPARE(q.queryItemValue("type"), QStringLiteral("movie"));
        QCOMPARE(q.queryItemValue("genre"), QStringLiteral("scifi"));
        QCOMPARE(q.queryItemValue("year"), QStringLiteral("2010s"));
        QCOMPARE(q.queryItemValue("list"), QStringLiteral("wikimaze-popular"));
        QCOMPARE(q.queryItemValue("live"), QStringLiteral("1"));
        const QUrlQuery span(WikiMazeSearch::searchUrl("http://h", "", "", "", "1995 - 2003", "", false));
        QCOMPARE(span.queryItemValue("from"), QStringLiteral("1995"));
        QCOMPARE(span.queryItemValue("to"), QStringLiteral("2003"));
        QVERIFY(!span.hasQueryItem("year"));
        const QUrlQuery plain(WikiMazeSearch::searchUrl("http://h", "x", "", "", "", "", false));
        QVERIFY(!plain.hasQueryItem("live"));
        QVERIFY(!plain.hasQueryItem("genre"));
        QVERIFY(!plain.hasQueryItem("type"));
    }

    void wikimazeSearchReadsResultsAndFacets()
    {
        FakeHttpServer server;
        server.setJson("/api/search", R"({"source":"store","total":2,"results":[
            {"id":"tt1","type":"movie","name":"One","poster":"p","releaseInfo":"2020"},
            {"id":"","type":"movie","name":"No id"},
            {"id":"tt2","type":"movie","name":"Two"}],
            "facets":{"genres":{"drama":2},"decades":{"2020s":2}}})");
        WikiMazeSearch search;
        search.setBase(server.base());
        QVERIFY(search.available());
        search.search("one", "movie", "", "", "", false);
        QVERIFY(search.loading());
        QTRY_VERIFY_WITH_TIMEOUT(!search.loading(), 5000);
        QCOMPARE(search.items().size(), 2);
        QCOMPARE(search.total(), 2);
        QCOMPARE(search.source(), QStringLiteral("store"));
        QCOMPARE(search.items()[0].provider, QStringLiteral("community.wikimaze"));
        QCOMPARE(search.genreCounts().value("drama").toInt(), 2);
        QCOMPARE(search.decadeCounts().value("2020s").toInt(), 2);
        QVERIFY(server.paths().first().contains("q=one"));
        search.clear();
        QVERIFY(search.items().isEmpty());
    }

    void wikimazeOnlyTheLastTypingIsAsked()
    {
        FakeHttpServer server;
        server.setJson("/api/search", R"({"source":"store","total":0,"results":[]})");
        WikiMazeSearch search;
        search.setBase(server.base());
        search.search("d", "", "", "", "", false);
        search.search("du", "", "", "", "", false);
        search.search("dune", "", "", "", "", false);
        QTRY_VERIFY_WITH_TIMEOUT(!search.loading(), 5000);
        QTest::qWait(500);
        QCOMPARE(server.requests.size(), 1);
        QVERIFY(server.requests[0].path.contains("q=dune"));
        // Nothing typed and no filter: nothing is asked.
        search.search("  ", "", "", "", "", false);
        QTest::qWait(500);
        QCOMPARE(server.requests.size(), 1);
    }

    void wikimazeOlderServerWithoutTheApiIsRemembered()
    {
        FakeHttpServer server;   // answers 404 for everything
        WikiMazeSearch search;
        search.setBase(server.base());
        search.search("dune", "", "", "", "", false);
        QTRY_VERIFY_WITH_TIMEOUT(search.unsupported(), 5000);
        QVERIFY(!search.loading());
        const int asked = int(server.requests.size());
        search.search("again", "", "", "", "", false);
        QTest::qWait(500);
        QCOMPARE(int(server.requests.size()), asked);   // it does not ask again
        search.setBase(server.base() + "/x");           // a new address starts over
        QVERIFY(!search.unsupported());
    }

    void wikimazeSlowAnswerEndsWithoutASecondTry()
    {
        FakeHttpServer server;
        FakeHttpServer::Answer never;
        never.delayMs = -1;
        server.set("/api/search", never);
        WikiMazeSearch search;
        search.setBase(server.base());
        search.search("dune", "", "", "", "", false);
        QTRY_VERIFY_WITH_TIMEOUT(!search.loading(), 12000);
        QVERIFY(!search.error().isEmpty());
        QVERIFY(!search.unsupported());
        QCOMPARE(server.requests.size(), 1);
    }

    void wikimazeFilterNamesLoadOnce()
    {
        FakeHttpServer server;
        server.setJson("/api/genres", R"({"genres":[{"key":"drama","label":"Drama"}]})");
        server.setJson("/api/lists", R"({"lists":[{"id":"wikimaze-popular","type":"movie","name":"Classics"}]})");
        WikiMazeSearch search;
        search.setBase(server.base());
        search.loadFilters();
        search.loadFilters();
        QTRY_VERIFY_WITH_TIMEOUT(!search.genres().isEmpty() && !search.lists().isEmpty(), 5000);
        QCOMPARE(server.requests.size(), 2);
    }

    void wikimazeFilterNamesFollowALateAddress()
    {
        FakeHttpServer server;
        server.setJson("/api/genres", R"({"genres":[{"key":"drama","label":"Drama"}]})");
        server.setJson("/api/lists", R"({"lists":[]})");
        WikiMazeSearch search;
        search.loadFilters();   // the page opens before the add-on's address is known
        QVERIFY(server.requests.isEmpty());
        search.setBase(server.base());
        QTRY_VERIFY_WITH_TIMEOUT(!search.genres().isEmpty(), 5000);
    }

    void listsOfAnAddonCanBeHidden()
    {
        FakeHttpServer server;
        server.setJson("/w/manifest.json",
                       manifest("community.wikimaze.x", "WikiMaze", R"(["catalog"])",
                                R"([{"type":"movie","id":"a","name":"A"},{"type":"series","id":"a","name":"A"},{"type":"movie","id":"b","name":"B"}])"));
        QTemporaryDir data;
        const QString store = data.filePath("addons.json");
        {
            StremioAdapter addons(store, false);
            Waiter<QString> w;
            addons.install(server.base() + "/w/manifest.json", [&](const QString& e) { w.value = e; });
            QVERIFY(w.wait());
            QCOMPARE(addons.catalogs().size(), 3);
            QCOMPARE(addons.wikimazeBase(), server.base() + "/w");
            QVERIFY(addons.setCatalogEnabled("community.wikimaze.x", "movie", "a", false));
            QCOMPARE(addons.catalogs().size(), 2);   // only the movie list "a" is gone
            QVERIFY(addons.setCatalogEnabled("community.wikimaze.x", "movie", "a", true));
            QCOMPARE(addons.catalogs().size(), 3);
            QVERIFY(addons.setCatalogEnabled("community.wikimaze.x", "series", "a", false));
            QVERIFY(addons.setEnabled("community.wikimaze.x", false));
            QVERIFY(addons.wikimazeBase().isEmpty());
            QVERIFY(addons.setEnabled("community.wikimaze.x", true));
        }
        StremioAdapter again(store, false);   // loads what was saved
        QCOMPARE(again.catalogs().size(), 2);  // the hidden list stays hidden
        QVERIFY(again.addons()[0].hiddenCatalogs.contains("series/a"));
    }

    void boundsTotalDuration()
    {
        FakeHttpServer server;
        FakeHttpServer::Answer slow;
        slow.body = "{\"streams\":[]}";
        slow.trickleMs = 200;
        server.set("/slow.json", slow);
        JsonFetcher fetcher;
        fetcher.setTimeout(1000);
        fetcher.setDeadline(1500);
        Waiter<QString> w;
        QElapsedTimer clock;
        clock.start();
        fetcher.get(QUrl(server.base() + "/slow.json"), [&](const QJsonObject&, const QString& e) { w.value = e; });
        QVERIFY(w.wait());
        QCOMPARE(*w.value, QStringLiteral("timed out"));
        QVERIFY(clock.elapsed() < 2900);
    }

    void redirectLoopsEnd()
    {
        FakeHttpServer server;
        FakeHttpServer::Answer loop;
        loop.status = 302;
        loop.location = "/loop.json";
        server.set("/loop.json", loop);
        JsonFetcher fetcher;
        Waiter<QString> w;
        fetcher.get(QUrl(server.base() + "/loop.json"), [&](const QJsonObject&, const QString& e) { w.value = e; });
        QVERIFY(w.wait());
        QVERIFY(!w.value->isEmpty());
        QVERIFY(server.requests.size() <= JsonFetcher::kMaxRedirects + 1);
    }

    void malformedAnswersAreErrors_data()
    {
        QTest::addColumn<QByteArray>("body");
        QTest::newRow("empty") << QByteArray();
        QTest::newRow("array") << QByteArray("[1,2]");
        QTest::newRow("truncated") << QByteArray("{\"streams\":[{\"url\":");
        QTest::newRow("binary") << QByteArray("\x00\xff\xfe{", 4);
    }

    void malformedAnswersAreErrors()
    {
        QFETCH(QByteArray, body);
        FakeHttpServer server;
        server.setJson("/bad.json", body);
        JsonFetcher fetcher;
        Waiter<QString> w;
        fetcher.get(QUrl(server.base() + "/bad.json"), [&](const QJsonObject&, const QString& e) { w.value = e; });
        QVERIFY(w.wait());
        QCOMPARE(*w.value, QStringLiteral("the answer was not JSON"));
    }

    // Past the cap, requests fail at once instead of piling up.
    void boundsRequestsInFlight()
    {
        FakeHttpServer server;
        server.setJson("/hang.json", "{}", -1);
        JsonFetcher fetcher;
        int refused = 0;
        for (int i = 0; i < JsonFetcher::kMaxInFlight + 3; ++i)
            fetcher.get(QUrl(server.base() + "/hang.json"), [&](const QJsonObject&, const QString& e) {
                refused += e == QStringLiteral("too many requests");
            });
        QTRY_COMPARE(refused, 3);
        QCOMPARE(fetcher.inFlight(), JsonFetcher::kMaxInFlight);
        fetcher.abortAll();
        QTRY_COMPARE(fetcher.inFlight(), 0);
    }

private:
    // gzip framing around zlib's deflate stream, as a web server sends it.
    static QByteArray gzip(const QByteArray& data)
    {
        QByteArray z = qCompress(data, 9);
        z = z.mid(4);  // qCompress puts the length first
        // zlib stream: 2-byte header, deflate data, 4-byte Adler-32.
        const QByteArray deflate = z.mid(2, z.size() - 6);
        QByteArray out("\x1f\x8b\x08\x00\x00\x00\x00\x00\x00\x03", 10);
        out += deflate;
        const quint32 crc = crc32(data);
        const quint32 size = quint32(data.size());
        for (int i = 0; i < 4; ++i)
            out += char((crc >> (8 * i)) & 0xff);
        for (int i = 0; i < 4; ++i)
            out += char((size >> (8 * i)) & 0xff);
        return out;
    }
    static quint32 crc32(const QByteArray& data)
    {
        quint32 crc = 0xffffffffu;
        for (char ch : data) {
            crc ^= quint8(ch);
            for (int k = 0; k < 8; ++k)
                crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
        }
        return ~crc;
    }
};

QTEST_MAIN(TestSources)
#include "tst_sources.moc"
