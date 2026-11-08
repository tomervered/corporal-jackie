// The app as main() builds it, around the fake target: what the user set is
// kept between runs, a fresh install gets its defaults, an import reloads the
// add-ons and folders, and settings from before the database are copied in.
// Also the torrent preferences and the cache sizes shown in Settings.

#include "AppCore.h"
#include "Browser.h"
#include "Caches.h"
#include "FakeTarget.h"
#include "LocalLibrary.h"
#include "Preferences.h"
#include "Session.h"
#include "Storage.h"
#include "StremioAdapter.h"
#include "TorrentTransport.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

using namespace cj;

namespace {

// One run of the app: the core, a transport with no network, the fake target.
struct Run
{
    explicit Run(const QString& root)
        : core(QDir(root).filePath(QStringLiteral("data")), QDir(root).filePath(QStringLiteral("cache")))
    {
        BrokerSettings s = core.brokerSettings();
        s.loopbackOnly = true;
        torrents = std::make_unique<TorrentTransport>(s);
        core.start(&target, *torrents, false);
    }
    ~Run() { core.storage().flush(); }

    AppCore core;
    FakeTarget target;
    std::unique_ptr<TorrentTransport> torrents;
};

QStringList addonUrls(const StremioAdapter& addons)
{
    QStringList urls;
    for (const InstalledAddon& a : addons.addons())
        urls << a.manifestUrl.toString();
    return urls;
}

void writeFile(const QString& path, const QByteArray& bytes)
{
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(bytes);
}

} // namespace

class TestApp : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QCoreApplication::setApplicationName(QStringLiteral("tst_app"));
        QCoreApplication::setOrganizationDomain(QStringLiteral("example.org"));
        // Old settings are read from QSettings: keep them in a test folder.
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settingsDir.path());
    }

    void init()
    {
        QSettings().clear();
        m_root = std::make_unique<QTemporaryDir>();
        QVERIFY(m_root->isValid());
    }

    void freshInstallGetsDefaults()
    {
        Run run(m_root->path());
        QVERIFY(run.core.storage().isOpen());
        QCOMPARE(addonUrls(run.core.addons()), QStringList{QString::fromLatin1(StremioAdapter::kCinemetaUrl)});
        QVERIFY(run.core.localFiles().folders().isEmpty());
        QVERIFY(run.core.session().hardwareDecoding());
        QCOMPARE(run.core.session().subtitleScale(), 1.0);
        QVERIFY(run.core.storage().setting(QString::fromLatin1(AppCore::kImportedOldSettings)).toBool());
        // The default add-on is saved, so the next run does not seed again.
        QVERIFY(run.core.storage().addons().has_value());
    }

    void playbackSettingsAreKept()
    {
        {
            Run run(m_root->path());
            Session& s = run.core.session();
            s.setHardwareDecoding(false);
            SubtitleStyle style;
            style.font = QStringLiteral("Arial");
            style.color = QStringLiteral("#FFFF00");
            style.outline = 1.5;
            style.background = 40;
            style.restyleAll = true;
            s.setSubtitleStyle(style);
            s.setSubtitleScale(1.4);
        }
        Run run(m_root->path());
        const Session& s = run.core.session();
        QVERIFY(!s.hardwareDecoding());
        QCOMPARE(s.subtitleStyle().font, QStringLiteral("Arial"));
        QCOMPARE(s.subtitleStyle().color, QStringLiteral("#FFFF00"));
        QCOMPARE(s.subtitleStyle().outline, 1.5);
        QCOMPARE(s.subtitleStyle().background, 40);
        QVERIFY(s.subtitleStyle().restyleAll);
        QCOMPARE(s.subtitleScale(), 1.4);
        // And the target was told at start.
        QVERIFY(run.target.sent(QStringLiteral("subtitleStyle Arial")));
    }

    void addonsAndFoldersAreKept()
    {
        QTemporaryDir videos;
        {
            Run run(m_root->path());
            run.core.browser().removeAddon(QStringLiteral("com.linvo.cinemeta"));
            QVERIFY(run.core.addons().addons().isEmpty());
            QVERIFY(run.core.browser().addFolder(QUrl::fromLocalFile(videos.path())));
        }
        Run run(m_root->path());
        // Removing the last add-on is kept: no Cinemeta comes back.
        QVERIFY(run.core.addons().addons().isEmpty());
        QCOMPARE(run.core.localFiles().folders().size(), 1);
        QCOMPARE(QDir(run.core.localFiles().folders().first()), QDir(videos.path()));
    }

    void importReloadsAddonsAndFolders()
    {
        QTemporaryDir videos;
        QByteArray exported;
        {
            Run run(m_root->path());
            QVERIFY(run.core.browser().addFolder(QUrl::fromLocalFile(videos.path())));
            exported = run.core.storage().exportJson();
        }
        m_root = std::make_unique<QTemporaryDir>();
        Run run(m_root->path());
        run.core.browser().removeAddon(QStringLiteral("com.linvo.cinemeta"));
        QVERIFY(run.core.addons().addons().isEmpty());
        QSignalSpy addons(&run.core.addons(), &StremioAdapter::addonsChanged);
        QSignalSpy folders(&run.core.localFiles(), &LocalLibrary::foldersChanged);
        QCOMPARE(run.core.storage().importJson(exported), QString());
        QVERIFY(addons.count() > 0);
        QVERIFY(folders.count() > 0);
        QCOMPARE(addonUrls(run.core.addons()), QStringList{QString::fromLatin1(StremioAdapter::kCinemetaUrl)});
        QCOMPARE(run.core.localFiles().folders().size(), 1);
    }

    void oldSettingsAreCopiedOnce()
    {
        {
            QSettings old;
            old.setValue(QStringLiteral("playback/hwdec"), QStringLiteral("false"));
            old.setValue(QStringLiteral("torrents/inMemory"), QStringLiteral("false"));
            old.setValue(QStringLiteral("torrents/diskCacheGB"), QStringLiteral("25"));
        }
        const QString data = QDir(m_root->path()).filePath(QStringLiteral("data"));
        QVERIFY(QDir().mkpath(data));
        const QJsonObject cinemeta{{"transportUrl", QString::fromLatin1(StremioAdapter::kCinemetaUrl)},
                                   {"manifest", QJsonObject{{"id", "org.example.old"},
                                                            {"version", "1.0.0"},
                                                            {"name", "Old"},
                                                            {"resources", QJsonArray{"stream"}},
                                                            {"types", QJsonArray{"movie"}},
                                                            {"catalogs", QJsonArray{}}}}};
        writeFile(QDir(data).filePath(QStringLiteral("addons.json")),
                  QJsonDocument(QJsonObject{{"addons", QJsonArray{cinemeta}}}).toJson());
        writeFile(QDir(data).filePath(QStringLiteral("local-folders.json")),
                  QJsonDocument(QJsonObject{{"folders", QJsonArray{m_root->path()}}}).toJson());

        {
            Run run(m_root->path());
            QVERIFY(!run.core.session().hardwareDecoding());
            QVERIFY(!run.core.preferences().torrentsInMemory());
            QCOMPARE(run.core.preferences().diskCacheGB(), 25);
            QVERIFY(!run.core.brokerSettings().memoryOnly);
            QCOMPARE(run.core.addons().addons().size(), 1);
            QCOMPARE(run.core.addons().addons().first().manifest.id, QStringLiteral("org.example.old"));
            QCOMPARE(run.core.localFiles().folders(), QStringList{m_root->path()});
            run.core.session().setHardwareDecoding(true);
        }
        // Copied once: the old value does not come back over the new one.
        Run run(m_root->path());
        QVERIFY(run.core.session().hardwareDecoding());
    }

    void preferencesApplyNextRun()
    {
        {
            Run run(m_root->path());
            Preferences& p = run.core.preferences();
            QVERIFY(p.torrentsInMemory());
            QVERIFY(!p.restartNeeded());
            QSignalSpy changed(&p, &Preferences::changed);
            p.setTorrentsInMemory(false);
            p.setDiskCacheGB(3);
            p.setMemoryCapMB(512);
            QVERIFY(changed.count() >= 3);
            QVERIFY(p.restartNeeded());
            // The engine keeps what it started with.
            QVERIFY(run.core.brokerSettings().memoryOnly);
            p.setCacheFolder(QUrl::fromLocalFile(m_root->path()));
            QVERIFY(p.customCacheFolder());
            // Torrents go in a folder of their own inside the one picked.
            QCOMPARE(QDir(p.cacheFolder()), QDir(m_root->path() + QStringLiteral("/Corporal Jackie torrents")));
        }
        Run run(m_root->path());
        const Preferences& p = run.core.preferences();
        QVERIFY(!p.restartNeeded());
        const BrokerSettings s = run.core.brokerSettings();
        QVERIFY(!s.memoryOnly);
        QCOMPARE(s.cacheCapBytes, qint64(3) * 1024 * 1024 * 1024);
        QCOMPARE(s.memoryCapBytes, qint64(512) * 1024 * 1024);
        QCOMPARE(QDir(s.cacheDir), QDir(m_root->path() + QStringLiteral("/Corporal Jackie torrents")));
        run.core.preferences().resetCacheFolder();
        QVERIFY(!run.core.preferences().customCacheFolder());
    }

    void preferencesClampValues()
    {
        Run run(m_root->path());
        Preferences& p = run.core.preferences();
        p.setMemoryCapMB(1);
        QCOMPARE(p.memoryCapMB(), 256);
        p.setDiskCacheGB(-5);
        QCOMPARE(p.diskCacheGB(), 1);
        p.setDiskCacheGB(5000);
        QCOMPARE(p.diskCacheGB(), 1000);
    }

    void cachesMeasureAndClear()
    {
        Run run(m_root->path());
        Caches& caches = run.core.caches();
        QCOMPARE(caches.imageCacheMB(), Caches::kDefaultImageCacheMB);
        caches.setImageCacheMB(100);
        QCOMPARE(caches.imageCacheMB(), 100);

        // Something in the image cache folder, as the HTTP cache would leave it.
        const QString images = QDir(m_root->path()).filePath(QStringLiteral("cache/images/data8"));
        QVERIFY(QDir().mkpath(images));
        writeFile(QDir(images).filePath(QStringLiteral("x.d")), QByteArray(3 * 1024 * 1024, 'x'));

        QSignalSpy usage(&caches, &Caches::usageChanged);
        caches.measure();
        QTRY_VERIFY(!caches.busy());
        QCOMPARE(caches.imageCacheUsed(), QStringLiteral("3 MB"));

        caches.clearImages();
        QTRY_VERIFY(!caches.busy());
        QTRY_VERIFY(!QFile::exists(QDir(images).filePath(QStringLiteral("x.d"))));
        caches.measure();
        QTRY_VERIFY(!caches.busy());
        QCOMPARE(caches.imageCacheUsed(), QStringLiteral("under 1 MB"));
    }

    void cacheSizeIsKept()
    {
        {
            Run run(m_root->path());
            run.core.caches().setImageCacheMB(80);
        }
        Run run(m_root->path());
        QCOMPARE(run.core.caches().imageCacheMB(), 80);
    }

    void describeSizes()
    {
        QCOMPARE(Caches::describe(0), QStringLiteral("under 1 MB"));
        QCOMPARE(Caches::describe(5LL * 1024 * 1024), QStringLiteral("5 MB"));
        QCOMPARE(Caches::describe(5LL * 1024 * 1024 * 1024), QStringLiteral("5.0 GB"));
    }

private:
    QTemporaryDir m_settingsDir;
    std::unique_ptr<QTemporaryDir> m_root;
};

QTEST_GUILESS_MAIN(TestApp)
#include "tst_app.moc"
