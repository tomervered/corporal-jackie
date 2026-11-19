// The app as main() builds it, around the fake target: what the user set is
// kept between runs, a fresh install gets its defaults, an import reloads the
// add-ons and folders, and settings from before the database are copied in.
// Also the torrent preferences and the cache sizes shown in Settings.

#include "AppCore.h"
#include "Browser.h"
#include "Caches.h"
#include "FakeTarget.h"
#include "KeyedListModel.h"
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
        : owned(std::make_unique<AppCore>(QDir(root).filePath(QStringLiteral("data")),
                                          QDir(root).filePath(QStringLiteral("cache"))))
        , core(*owned)
    {
        BrokerSettings s = core.brokerSettings();
        s.loopbackOnly = true;
        torrents = std::make_unique<TorrentTransport>(s);
        core.start(&target, *torrents, false);
    }
    ~Run()
    {
        core.storage().flush();
        owned.reset();   // before the target and transport it uses
    }

    FakeTarget target;
    std::unique_ptr<TorrentTransport> torrents;
    std::unique_ptr<AppCore> owned;
    AppCore& core;
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

    // A list that gains a page, or whose row finished loading, changes in
    // place: the views keep their delegates and posters.
    void keyedListChangesInPlace()
    {
        KeyedListModel model;
        QSignalSpy resets(&model, &QAbstractItemModel::modelReset);
        QSignalSpy inserts(&model, &QAbstractItemModel::rowsInserted);
        QSignalSpy removes(&model, &QAbstractItemModel::rowsRemoved);
        QSignalSpy changes(&model, &QAbstractItemModel::dataChanged);
        const auto entry = [](const char* key, const char* name) {
            return QVariantMap{{"key", QString::fromLatin1(key)}, {"name", QString::fromLatin1(name)}};
        };

        model.setValues({entry("a", "A"), entry("b", "B")});
        QCOMPARE(model.rowCount(), 2);
        QCOMPARE(inserts.size(), 1);

        model.setValues({entry("a", "A"), entry("b", "B, loaded"), entry("c", "C")});
        QCOMPARE(inserts.size(), 2);
        QCOMPARE(changes.size(), 1);
        QCOMPARE(model.data(model.index(1), KeyedListModel::kEntryRole).toMap().value("name").toString(),
                 QStringLiteral("B, loaded"));

        model.setValues({entry("a", "A")});
        QCOMPARE(removes.size(), 1);
        QCOMPARE(resets.size(), 0);

        // Another list altogether.
        model.setValues({entry("x", "X")});
        QCOMPARE(resets.size(), 1);
        QCOMPARE(model.rowCount(), 1);
    }

    void freshInstallGetsDefaults()
    {
        Run run(m_root->path());
        QVERIFY(run.core.storage().isOpen());
        // No add-ons: WikiMaze comes only with the first-run answer.
        QVERIFY(run.core.addons().addons().isEmpty());
        QVERIFY(run.core.localFiles().folders().isEmpty());
        QVERIFY(run.core.session().hardwareDecoding());
        QCOMPARE(run.core.session().subtitleScale(), 1.0);
        QVERIFY(run.core.storage().setting(QString::fromLatin1(AppCore::kImportedOldSettings)).toBool());
        QVERIFY(run.core.storage().addons().has_value());
    }

    // Until the person answers the first-run screen, nothing optional is on.
    void nothingIsContactedBeforeTheAnswer()
    {
        Run run(m_root->path());
        const Preferences& p = run.core.preferences();
        QVERIFY(!p.privacyChosen());
        QVERIFY(!p.useCatalogAddon() && !p.useDht() && !p.usePublicTrackers() && !p.usePortMapping());
        const BrokerSettings s = run.core.brokerSettings();
        QVERIFY(!s.dht && !s.localDiscovery && !s.portMapping && !s.publicTrackers);
        QVERIFY(!s.torrents && !s.upload);
        QVERIFY(run.core.addons().addons().isEmpty());
    }

    // Torrents and uploading are their own answers, off unless given.
    void torrentsAndUploadsAreAskedForAndKept()
    {
        {
            Run run(m_root->path());
            Preferences& p = run.core.preferences();
            p.choosePrivacy(false, false, false, false);
            QVERIFY(!run.core.brokerSettings().torrents && !run.core.brokerSettings().upload);
            p.choosePrivacy(false, false, false, false, true, false);
            QVERIFY(run.core.brokerSettings().torrents && !run.core.brokerSettings().upload);
            p.setAllowUploads(true);
            QVERIFY(run.core.brokerSettings().upload);
        }
        Run run(m_root->path());
        QVERIFY(run.core.preferences().useTorrents() && run.core.preferences().allowUploads());
    }

    // The look (Settings > Appearance) is kept, and only known modes and
    // #rrggbb colours are taken.
    void theLookIsKept()
    {
        {
            Run run(m_root->path());
            Preferences& p = run.core.preferences();
            QCOMPARE(p.colorMode(), QStringLiteral("dark"));
            QSignalSpy changed(&p, &Preferences::appearanceChanged);
            p.setColorMode(QStringLiteral("bright"));
            p.setColorMode(QStringLiteral("neon"));   // not a mode
            QCOMPARE(p.colorMode(), QStringLiteral("bright"));
            p.setColorEdits({{QStringLiteral("bright"), QVariantMap{{QStringLiteral("accent"), QStringLiteral("#AA3300")},
                                                                     {QStringLiteral("text"), QStringLiteral("red")}}},
                             {QStringLiteral("neon"), QVariantMap{{QStringLiteral("accent"), QStringLiteral("#000000")}}}});
            QCOMPARE(changed.count(), 2);
            QVERIFY(p.showUpscalerBadge());
            p.setShowUpscalerBadge(false);
        }
        Run run(m_root->path());
        const Preferences& p = run.core.preferences();
        QCOMPARE(p.colorMode(), QStringLiteral("bright"));
        const QVariantMap expected{{QStringLiteral("bright"), QVariantMap{{QStringLiteral("accent"), QStringLiteral("#aa3300")}}}};
        QCOMPARE(p.colorEdits(), expected);
        QVERIFY(!p.showUpscalerBadge());
    }

    // The Custom colors go to a file and come back; a file that is not one
    // (or lacks a color) changes nothing.
    void colorsFileRoundTrip()
    {
        Run run(m_root->path());
        Preferences& p = run.core.preferences();
        QTemporaryDir dir;
        const QUrl file = QUrl::fromLocalFile(dir.filePath(QStringLiteral("mine.json")));
        const QVariantMap colors{{QStringLiteral("accent"), QStringLiteral("#7aa2f7")}, {QStringLiteral("canvas"), QStringLiteral("#11151d")},
                                 {QStringLiteral("panel"), QStringLiteral("#1a2030")}, {QStringLiteral("text"), QStringLiteral("#E4E9F2")}};
        QCOMPARE(p.exportColors(file, colors), QString());
        QVERIFY(!p.exportColors(file, {{QStringLiteral("accent"), QStringLiteral("#ffffff")}}).isEmpty());
        QVERIFY(!p.exportColors(QUrl(QStringLiteral("https://example.com/x.json")), colors).isEmpty());
        p.setColorMode(QStringLiteral("dark"));
        QCOMPARE(p.importColors(file), QString());
        QCOMPARE(p.colorMode(), QStringLiteral("custom"));
        QCOMPARE(p.colorEdits().value(QStringLiteral("custom")).toMap().value(QStringLiteral("text")).toString(), QStringLiteral("#e4e9f2"));

        QFile other(dir.filePath(QStringLiteral("other.json")));
        QVERIFY(other.open(QIODevice::WriteOnly));
        other.write("{\"accent\": \"#000000\", \"canvas\": \"#000000\", \"panel\": \"#000000\", \"text\": \"#000000\"}");
        other.close();
        QVERIFY(!p.importColors(QUrl::fromLocalFile(other.fileName())).isEmpty());
        QCOMPARE(p.colorEdits().value(QStringLiteral("custom")).toMap().value(QStringLiteral("accent")).toString(), QStringLiteral("#7aa2f7"));
    }

    // A torrent while torrents are off never reaches the player.
    void torrentsOffAreRefusedBeforeThePlayer()
    {
        Run run(m_root->path());
        Session& session = run.core.session();
        QSignalSpy errors(&session, &Session::errorOccurred);
        const QUrl torrent(QStringLiteral("torrent://0123456789abcdef0123456789abcdef01234567"));
        session.open(torrent);
        QCOMPARE(errors.count(), 1);
        QCOMPARE(errors.first().first().value<PlaybackError>(), PlaybackError::TorrentsOff);
        QVERIFY(!run.target.sent(QStringLiteral("load")));
        QVERIFY(!session.loading());
        // Local files and web links are not affected.
        session.open(QUrl(QStringLiteral("http://127.0.0.1:1/clip.mkv")));
        QVERIFY(run.target.sent(QStringLiteral("load http://127.0.0.1:1/clip.mkv")));
        run.core.preferences().choosePrivacy(false, false, false, false, true, false);
        session.open(torrent);
        QVERIFY(run.target.sent(QStringLiteral("load ") + torrent.toString()));
        QCOMPARE(errors.count(), 1);
    }

    // An answer given before the screen asked about torrents is asked again,
    // starting from the earlier answers; torrents stay off until then.
    void theScreenAsksAgainAfterNewQuestions()
    {
        {
            Run run(m_root->path());
            run.core.preferences().choosePrivacy(true, true, false, false, true, true);
            run.core.storage().setSetting(QStringLiteral("privacy/version"), 1);
        }
        Run run(m_root->path());
        const Preferences& p = run.core.preferences();
        QVERIFY(!p.privacyChosen());
        QVERIFY(!p.useTorrents() && !p.allowUploads() && !p.useDht());
        QCOMPARE(p.earlierChoices().value(QStringLiteral("catalogAddon")).toBool(), true);
        QCOMPARE(p.earlierChoices().value(QStringLiteral("dht")).toBool(), true);
        QCOMPARE(p.earlierChoices().value(QStringLiteral("trackers")).toBool(), false);
    }

    void theAnswerAppliesAtOnceAndIsKept()
    {
        {
            Run run(m_root->path());
            Preferences& p = run.core.preferences();
            QSignalSpy chosen(&p, &Preferences::privacyChanged);
            p.choosePrivacy(true, true, false, true);
            QCOMPARE(chosen.count(), 1);
            QVERIFY(p.privacyChosen());
            QCOMPARE(addonUrls(run.core.addons()), QStringList{QString::fromLatin1(StremioAdapter::kDefaultAddonUrl)});
            const BrokerSettings s = run.core.brokerSettings();
            QVERIFY(s.dht && s.localDiscovery && s.portMapping);
            QVERIFY(!s.publicTrackers);
            // WikiMaze goes when the switch goes off, and back when it is on again.
            p.setUseCatalogAddon(false);
            QVERIFY(run.core.addons().addons().isEmpty());
            p.setUseCatalogAddon(true);
            QCOMPARE(run.core.addons().addons().size(), 1);
            p.setUseDht(false);
            QVERIFY(!run.core.brokerSettings().dht && !run.core.brokerSettings().localDiscovery);
            p.setUsePublicTrackers(true);
        }
        // Next run: the answers stand, and nothing asks again.
        Run run(m_root->path());
        const Preferences& p = run.core.preferences();
        QVERIFY(p.privacyChosen());
        QVERIFY(p.useCatalogAddon() && !p.useDht() && p.usePublicTrackers() && p.usePortMapping());
        QCOMPARE(run.core.addons().addons().size(), 1);
    }

    void changingOtherChoicesLeavesTheAddonListAlone()
    {
        Run run(m_root->path());
        Preferences& p = run.core.preferences();
        p.choosePrivacy(true, false, false, false);
        run.core.browser().removeAddon(QStringLiteral("community.wikimaze"));
        QVERIFY(run.core.addons().addons().isEmpty());
        // The person removed WikiMaze on the Add-ons page: no other switch brings it back.
        p.setUseDht(true);
        p.setUsePortMapping(true);
        QVERIFY(run.core.addons().addons().isEmpty());
    }

    // An install from before the first-run screen keeps its other add-ons but
    // loses WikiMaze until the person says yes.
    void defaultAddonWaitsForTheAnswerOnAnOlderInstall()
    {
        {
            Run run(m_root->path());
            run.core.preferences().choosePrivacy(true, false, false, false);
            QCOMPARE(run.core.addons().addons().size(), 1);
            // Back to how an old install looks: WikiMaze there, no answer.
            run.core.storage().setSetting(QStringLiteral("privacy/chosen"), false);
        }
        {
            Run run(m_root->path());
            QVERIFY(!run.core.preferences().privacyChosen());
            QVERIFY(run.core.addons().addons().isEmpty());
            QVERIFY(run.core.storage().addons()->isEmpty());   // saved that way
            run.core.preferences().choosePrivacy(false, false, false, false);
        }
        Run run(m_root->path());
        QVERIFY(run.core.preferences().privacyChosen());
        QVERIFY(run.core.addons().addons().isEmpty());
    }

    // Cinemeta, the default before WikiMaze, leaves an install once; added
    // again by hand, it stays.
    void cinemetaLeavesOnceOnUpgrade()
    {
        const QJsonObject cinemeta{{"transportUrl", QStringLiteral("https://v3-cinemeta.strem.io/manifest.json")},
                                   {"manifest", QJsonObject{{"id", QString::fromLatin1(StremioAdapter::kCinemetaId)},
                                                            {"version", "3.0.0"},
                                                            {"name", "Cinemeta"},
                                                            {"resources", QJsonArray{"catalog", "meta"}},
                                                            {"types", QJsonArray{"movie", "series"}},
                                                            {"catalogs", QJsonArray{}}}}};
        {
            Run run(m_root->path());
            run.core.preferences().choosePrivacy(true, false, false, false);
            run.core.storage().saveAddons(QJsonArray{cinemeta});
            run.core.storage().setSetting(QStringLiteral("addons/cinemetaRetired"), false);
        }
        {
            Run run(m_root->path());
            // Gone at the first start with WikiMaze as the default.
            QVERIFY(addonUrls(run.core.addons()).isEmpty());
            run.core.addons().restore(QJsonArray{cinemeta});
            run.core.storage().saveAddons(run.core.addons().storedList());
        }
        Run run(m_root->path());
        QCOMPARE(addonUrls(run.core.addons()), QStringList{QStringLiteral("https://v3-cinemeta.strem.io/manifest.json")});
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
            run.core.preferences().choosePrivacy(true, false, false, false);
            QCOMPARE(run.core.addons().addons().size(), 1);
            run.core.browser().removeAddon(QStringLiteral("community.wikimaze"));
            QVERIFY(run.core.addons().addons().isEmpty());
            QVERIFY(run.core.browser().addFolder(QUrl::fromLocalFile(videos.path())));
        }
        Run run(m_root->path());
        // Removing the last add-on is kept: no WikiMaze comes back.
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
            run.core.preferences().choosePrivacy(true, false, false, false);
            QVERIFY(run.core.browser().addFolder(QUrl::fromLocalFile(videos.path())));
            exported = run.core.storage().exportJson();
        }
        m_root = std::make_unique<QTemporaryDir>();
        Run run(m_root->path());
        QVERIFY(run.core.addons().addons().isEmpty());
        QSignalSpy addons(&run.core.addons(), &StremioAdapter::addonsChanged);
        QSignalSpy folders(&run.core.localFiles(), &LocalLibrary::foldersChanged);
        QCOMPARE(run.core.storage().importJson(exported, Storage::ImportExtras::Include), QString());
        QVERIFY(addons.count() > 0);
        QVERIFY(folders.count() > 0);
        // The export came from an install that said yes to WikiMaze; this one
        // has not answered, so WikiMaze stays out until it does.
        QVERIFY(run.core.addons().addons().isEmpty());
        QCOMPARE(run.core.localFiles().folders().size(), 1);
        run.core.preferences().choosePrivacy(true, false, false, false);
        QCOMPARE(addonUrls(run.core.addons()), QStringList{QString::fromLatin1(StremioAdapter::kDefaultAddonUrl)});
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
        const QJsonObject oldAddon{{"transportUrl", QString::fromLatin1(StremioAdapter::kDefaultAddonUrl)},
                                   {"manifest", QJsonObject{{"id", "org.example.old"},
                                                            {"version", "1.0.0"},
                                                            {"name", "Old"},
                                                            {"resources", QJsonArray{"stream"}},
                                                            {"types", QJsonArray{"movie"}},
                                                            {"catalogs", QJsonArray{}}}}};
        writeFile(QDir(data).filePath(QStringLiteral("addons.json")),
                  QJsonDocument(QJsonObject{{"addons", QJsonArray{oldAddon}}}).toJson());
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
            QVERIFY(!p.torrentsInMemory());  // disk is the default
            QVERIFY(!p.restartNeeded());
            QSignalSpy changed(&p, &Preferences::changed);
            p.setTorrentsInMemory(true);
            p.setDiskCacheGB(3);
            p.setMemoryCapMB(512);
            QVERIFY(changed.count() >= 3);
            QVERIFY(p.restartNeeded());
            // The engine keeps what it started with.
            QVERIFY(!run.core.brokerSettings().memoryOnly);
            p.setCacheFolder(QUrl::fromLocalFile(m_root->path()));
            QVERIFY(p.customCacheFolder());
            // Torrents go in a folder of their own inside the one picked.
            QCOMPARE(QDir(p.cacheFolder()), QDir(m_root->path() + QStringLiteral("/Corporal Jackie torrents")));
        }
        Run run(m_root->path());
        const Preferences& p = run.core.preferences();
        QVERIFY(!p.restartNeeded());
        const BrokerSettings s = run.core.brokerSettings();
        QVERIFY(s.memoryOnly);
        QCOMPARE(s.cacheCapBytes, qint64(3) * 1024 * 1024 * 1024);
        QCOMPARE(s.memoryCapBytes, qint64(512) * 1024 * 1024);
        QCOMPARE(QDir(s.cacheDir), QDir(m_root->path() + QStringLiteral("/Corporal Jackie torrents")));
        run.core.preferences().resetCacheFolder();
        QVERIFY(!run.core.preferences().customCacheFolder());
    }

    void ramOnlyDefaultMovesToDiskOnce()
    {
        // An install from before disk became the default saved RAM only.
        {
            Run run(m_root->path());
            run.core.storage().setSetting(QStringLiteral("torrents/defaultsVersion"), 1);
            run.core.storage().setSetting(QStringLiteral("torrents/inMemory"), true);
        }
        {
            Run run(m_root->path());
            QVERIFY(!run.core.preferences().torrentsInMemory());
            QVERIFY(!run.core.brokerSettings().memoryOnly);
            run.core.preferences().setTorrentsInMemory(true);
        }
        // Moved once: a later choice of RAM only stays.
        Run run(m_root->path());
        QVERIFY(run.core.preferences().torrentsInMemory());
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
