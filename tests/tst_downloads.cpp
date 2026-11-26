// Downloads to a chosen folder: a torrent read in order through the stream
// broker, a torrent downloaded the normal way (TorrentDownloader), a web
// link; sharing per download; cancelling; file names from outside; and that
// only the folder and the mode are stored.

#include "Downloads.h"
#include "FakeHttpServer.h"
#include "Preferences.h"
#include "LibraryDatabase.h"
#include "TorrentDownloader.h"
#include "TorrentHarness.h"
#include "TorrentTransport.h"

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using namespace cj;
using cj::test::FakeHttpServer;
using harness::Seeder;
using harness::TestTorrent;

namespace {

constexpr int kPiece = 16 * 1024;

BrokerSettings loopback(const QString& cacheDir)
{
    BrokerSettings s;
    s.cacheDir = cacheDir;
    s.loopbackOnly = true;
    s.metadataTimeoutMs = 30000;
    s.upload = false;
    return s;
}

QByteArray readAll(const QString& path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

// The pieces main() and PlayerApplication put together, without a player.
struct Rig
{
    explicit Rig(const QString& root)
        : storage(QDir(root).filePath(QStringLiteral("db.sqlite")))
        , preferences(storage, QDir(root).filePath(QStringLiteral("cache")))
        , transport(loopback(QDir(root).filePath(QStringLiteral("cache"))))
        , downloads(storage, preferences, &transport, QDir(root).filePath(QStringLiteral("Downloads")))
    {
        // Torrents on, uploads off: as a person who said yes to torrents only.
        preferences.choosePrivacy(false, false, false, false, true, false);
        downloads.setNetwork(loopback(QDir(root).filePath(QStringLiteral("cache"))));
    }

    QVariantMap item(int i = 0) const { return downloads.items().value(i).toMap(); }
    bool waitFor(const QString& state, int ms = 60000)
    {
        return QTest::qWaitFor([&] { return item().value(QStringLiteral("state")).toString() == state; }, ms);
    }

    LibraryDatabase storage;
    Preferences preferences;
    TorrentTransport transport;
    Downloads downloads;
};

PlayableSource torrentSource(const QString& magnet, const QString& title)
{
    PlayableSource s;
    s.url = QUrl(magnet);
    s.kind = PlayableSource::Kind::Torrent;
    s.title = title;
    return s;
}

PlayableSource webSource(const QUrl& url, const QString& title, QMap<QString, QString> headers = {})
{
    PlayableSource s = PlayableSource::fromUrl(url, title);
    s.headers = std::move(headers);
    return s;
}

QStringList filesIn(const QString& folder)
{
    return QDir(folder).entryList(QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot, QDir::Name);
}

} // namespace

class TestDownloads : public QObject
{
    Q_OBJECT

private slots:
    void namesFromOutsideAreMadeSafe()
    {
        QCOMPARE(Downloads::safeFileName("Movie.2021.1080p.mkv"), QStringLiteral("Movie.2021.1080p.mkv"));
        QCOMPARE(Downloads::safeFileName("../../etc/passwd"), QStringLiteral("passwd"));
        QCOMPARE(Downloads::safeFileName("..\\..\\Windows\\evil.exe"), QStringLiteral("evil.exe"));
        QCOMPARE(Downloads::safeFileName("a<b>c:d\"e|f?g*h.mp4"), QStringLiteral("a_b_c_d_e_f_g_h.mp4"));
        QCOMPARE(Downloads::safeFileName("CON"), QStringLiteral("_CON"));
        QCOMPARE(Downloads::safeFileName("nul.mkv"), QStringLiteral("_nul.mkv"));
        QCOMPARE(Downloads::safeFileName("trailing dots... "), QStringLiteral("trailing dots"));
        QCOMPARE(Downloads::safeFileName(".."), QStringLiteral("video"));
        QCOMPARE(Downloads::safeFileName(""), QStringLiteral("video"));
        QCOMPARE(Downloads::safeFileName(QStringLiteral("tab\there")), QStringLiteral("tab_here"));
        const QString longName = QString(300, QLatin1Char('x')) + QStringLiteral(".mkv");
        const QString cut = Downloads::safeFileName(longName);
        QVERIFY(cut.size() <= 180);
        QVERIFY(cut.endsWith(QLatin1String(".mkv")));
    }

    void folderAndModeAreKeptAndNothingElse()
    {
        QTemporaryDir dir;
        const QString chosen = dir.filePath("Chosen");
        {
            Rig rig(dir.path());
            QVERIFY(!rig.downloads.customFolder());
            QCOMPARE(rig.downloads.folder(), QDir::toNativeSeparators(dir.filePath("Downloads")));
            rig.downloads.setFolder(QUrl::fromLocalFile(chosen));
            rig.downloads.setNormalTorrents(true);
            rig.storage.flush();
        }
        Rig rig(dir.path());
        QCOMPARE(rig.downloads.folder(), QDir::toNativeSeparators(chosen));
        QVERIFY(rig.downloads.customFolder());
        QVERIFY(rig.downloads.normalTorrents());
        rig.downloads.resetFolder();
        QVERIFY(!rig.downloads.customFolder());
    }

    // The list comes back after a restart: a finished file with its place, a
    // download that was running as interrupted; nothing is kept with the
    // history switched off.
    void theListSurvivesARestart()
    {
        QTemporaryDir dir;
        FakeHttpServer server;
        FakeHttpServer::Answer a;
        a.body = QByteArray(200 * 1024, 'x');
        a.contentType = "video/mp4";
        server.set("/a.mp4", a);
        {
            Rig rig(dir.path());
            QCOMPARE(rig.downloads.start(webSource(QUrl(server.base() + "/a.mp4"), "Kept")), QString());
            QVERIFY(rig.waitFor("done", 10000));
        }
        {
            Rig rig(dir.path());
            QCOMPARE(rig.downloads.items().size(), 1);
            QCOMPARE(rig.item().value("state").toString(), QStringLiteral("done"));
            QVERIFY(QFile::exists(rig.item().value("path").toString()));
            QFile::remove(rig.item().value("path").toString());
        }
        {
            Rig rig(dir.path());
            QCOMPARE(rig.item().value("state").toString(), QStringLiteral("failed"));   // the file is gone
            rig.storage.setKeepsHistory(false);
            rig.downloads.clearFinished();
        }
        Rig rig(dir.path());
        QCOMPARE(rig.downloads.items().size(), 0);
    }

    void streamableTorrentIsSavedWhole()
    {
        QTemporaryDir dir;
        const TestTorrent t = harness::makeTorrent(dir.filePath("seed"), "Big Pack", {300 * 1024, 700 * 1024}, kPiece, 7);
        Seeder seeder(t, t.root);
        Rig rig(dir.path());
        const int file = t.indexOf("file1.bin");
        QVERIFY(file >= 0);
        const QString magnet = harness::magnet(t, {seeder.port()}) + "&so=" + QString::number(file);

        QCOMPARE(rig.downloads.start(torrentSource(magnet, "Big Pack")), QString());
        QCOMPARE(rig.downloads.activeCount(), 1);
        QCOMPARE(rig.downloads.start(torrentSource(magnet, "Big Pack")), QStringLiteral("This torrent is already downloading."));
        QVERIFY(rig.waitFor("done"));
        const QString saved = rig.item().value("path").toString();
        QCOMPARE(QFileInfo(saved).fileName(), QStringLiteral("file1.bin"));
        QCOMPARE(QFileInfo(saved).absolutePath(), QDir(dir.filePath("Downloads")).absolutePath());
        QCOMPARE(readAll(saved), t.data[1]);
        QCOMPARE(rig.item().value("mode").toString(), QStringLiteral("stream"));
        QCOMPARE(filesIn(dir.filePath("Downloads")), QStringList{"file1.bin"});   // no .part left

        // The same file again gets a name of its own.
        QCOMPARE(rig.downloads.start(torrentSource(magnet, "Big Pack")), QString());
        QVERIFY(rig.waitFor("done"));
        QCOMPARE(QFileInfo(rig.item().value("path").toString()).fileName(), QStringLiteral("file1 (2).bin"));
    }

    void normalTorrentIsSavedAndCanShare()
    {
        QTemporaryDir dir;
        const TestTorrent t = harness::makeTorrent(dir.filePath("seed"), "Pack", {200 * 1024, 900 * 1024}, kPiece, 11);
        Seeder seeder(t, t.root);
        Rig rig(dir.path());
        rig.downloads.setNormalTorrents(true);
        const int file = t.indexOf("file1.bin");
        const QString magnet = harness::magnet(t, {seeder.port()}) + "&so=" + QString::number(file);

        QCOMPARE(rig.downloads.start(torrentSource(magnet, "Pack")), QString());
        QCOMPARE(rig.item().value("mode").toString(), QStringLiteral("normal"));
        QVERIFY(!rig.item().value("sharing").toBool());   // uploads were not allowed
        QVERIFY(rig.item().value("canShare").toBool());
        rig.downloads.setSharing(rig.item().value("id").toInt(), true);
        QVERIFY(rig.item().value("sharing").toBool());
        // Shared, so it stays in the session seeding once done.
        QVERIFY(rig.waitFor("seeding"));
        const QString saved = rig.item().value("path").toString();
        QCOMPARE(readAll(saved), t.data[1]);
        // The torrent's other file was never wanted, so never written.
        QVERIFY(!QFileInfo::exists(QDir(dir.filePath("Downloads")).filePath("Pack/file0.bin"))
                || QFileInfo(QDir(dir.filePath("Downloads")).filePath("Pack/file0.bin")).size() == 0);

        // Sharing off: it leaves the session and the file stays.
        rig.downloads.setSharing(rig.item().value("id").toInt(), false);
        QCOMPARE(rig.item().value("state").toString(), QStringLiteral("done"));
        QVERIFY(QTest::qWaitFor([&] { return rig.downloads.activeCount() == 0; }, 5000));
        QCOMPARE(readAll(saved), t.data[1]);
    }

    void cancelLeavesNothingBehind()
    {
        QTemporaryDir dir;
        const TestTorrent t = harness::makeTorrent(dir.filePath("seed"), "Slow.bin", {2 * 1024 * 1024}, kPiece, 3);
        Seeder seeder(t, t.root, 64 * 1024);   // slow, so it is still running
        Rig rig(dir.path());
        QCOMPARE(rig.downloads.start(torrentSource(harness::magnet(t, {seeder.port()}), "Slow")), QString());
        QVERIFY(rig.waitFor("downloading"));
        QVERIFY(QTest::qWaitFor([&] { return !filesIn(dir.filePath("Downloads")).isEmpty(); }, 10000));
        rig.downloads.cancel(rig.item().value("id").toInt());
        QVERIFY(rig.waitFor("cancelled", 10000));
        QCOMPARE(filesIn(dir.filePath("Downloads")), QStringList());
        rig.downloads.clearFinished();
        QCOMPARE(rig.downloads.items().size(), 0);
    }

    void webLinkIsSaved()
    {
        QTemporaryDir dir;
        FakeHttpServer server;
        const QByteArray body = harness::testBytes(500 * 1024, 5);
        FakeHttpServer::Answer a;
        a.body = body;
        a.contentType = "video/x-matroska";
        server.set("/files/clip", a);
        Rig rig(dir.path());

        // No name in the link: the title, with an extension from the type.
        QCOMPARE(rig.downloads.start(webSource(QUrl(server.base() + "/files/clip"), "My: Clip")), QString());
        QVERIFY(rig.waitFor("done", 10000));
        const QString saved = rig.item().value("path").toString();
        QCOMPARE(QFileInfo(saved).fileName(), QStringLiteral("My_ Clip.mkv"));
        QCOMPARE(readAll(saved), body);
        QCOMPARE(rig.item().value("canShare").toBool(), false);
    }

    void headersNeverFollowARedirectElsewhere()
    {
        QTemporaryDir dir;
        FakeHttpServer origin, other;
        FakeHttpServer::Answer moved;
        moved.status = 302;
        moved.location = (other.base() + "/steal.mp4").toUtf8();
        origin.set("/video.mp4", moved);
        FakeHttpServer::Answer file;
        file.body = "secret video";
        other.set("/steal.mp4", file);
        Rig rig(dir.path());

        QCOMPARE(rig.downloads.start(webSource(QUrl(origin.base() + "/video.mp4"), "v",
                                               {{"Authorization", "Bearer token"}})), QString());
        QVERIFY(rig.waitFor("failed", 10000));
        for (const auto& r : other.requests)
            QVERIFY(!r.headers.contains("authorization"));
        QCOMPARE(filesIn(dir.filePath("Downloads")), QStringList());
    }

    void playlistsAndLocalFilesAreRefused()
    {
        QTemporaryDir dir;
        Rig rig(dir.path());
        QVERIFY(!rig.downloads.start(webSource(QUrl("https://example.com/live/index.m3u8"), "live")).isEmpty());
        QVERIFY(!Downloads::canDownload(PlayableSource::fromUrl(QUrl::fromLocalFile(dir.filePath("a.mkv")), "a")));
        QVERIFY(Downloads::canDownload(webSource(QUrl("https://example.com/a.mp4"), "a")));
        QCOMPARE(rig.downloads.items().size(), 0);
    }

    void torrentsOffAreRefused()
    {
        QTemporaryDir dir;
        Rig rig(dir.path());
        rig.preferences.setUseTorrents(false);
        QVERIFY(!rig.downloads.start(torrentSource("magnet:?xt=urn:btih:0123456789abcdef0123456789abcdef01234567", "x")).isEmpty());
        QVERIFY(!rig.transport.engineStarted());   // nobody was called
    }
};

QTEST_MAIN(TestDownloads)
#include "tst_downloads.moc"
