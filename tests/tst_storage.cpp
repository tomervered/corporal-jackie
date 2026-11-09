// The database: it opens, migrates, keeps what was saved across runs, and
// the history and library rules hold. Then the Library: what plays is
// recorded, and starts again where it was left.

#include "Browser.h"
#include "FakeTarget.h"
#include "Library.h"
#include "Session.h"
#include "Storage.h"

#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <sqlite3.h>

using namespace cj;

namespace {

HistoryEntry episode(int n, double position, qint64 at, double duration = 1500)
{
    HistoryEntry e;
    e.adapter = QStringLiteral("stremio");
    e.provider = QStringLiteral("org.example");
    e.itemId = QStringLiteral("tt100");
    e.itemType = QStringLiteral("series");
    e.itemName = QStringLiteral("A Show");
    e.videoId = QStringLiteral("tt100:1:%1").arg(n);
    e.key = e.adapter + ':' + e.itemId + ':' + e.videoId;
    e.season = 1;
    e.episode = n;
    e.position = position;
    e.duration = duration;
    e.updatedAt = at;
    e.source = PlayableSource::fromUrl(QUrl(QStringLiteral("https://example.com/%1.mkv").arg(n)), QStringLiteral("Ep %1").arg(n));
    return e;
}

HistoryEntry direct(const QString& url, double position, qint64 at)
{
    HistoryEntry e;
    e.source = PlayableSource::fromUrl(QUrl(url), QStringLiteral("clip"));
    e.key = QStringLiteral("url:") + Storage::sourceKey(e.source);
    e.itemName = QStringLiteral("clip");
    e.position = position;
    e.duration = 600;
    e.updatedAt = at;
    return e;
}

// Makes every insert into `table` fail, as a full or broken disk would.
void injectFailure(const QString& path, const char* table)
{
    sqlite3* db = nullptr;
    QCOMPARE(sqlite3_open(path.toUtf8().constData(), &db), SQLITE_OK);
    const QByteArray sql = QByteArray("CREATE TRIGGER fail_") + table + " BEFORE INSERT ON " + table
                         + " BEGIN SELECT RAISE(FAIL, 'injected failure'); END;";
    QCOMPARE(sqlite3_exec(db, sql.constData(), nullptr, nullptr, nullptr), SQLITE_OK);
    sqlite3_close(db);
}

// The last item the Session told the target to load, and where.
QString lastLoad(const FakeTarget& target)
{
    for (auto it = target.commands.crbegin(); it != target.commands.crend(); ++it)
        if (it->startsWith(QLatin1String("load ")))
            return *it;
    return {};
}

} // namespace

class TestStorage : public QObject
{
    Q_OBJECT

private slots:
    void exportOmitsReusableCredentials_data()
    {
        QTest::addColumn<QString>("location");
        for (const char* location : {"header", "url", "addon"})
            QTest::newRow(location) << QString::fromLatin1(location);
    }

    void exportOmitsReusableCredentials()
    {
        QFETCH(QString, location);
        Storage storage({});
        auto entry = episode(1, 600, 1000);
        const QString marker = "auditDummyCredential987";
        if (location == "header")
            entry.source.headers.insert("Authorization", "Bearer " + marker);
        if (location == "url")
            entry.source.url = QUrl("https://example.com/video?token=" + marker);
        if (location == "addon")
            storage.saveAddons(QJsonArray{QJsonObject{{"transportUrl", "https://example.com/" + marker + "/manifest.json"}}});
        storage.record(entry);
        QVERIFY2(!storage.exportJson().contains(marker.toUtf8()), "Default export contains a reusable credential");
    }

    void failedWriteIsReported()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("storage.sqlite");
        Storage storage(path);
        sqlite3* db = nullptr;
        QCOMPARE(sqlite3_open(path.toUtf8().constData(), &db), SQLITE_OK);
        const int result = sqlite3_exec(db, "CREATE TRIGGER reject_history BEFORE INSERT ON history BEGIN SELECT RAISE(FAIL, 'audit injected disk failure'); END;", nullptr, nullptr, nullptr);
        sqlite3_close(db);
        QCOMPARE(result, SQLITE_OK);
        storage.record(episode(1, 600, 1000));
        storage.flush();
        Storage reopened(path);
        QVERIFY(reopened.history().isEmpty());
        QVERIFY2(!storage.error().isEmpty(), "Failed database write has no observable error");
    }

    void importReportsFailedPersistence()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("import.sqlite");
        Storage destination(path);
        sqlite3* db = nullptr;
        QCOMPARE(sqlite3_open(path.toUtf8().constData(), &db), SQLITE_OK);
        const int result = sqlite3_exec(db, "CREATE TRIGGER reject_history BEFORE INSERT ON history BEGIN SELECT RAISE(FAIL, 'audit injected persistence failure'); END;", nullptr, nullptr, nullptr);
        sqlite3_close(db);
        QCOMPARE(result, SQLITE_OK);
        Storage source({});
        source.record(episode(1, 600, 1000));
        const QString error = destination.importJson(source.exportJson());
        destination.flush();
        Storage reopened(path);
        QVERIFY(reopened.history().isEmpty());
        QVERIFY2(!error.isEmpty(), "Import reported success although persistence failed");
    }

    // A full export is for the user's own backup, and keeps everything.
    void fullExportKeepsEverything()
    {
        Storage storage({});
        auto entry = episode(1, 600, 1000);
        entry.source.headers.insert("Authorization", "Bearer keepme");
        storage.record(entry);
        storage.saveAddons(QJsonArray{QJsonObject{{"transportUrl", "https://example.com/cfg/manifest.json"}}});
        const QByteArray full = storage.exportJson(Storage::ExportMode::Full);
        QVERIFY(full.contains("keepme"));
        QVERIFY(full.contains("/cfg/manifest.json"));
        QVERIFY(!storage.exportJson().contains("/cfg/manifest.json"));
        QVERIFY(storage.exportJson().contains("addonsWithSettings"));
    }

    // After a shareable export and import, an add-on item resumes through
    // its ids: Continue Watching opens the item, and picking a stream starts
    // at the saved spot.
    void shareableExportStillResumes()
    {
        Storage from({});
        auto entry = episode(1, 600, 1000);
        entry.source.url = QUrl("https://debrid.example/dl/s3cret/1.mkv?sig=s3cret");
        entry.source.headers.insert("Cookie", "s=s3cret");
        from.record(entry);
        const QByteArray json = from.exportJson();
        QVERIFY(!json.contains("s3cret"));

        Storage to({});
        QCOMPARE(to.importJson(json), QString());
        FakeTarget target;
        Session session(&target);
        Browser browser(&session);
        Library library(to, session, browser);
        QSignalSpy opened(&library, &Library::openRequested);
        library.resume(entry.key);
        QCOMPARE(opened.count(), 1);
        QVERIFY(lastLoad(target).isEmpty());
        QCOMPARE(to.find(entry.key)->position, 600.0);
    }

    // A private tracker's announce link carries a passkey: the export keeps
    // the magnet's info hash and file only, and the key still matches.
    void exportDropsMagnetTrackers()
    {
        Storage from({});
        PlayableSource magnet = PlayableSource::fromUrl(QUrl("torrent://abc/0"), "Film");
        magnet.origin = "magnet:?xt=urn:btih:ABC&dn=Film&tr=https%3A%2F%2Ftracker.example%2Fpasskey123%2Fannounce&so=0";
        HistoryEntry e;
        e.source = magnet;
        e.key = "url:" + Storage::sourceKey(magnet);
        e.position = 300;
        e.duration = 600;
        e.updatedAt = 1000;
        from.record(e);
        QVERIFY(!e.key.contains("passkey123"));
        const QByteArray json = from.exportJson();
        QVERIFY(!json.contains("passkey123"));
        Storage to({});
        QCOMPARE(to.importJson(json), QString());
        QCOMPARE(to.resumePosition(magnet), 300.0);
    }

    // Keys written before magnets were keyed by info hash move over once.
    void oldMagnetKeysMoveToInfoHashes()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("old.sqlite");
        const QString old = "magnet:?xt=urn:btih:abc&tr=udp%3A%2F%2Ft.example%3A1";
        {
            Storage storage(path);
            storage.flush();
        }
        sqlite3* db = nullptr;
        QCOMPARE(sqlite3_open(path.toUtf8().constData(), &db), SQLITE_OK);
        const QByteArray sql = "INSERT INTO history(key, source, position, duration, updated_at, finished) VALUES('url:"
                             + old.toUtf8() + "', '{\"url\":\"torrent://abc/0\",\"kind\":\"torrent\",\"origin\":\""
                             + old.toUtf8() + "\"}', 120, 600, 5, 0)";
        QCOMPARE(sqlite3_exec(db, sql.constData(), nullptr, nullptr, nullptr), SQLITE_OK);
        sqlite3_close(db);
        {
            Storage storage(path);
            QCOMPARE(storage.history().size(), 1);
            QCOMPARE(storage.history().first().key, QStringLiteral("url:magnet:?xt=urn:btih:abc"));
        }
        Storage reopened(path);
        QCOMPARE(reopened.history().first().key, QStringLiteral("url:magnet:?xt=urn:btih:abc"));
    }

    // The UI hears about a failed write, and keeps working.
    void failedWriteReachesTheUi()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("storage.sqlite");
        Storage storage(path);
        injectFailure(path, "library");
        FakeTarget target;
        Session session(&target);
        Browser browser(&session);
        Library library(storage, session, browser);
        QSignalSpy changed(&library, &Library::storageErrorChanged);
        library.add(QVariantMap{{"adapter", "stremio"}, {"id", "tt1"}, {"name", "X"}});
        storage.flush();
        QTRY_COMPARE(changed.count(), 1);
        QVERIFY(library.storageError().contains("injected"));
        storage.record(episode(1, 600, 1000));  // other tables still save
        storage.flush();
        Storage reopened(path);
        QCOMPARE(reopened.history().size(), 1);
        QVERIFY(reopened.library().isEmpty());
    }

    // An import is all or nothing: a failure on the last table leaves the
    // others untouched, on disk and in memory.
    void importIsAllOrNothing()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("import.sqlite");
        Storage destination(path);
        injectFailure(path, "library");
        Storage source({});
        source.record(episode(1, 600, 1000));
        LibraryItem item;
        item.adapter = "stremio";
        item.itemId = "tt9";
        source.addToLibrary(item);
        source.saveFolders({"/videos"});
        QSignalSpy history(&destination, &Storage::historyChanged);
        const QString error = destination.importJson(source.exportJson());
        QVERIFY(error.contains("Nothing was imported"));
        QVERIFY(destination.history().isEmpty());
        QVERIFY(!destination.folders());
        QCOMPARE(history.count(), 0);
        Storage reopened(path);
        QVERIFY(reopened.history().isEmpty());
        QVERIFY(!reopened.folders());
    }

    // A damaged file is refused before anything is written.
    void importChecksTheWholeFileFirst_data()
    {
        QTest::addColumn<QByteArray>("tail");
        QTest::newRow("history not a list") << QByteArray("\"history\":{}");
        QTest::newRow("entry not an object") << QByteArray("\"history\":[1]");
        QTest::newRow("negative position") << QByteArray("\"history\":[{\"key\":\"k\",\"position\":-5}]");
        QTest::newRow("bad link") << QByteArray("\"history\":[{\"key\":\"k\",\"source\":{\"url\":\"javascript:alert(1)\"}}]");
        QTest::newRow("bad subtitle") << QByteArray("\"history\":[{\"key\":\"k\",\"source\":{\"url\":\"https://a.example/v\",\"subtitles\":[{\"url\":\"ftp://x/y.srt\"}]}}]");
        QTest::newRow("addon not web") << QByteArray("\"addons\":[{\"transportUrl\":\"file:///etc/manifest.json\"}]");
        QTest::newRow("folder not text") << QByteArray("\"folders\":[3]");
        // Found by tests/fuzz: a time past qint64 was converted unchecked.
        QTest::newRow("huge time") << QByteArray("\"history\":[{\"key\":\"k\",\"updatedAt\":1.7e54}]");
        QTest::newRow("negative time") << QByteArray("\"history\":[{\"key\":\"k\",\"updatedAt\":-1}]");
        QTest::newRow("huge added time") << QByteArray("\"library\":[{\"adapter\":\"a\",\"itemId\":\"t\",\"addedAt\":1e300}]");
    }

    void importChecksTheWholeFileFirst()
    {
        QFETCH(QByteArray, tail);
        Storage storage({});
        const QByteArray json = "{\"format\":\"corporal-jackie-export\",\"version\":1,\"library\":[{\"adapter\":\"stremio\",\"itemId\":\"tt1\"}],"
                              + tail + "}";
        QVERIFY(!storage.importJson(json).isEmpty());
        QVERIFY(storage.library().isEmpty());
        QVERIFY(storage.history().isEmpty());
    }

    // History off: nothing new is recorded, the choice survives a restart,
    // and turning it on again records as before.
    void historyOffRecordsNothing()
    {
        QTemporaryDir dir;
        const QString file = dir.filePath(QStringLiteral("cj.sqlite"));
        {
            Storage s(file);
            QVERIFY(s.keepsHistory());
            s.record(episode(1, 600, 1000));
            s.setKeepsHistory(false);
            s.record(episode(2, 700, 2000));
            QCOMPARE(s.history().size(), 1);
        }
        {
            Storage s(file);
            QVERIFY(!s.keepsHistory());
            QCOMPARE(s.history().size(), 1);  // what was there stays
            s.record(episode(3, 800, 3000));
            QCOMPARE(s.history().size(), 1);
            s.setKeepsHistory(true);
            s.record(episode(3, 800, 3000));
            QCOMPARE(s.history().size(), 2);
        }
        Storage s(file);
        QCOMPARE(s.history().size(), 2);
    }

    // Forgetting and clearing leave no readable copy of the entries in the
    // database file or its write-ahead log, while the database is open.
    // Without the checkpoint the old pages stay in the WAL (checked). Ubuntu
    // builds SQLite to overwrite deleted rows anyway; MSYS2's and Homebrew's
    // builds do not, so there this also checks PRAGMA secure_delete.
    void clearedHistoryLeavesNoTrace()
    {
        QTemporaryDir dir;
        const QString file = dir.filePath(QStringLiteral("cj.sqlite"));
        const QByteArray secret = "Zyzzyva Midnight Special";
        const auto traces = [&] {
            QStringList found;
            for (const QString& suffix : {QString(), QStringLiteral("-wal"), QStringLiteral("-shm")}) {
                QFile f(file + suffix);
                if (f.open(QIODevice::ReadOnly) && f.readAll().contains(secret))
                    found << QFileInfo(f).fileName();
            }
            return found;
        };
        Storage s(file);
        // The show to forget, between entries that stay, so its rows share
        // pages with rows that are kept.
        s.record(direct(QStringLiteral("https://a.example/before.mkv"), 90, 500));
        for (int n = 1; n <= 20; ++n) {
            HistoryEntry e = episode(n, 600, 1000 + n);
            e.itemName = QString::fromUtf8(secret);
            s.record(e);
        }
        s.record(direct(QStringLiteral("https://a.example/after.mkv"), 90, 5000));
        s.flush();
        QVERIFY(!traces().isEmpty());  // the check can see it

        s.forget(episode(1, 0, 0).itemKey());
        s.flush();
        QCOMPARE(traces(), QStringList());
        QCOMPARE(s.history().size(), 2);

        for (int n = 1; n <= 5; ++n) {
            HistoryEntry e = episode(n, 600, 9000 + n);
            e.itemName = QString::fromUtf8(secret);
            s.record(e);
        }
        s.flush();
        QVERIFY(!traces().isEmpty());
        s.clearHistory();
        s.flush();
        QCOMPARE(traces(), QStringList());
    }

    void opensInMemory()
    {
        Storage s({});
        QVERIFY(s.isOpen());
        QVERIFY(!s.addons());
        QVERIFY(!s.folders());
        QVERIFY(s.history().isEmpty());
    }

    void keepsEverythingAcrossRuns()
    {
        QTemporaryDir dir;
        const QString file = dir.filePath(QStringLiteral("sub/cj.sqlite"));
        PlayableSource torrent = PlayableSource::fromUrl(QUrl(QStringLiteral("http://127.0.0.1:5555/stream/abc")), QStringLiteral("Film"));
        torrent.origin = QStringLiteral("magnet:?xt=urn:btih:abc");
        torrent.headers.insert(QStringLiteral("Referer"), QStringLiteral("x"));
        torrent.subtitles.append({QUrl(QStringLiteral("https://example.com/en.srt")), QStringLiteral("en"), QStringLiteral("English")});
        {
            Storage s(file);
            QVERIFY(s.isOpen());
            s.setSetting(QStringLiteral("player/volume"), 70);
            s.setSetting(QStringLiteral("ui/name"), QStringLiteral("héllo"));
            s.saveAddons(QJsonArray{QJsonObject{{"transportUrl", "https://a.example/manifest.json"},
                                                {"manifest", QJsonObject{{"id", "a"}, {"name", "A"}}}}});
            s.saveFolders({QStringLiteral("/videos"), QStringLiteral("/more")});
            s.record(episode(1, 600, 1000));
            HistoryEntry film = direct(QStringLiteral("x"), 90, 2000);
            film.source = torrent;
            film.key = QStringLiteral("url:") + Storage::sourceKey(torrent);
            s.record(film);
            LibraryItem item;
            item.adapter = QStringLiteral("stremio");
            item.itemId = QStringLiteral("tt100");
            item.name = QStringLiteral("A Show");
            s.addToLibrary(item);
        }
        Storage s(file);
        QVERIFY(s.isOpen());
        QCOMPARE(s.setting(QStringLiteral("player/volume")).toInt(), 70);
        QCOMPARE(s.setting(QStringLiteral("ui/name")).toString(), QStringLiteral("héllo"));
        QCOMPARE(s.setting(QStringLiteral("missing"), 5).toInt(), 5);
        QVERIFY(s.addons());
        QCOMPARE(s.addons()->size(), 1);
        QCOMPARE(s.addons()->at(0).toObject().value("manifest").toObject().value("name").toString(), QStringLiteral("A"));
        QCOMPARE(*s.folders(), QStringList({QStringLiteral("/videos"), QStringLiteral("/more")}));
        QCOMPARE(s.history().size(), 2);
        QCOMPARE(s.history().at(0).source, torrent);  // newest first, every field kept
        QCOMPARE(s.history().at(1).position, 600.0);
        QVERIFY(s.inLibrary(QStringLiteral("stremio:tt100")));

        // A magnet link resumes even though its stream URL is new this run.
        PlayableSource again = torrent;
        again.url = QUrl(QStringLiteral("http://127.0.0.1:6666/stream/abc"));
        QCOMPARE(s.resumePosition(again), 90.0);
    }

    void emptyListsAreRememberedAsSaved()
    {
        QTemporaryDir dir;
        const QString file = dir.filePath(QStringLiteral("cj.sqlite"));
        {
            Storage s(file);
            s.saveAddons({});
        }
        Storage s(file);
        QVERIFY(s.addons());  // saved empty: not a fresh install
        QVERIFY(s.addons()->isEmpty());
        QVERIFY(!s.folders());
    }

    void refusesANewerSchema()
    {
        QTemporaryDir dir;
        const QString file = dir.filePath(QStringLiteral("cj.sqlite"));
        sqlite3* db = nullptr;
        QCOMPARE(sqlite3_open(file.toUtf8().constData(), &db), SQLITE_OK);
        sqlite3_exec(db, "PRAGMA user_version=99", nullptr, nullptr, nullptr);
        sqlite3_close(db);
        Storage s(file);
        QVERIFY(!s.isOpen());
        QVERIFY(s.error().contains(QStringLiteral("newer")));
        s.record(episode(1, 100, 1));  // ignored on disk, harmless
    }

    void recordUpdatesByKey()
    {
        Storage s({});
        QSignalSpy changed(&s, &Storage::historyChanged);
        s.record(episode(1, 100, 1000));
        s.record(episode(2, 200, 2000));
        s.record(episode(1, 300, 3000));
        QCOMPARE(changed.count(), 3);
        QCOMPARE(s.history().size(), 2);
        QCOMPARE(s.history().at(0).episode, 1);
        QCOMPARE(s.history().at(0).position, 300.0);
        QVERIFY(s.find(episode(2, 0, 0).key));
    }

    void continueWatchingShowsOneRowPerItem()
    {
        Storage s({});
        s.record(episode(1, 1400, 1000));
        s.record(episode(2, 500, 2000));
        s.record(direct(QStringLiteral("https://example.com/a.mp4"), 120, 3000));
        s.record(direct(QStringLiteral("https://example.com/b.mp4"), 10, 4000));  // too early to resume
        HistoryEntry done = direct(QStringLiteral("https://example.com/c.mp4"), 590, 5000);
        done.finished = true;
        s.record(done);

        const QList<HistoryEntry> rows = s.continueWatching();
        QCOMPARE(rows.size(), 2);
        QCOMPARE(rows.at(0).source.url, QUrl(QStringLiteral("https://example.com/a.mp4")));
        QCOMPARE(rows.at(1).episode, 2);  // the newest episode of the show, once
        QCOMPARE(s.continueWatching(1).size(), 1);

        QCOMPARE(s.resumePosition(rows.at(0).source), 120.0);
        QCOMPARE(s.resumePosition(done.source), 0.0);
        QCOMPARE(s.resumePosition(PlayableSource::fromUrl(QUrl(QStringLiteral("https://example.com/b.mp4")), {})), 0.0);

        s.forget(QStringLiteral("stremio:tt100"));
        QCOMPARE(s.history().size(), 3);
        QCOMPARE(s.continueWatching().size(), 1);
    }

    void historyIsCapped()
    {
        QTemporaryDir dir;
        const QString file = dir.filePath(QStringLiteral("cj.sqlite"));
        const int total = Storage::kMaxHistory + 25;
        {
            Storage s(file);
            for (int i = 0; i < total; ++i)
                s.record(direct(QStringLiteral("https://example.com/%1.mp4").arg(i), 100, 1000 + i));
            QCOMPARE(s.history().size(), Storage::kMaxHistory);
            QCOMPARE(s.history().last().updatedAt, qint64(1000 + total - Storage::kMaxHistory));
        }
        Storage s(file);
        QCOMPARE(s.history().size(), Storage::kMaxHistory);
        QCOMPARE(s.history().first().updatedAt, qint64(1000 + total - 1));
    }

    void libraryAddsAndRemoves()
    {
        Storage s({});
        QSignalSpy changed(&s, &Storage::libraryChanged);
        LibraryItem a;
        a.adapter = QStringLiteral("stremio");
        a.itemId = QStringLiteral("tt1");
        a.addedAt = 10;
        LibraryItem b = a;
        b.itemId = QStringLiteral("tt2");
        b.addedAt = 20;
        s.addToLibrary(a);
        s.addToLibrary(b);
        s.addToLibrary(a);  // again: one entry
        QCOMPARE(s.library().size(), 2);
        QCOMPARE(s.library().at(0).itemId, QStringLiteral("tt2"));
        s.removeFromLibrary(QStringLiteral("stremio:tt2"));
        s.removeFromLibrary(QStringLiteral("stremio:nothing"));
        QCOMPARE(s.library().size(), 1);
        QCOMPARE(changed.count(), 4);
    }

    void libraryRecordsAndResumesDirectOpens()
    {
        Storage storage({});
        FakeTarget target;
        Session session(&target);
        Browser browser(&session);
        Library library(storage, session, browser);

        session.open(QUrl(QStringLiteral("https://example.com/a.mp4")));
        target.startPlaying(1000);
        target.movePosition(300);
        target.setState(PlaybackState::Paused);  // a pause saves the spot
        const auto a = storage.find(QStringLiteral("url:https://example.com/a.mp4"));
        QVERIFY(a);
        QCOMPARE(a->position, 300.0);
        QCOMPARE(a->duration, 1000.0);
        QVERIFY(!a->finished);
        QCOMPARE(library.continueWatching().size(), 1);
        QCOMPARE(library.continueWatching()[0].toMap().value("progress").toDouble(), 0.3);

        session.open(QUrl(QStringLiteral("https://example.com/b.mp4")));
        QCOMPARE(lastLoad(target), QStringLiteral("load https://example.com/b.mp4 0"));
        session.open(QUrl(QStringLiteral("https://example.com/a.mp4")));
        QCOMPARE(lastLoad(target), QStringLiteral("load https://example.com/a.mp4 300"));

        // Watched to the end: next time it starts over.
        target.startPlaying(1000);
        target.movePosition(990);
        target.setState(PlaybackState::Ended);
        QVERIFY(storage.find(QStringLiteral("url:https://example.com/a.mp4"))->finished);
        QVERIFY(library.continueWatching().isEmpty());
        session.open(QUrl(QStringLiteral("https://example.com/b.mp4")));
        session.open(QUrl(QStringLiteral("https://example.com/a.mp4")));
        QCOMPARE(lastLoad(target), QStringLiteral("load https://example.com/a.mp4 0"));
    }

    void libraryKeepsEpisodesByItem()
    {
        Storage storage({});
        FakeTarget target;
        Session session(&target);
        Browser browser(&session);
        Library library(storage, session, browser);

        MetaPreview show;
        show.adapter = QStringLiteral("stremio");
        show.provider = QStringLiteral("org.example");
        show.id = QStringLiteral("tt1");
        show.type = QStringLiteral("series");
        show.name = QStringLiteral("Show");
        show.poster = QStringLiteral("https://example.com/poster.jpg");
        const PlayContext second{show, VideoEntry{QStringLiteral("tt1:1:2"), QStringLiteral("Second"), 1, 2, {}}};

        // As Browser::play does it.
        emit browser.aboutToPlay(second);
        session.playSource(PlayableSource::fromUrl(QUrl(QStringLiteral("https://example.com/s1e2.mkv")), QStringLiteral("S1E2")));
        target.startPlaying(1500);
        target.movePosition(700);
        target.setState(PlaybackState::Paused);
        const QString key = QStringLiteral("stremio:tt1:tt1:1:2");
        QVERIFY(storage.find(key));
        QCOMPARE(storage.find(key)->position, 700.0);
        QCOMPARE(storage.find(key)->poster, show.poster);

        const QVariantMap row = library.continueWatching().value(0).toMap();
        QCOMPARE(row.value("name").toString(), QStringLiteral("Show"));
        QCOMPARE(row.value("subtitle").toString(), QStringLiteral("S1 E2 · Second"));
        QCOMPARE(row.value("itemKey").toString(), QStringLiteral("stremio:tt1"));
        QVERIFY(row.value("canOpen").toBool());

        // Another stream of the same episode goes on from there too.
        session.open(QUrl(QStringLiteral("https://example.com/other.mp4")));
        emit browser.aboutToPlay(second);
        session.playSource(PlayableSource::fromUrl(QUrl(QStringLiteral("https://mirror.example.com/x.mkv")), {}));
        QCOMPARE(lastLoad(target), QStringLiteral("load https://mirror.example.com/x.mkv 700"));
        target.startPlaying(1500);
        target.movePosition(800);
        target.setState(PlaybackState::Paused);
        QCOMPARE(storage.find(key)->position, 800.0);
        QCOMPARE(storage.find(key)->source.url, QUrl(QStringLiteral("https://mirror.example.com/x.mkv")));

        // Saved items.
        QVERIFY(!library.contains(QStringLiteral("stremio:tt1")));
        library.add(QVariantMap{{"adapter", "stremio"}, {"provider", "org.example"}, {"id", "tt1"},
                                {"type", "series"}, {"name", "Show"}});
        QVERIFY(library.contains(QStringLiteral("stremio:tt1")));
        QCOMPARE(library.items().size(), 1);
        library.remove(QStringLiteral("stremio:tt1"));
        QVERIFY(library.items().isEmpty());

        library.forget(QStringLiteral("stremio:tt1"));
        QVERIFY(library.continueWatching().isEmpty());
    }

    void libraryResumesAMagnetLinkByWhatItWasOpenedFrom()
    {
        Storage storage({});
        FakeTarget target;
        Session session(&target);
        // Like the torrent transport: a magnet link becomes a stream URL.
        int resolved = 0;
        session.addSourceResolver([&](const QString& input) -> std::optional<PlayableSource> {
            if (!input.startsWith(QLatin1String("magnet:")))
                return std::nullopt;
            ++resolved;
            return PlayableSource::fromUrl(QUrl(QStringLiteral("torrent://abc/%1").arg(resolved)), QStringLiteral("Film"));
        });
        Browser browser(&session);
        Library library(storage, session, browser);

        const QString magnet = QStringLiteral("magnet:?xt=urn:btih:abc");
        session.openPath(magnet);
        QCOMPARE(lastLoad(target), QStringLiteral("load torrent://abc/1 0"));
        target.startPlaying(5000);
        target.movePosition(1200);
        target.setState(PlaybackState::Paused);
        QVERIFY(storage.find(QStringLiteral("url:") + magnet));

        // Opened again, it gets a new stream URL but the same spot.
        session.open(QUrl(QStringLiteral("https://example.com/b.mp4")));
        session.openPath(magnet);
        QCOMPARE(lastLoad(target), QStringLiteral("load torrent://abc/2 1200"));

        // And from Continue Watching.
        session.open(QUrl(QStringLiteral("https://example.com/b.mp4")));
        library.resume(QStringLiteral("url:") + magnet);
        QCOMPARE(lastLoad(target), QStringLiteral("load torrent://abc/3 1200"));
    }

    void exportAndImportThroughFiles()
    {
        QTemporaryDir dir;
        Storage storage({});
        FakeTarget target;
        Session session(&target);
        Browser browser(&session);
        Library library(storage, session, browser);
        storage.record(episode(1, 600, 1000));
        const QUrl file = QUrl::fromLocalFile(dir.filePath(QStringLiteral("export.json")));
        QCOMPARE(library.exportTo(file), QString());

        Storage other({});
        Library otherLibrary(other, session, browser);
        QCOMPARE(otherLibrary.importFrom(file), QString());
        QCOMPARE(other.history().size(), 1);
        QVERIFY(!otherLibrary.importFrom(QUrl::fromLocalFile(dir.filePath(QStringLiteral("missing.json")))).isEmpty());
    }

    void exportImportMerges()
    {
        Storage from({});
        from.saveAddons(QJsonArray{QJsonObject{{"transportUrl", "https://a.example/manifest.json"}, {"manifest", QJsonObject{{"id", "a"}}}},
                                   QJsonObject{{"transportUrl", "https://b.example/manifest.json"}, {"manifest", QJsonObject{{"id", "b"}}}}});
        from.saveFolders({QStringLiteral("/videos")});
        from.record(episode(1, 700, 5000));
        from.record(episode(2, 100, 1000));
        LibraryItem item;
        item.adapter = QStringLiteral("stremio");
        item.itemId = QStringLiteral("tt9");
        from.addToLibrary(item);
        const QByteArray json = from.exportJson();

        Storage to({});
        to.saveAddons(QJsonArray{QJsonObject{{"transportUrl", "https://a.example/manifest.json"}, {"manifest", QJsonObject{{"id", "a"}}}}});
        to.record(episode(1, 200, 1000));   // older here: the export wins
        to.record(episode(2, 900, 9000));   // newer here: kept
        QSignalSpy addons(&to, &Storage::addonsImported);
        QSignalSpy folders(&to, &Storage::foldersImported);
        QCOMPARE(to.importJson(json), QString());
        QCOMPARE(addons.count(), 1);
        QCOMPARE(folders.count(), 1);
        QCOMPARE(to.addons()->size(), 2);
        QCOMPARE(*to.folders(), QStringList{QStringLiteral("/videos")});
        QCOMPARE(to.find(episode(1, 0, 0).key)->position, 700.0);
        QCOMPARE(to.find(episode(2, 0, 0).key)->position, 900.0);
        QVERIFY(to.inLibrary(QStringLiteral("stremio:tt9")));

        // Importing the same file again changes nothing.
        QCOMPARE(to.importJson(json), QString());
        QCOMPARE(addons.count(), 1);

        QVERIFY(!to.importJson("not json").isEmpty());
        QVERIFY(!to.importJson("{\"format\":\"other\"}").isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestStorage)
#include "tst_storage.moc"
