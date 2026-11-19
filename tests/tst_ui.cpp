// The window as the app shows it, against the fake player and a fake add-on:
// every page opens, the keys do what the shortcut list says, and nothing in
// QML warns along the way (a warning is a broken binding or a missing name).
// Over the video, every key goes to the player's own bindings.
//
// Set CJ_UI_SHOTS to a folder to save a picture of each page there.

#include "AppCore.h"
#include "Browser.h"
#include "FakeHttpServer.h"
#include "FakeTarget.h"
#include "KeyedListModel.h"
#include "Library.h"
#include "Preferences.h"
#include "Session.h"
#include "Storage.h"
#include "TorrentTransport.h"

#include <QDateTime>
#include <QGuiApplication>
#include <QImageReader>
#include <QLocale>
#include <QColor>
#include <QDir>
#include <QJSValue>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlError>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QPainter>
#include <QTest>

#include <memory>

using namespace cj;
using test::FakeHttpServer;

namespace {

// Stands in for the mpv video item: the window only sets its session.
class FakeVideoSurface : public QQuickItem
{
    Q_OBJECT
    Q_PROPERTY(cj::Session* playbackSession MEMBER m_session NOTIFY sessionChanged)
signals:
    void sessionChanged();

private:
    Session* m_session = nullptr;
};

QByteArray manifest()
{
    return R"({"id":"org.example.films","version":"1.0.0","name":"Films","types":["movie","series"],"idPrefixes":["tt"],
               "resources":["catalog","meta","stream"],
               "catalogs":[{"type":"movie","id":"top","name":"Top",
                            "extra":[{"name":"search"},{"name":"skip"},{"name":"genre","options":["Drama","Comedy"]}]}]})";
}

} // namespace

class TestUi : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        // As on a Windows set to Hebrew: the app must still read left to right.
        QLocale::setDefault(QLocale(QLocale::Hebrew, QLocale::Israel));
        QGuiApplication::setLayoutDirection(Qt::RightToLeft);
        AppCore::useLeftToRight();

        // No GPU needed, and pictures can be taken of the window.
        QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
        qmlRegisterType<FakeVideoSurface>("CorporalJackie.Player", 1, 0, "VideoSurface");
        qmlRegisterType<KeyedListModel>("CorporalJackie.Player", 1, 0, "KeyedList");
        qmlRegisterUncreatableType<Session>("CorporalJackie.Player", 1, 0, "Session", QStringLiteral("Use the app's session"));
        qmlRegisterUncreatableMetaObject(cj::staticMetaObject, "CorporalJackie.Player", 1, 0, "Playback",
                                         QStringLiteral("Enums only"));

        m_server.setJson("/films/manifest.json", manifest());
        m_server.setJson("/films/catalog/movie/top.json",
                         R"({"metas":[{"id":"tt1","type":"movie","name":"First Film","releaseInfo":"2001"},
                                      {"id":"tt2","type":"movie","name":"Second Film"}]})");
        m_server.setJson("/films/catalog/movie/top/search=second.json",
                         R"({"metas":[{"id":"tt2","type":"movie","name":"Second Film"}]})");
        m_server.setJson("/films/catalog/movie/top/genre=Drama.json",
                         R"({"metas":[{"id":"tt1","type":"movie","name":"First Film"}]})");
        m_server.setJson("/films/meta/movie/tt1.json",
                         R"({"meta":{"id":"tt1","type":"movie","name":"First Film","description":"A film.",
                                     "releaseInfo":"2001","genres":["Drama"]}})");
        m_server.setJson("/films/meta/series/tt9.json",
                         R"({"meta":{"id":"tt9","type":"series","name":"A Show","videos":[
                               {"id":"tt9:1:1","season":1,"episode":1,"title":"Pilot"},
                               {"id":"tt9:1:2","season":1,"episode":2,"title":"Second"}]}})");
        m_server.setJson("/films/stream/series/tt9%3A1%3A1.json",
                         R"({"streams":[{"name":"Films\n1080p","title":"ep1.mkv","url":"http:\/\/127.0.0.1:1\/ep1.mkv"}]})");
        m_server.setJson("/films/stream/series/tt9%3A1%3A2.json",
                         R"({"streams":[{"name":"Films\n1080p","title":"ep2.mkv","url":"http:\/\/127.0.0.1:1\/ep2.mkv"}]})");
        m_server.setJson("/films/stream/movie/tt1.json",
                         R"({"streams":[{"name":"Films\n1080p","title":"first.mkv","url":"http:\/\/127.0.0.1:1\/first.mkv"},
                                        {"name":"Films\n720p","title":"small.mkv\n💾 2 GB","url":"http:\/\/127.0.0.1:1\/small.mkv"}]})");

        QVERIFY(m_root.isValid());
        m_core = std::make_unique<AppCore>(m_root.filePath(QStringLiteral("data")), m_root.filePath(QStringLiteral("cache")));
        BrokerSettings s = m_core->brokerSettings();
        s.loopbackOnly = true;
        m_torrents = std::make_unique<TorrentTransport>(s);
        m_core->start(&m_target, *m_torrents, false);

        // Only the fake add-on: WikiMaze would go to the internet.
        Browser& browser = m_core->browser();
        browser.installAddon(m_server.base() + QStringLiteral("/films/manifest.json"));
        QTRY_COMPARE(browser.addons().size(), 1);

        m_engine = std::make_unique<QQmlApplicationEngine>();
        connect(m_engine.get(), &QQmlEngine::warnings, this, [this](const QList<QQmlError>& errors) {
            for (const QQmlError& e : errors)
                m_warnings << e.toString();
        });
        m_core->exposeTo(m_engine->rootContext());
        m_engine->load(QUrl(QStringLiteral("qrc:/CorporalJackie/Main.qml")));
        QVERIFY(!m_engine->rootObjects().isEmpty());
        m_window = qobject_cast<QQuickWindow*>(m_engine->rootObjects().first());
        QVERIFY(m_window);
        QVERIFY(QTest::qWaitForWindowExposed(m_window));
        m_window->requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(m_window));
        m_pages = m_window->findChild<QQuickItem*>(QStringLiteral("pages"));
        QVERIFY(m_pages);
        noWarnings();
    }

    void cleanup()
    {
        noWarnings();
        allTextIsPlain();
    }

    // The first run: a screen over everything, switches off, keys and clicks
    // kept from the window below, until the person answers.
    void firstRunAsksBeforeAnything()
    {
        QQuickItem* screen = m_window->findChild<QQuickItem*>(QStringLiteral("privacyScreen"));
        QVERIFY(screen);
        QVERIFY(screen->isVisible());
        QVERIFY(!m_core->preferences().privacyChosen());
        const QList<QQuickItem*> options = screen->findChildren<QQuickItem*>(QStringLiteral("privacyOption"));
        QCOMPARE(options.size(), 4);
        shot("privacy");

        // Keys go nowhere: B would open the pages, P the settings.
        QTest::keyClick(m_window, Qt::Key_P);
        QVERIFY(!m_window->property("settingsVisible").toBool());

        // Switch on the second one (DHT and local discovery) and continue.
        QQuickItem* dht = options.at(1);
        QTest::mouseClick(m_window, Qt::LeftButton, {}, dht->mapToScene(QPointF(dht->width() - 30, dht->height() / 2)).toPoint());
        QTRY_VERIFY(screen->property("dht").toBool());
        QVERIFY(!screen->property("catalogAddon").toBool());

        // Torrents and uploading are asked on their own; uploading only once
        // torrents are on. No "Allow all".
        QVERIFY(!screen->findChild<QQuickItem*>(QStringLiteral("privacyAllowAll")));
        QVERIFY(screen->findChild<QQuickItem*>(QStringLiteral("copyrightNotice")));
        QQuickItem* torrents = screen->findChild<QQuickItem*>(QStringLiteral("privacyTorrents"));
        QQuickItem* uploads = screen->findChild<QQuickItem*>(QStringLiteral("privacyUploads"));
        QVERIFY(torrents && uploads);
        QVERIFY(!uploads->property("switchEnabled").toBool());
        QTest::mouseClick(m_window, Qt::LeftButton, {},
                          torrents->mapToScene(QPointF(torrents->width() - 30, torrents->height() / 2)).toPoint());
        QTRY_VERIFY(screen->property("torrents").toBool());
        QVERIFY(uploads->property("switchEnabled").toBool());
        QVERIFY(!screen->property("uploads").toBool());
        QQuickItem* go = screen->findChild<QQuickItem*>(QStringLiteral("privacyContinue"));
        QVERIFY(go);
        // The screen scrolls; bring the button into view.
        QQuickItem* flick = go->parentItem()->parentItem()->parentItem();
        QVERIFY(flick->inherits("QQuickFlickable"));
        flick->setProperty("contentY", qMax(0.0, flick->property("contentHeight").toDouble() - flick->height()));
        QTest::qWait(50);
        QTest::mouseClick(m_window, Qt::LeftButton, {}, go->mapToScene(QPointF(go->width() / 2, go->height() / 2)).toPoint());
        QTRY_VERIFY(!screen->isVisible());
        const Preferences& p = m_core->preferences();
        QVERIFY(p.privacyChosen());
        QVERIFY(p.useDht());
        QVERIFY(!p.useCatalogAddon() && !p.usePublicTrackers() && !p.usePortMapping());
        QVERIFY(p.useTorrents() && !p.allowUploads());
        // WikiMaze stayed out; only the fake add-on is here.
        QCOMPARE(m_core->browser().addons().size(), 1);
        noWarnings();
    }

    // Nothing in the window is mirrored, whatever the system language.
    void leftToRightUnderAHebrewSystem()
    {
        QCOMPARE(QGuiApplication::layoutDirection(), Qt::LeftToRight);
        int controls = 0;
        for (QQuickItem* item : m_window->contentItem()->findChildren<QQuickItem*>()) {
            const QVariant mirrored = item->property("mirrored");
            if (!mirrored.isValid())
                continue;
            ++controls;
            QVERIFY2(!mirrored.toBool(), item->metaObject()->className());
        }
        QVERIFY(controls > 0);
    }

    // Many add-ons' poster servers send WebP: without the
    // decoder, those posters show as plain tiles with only the name.
    void postersInWebPCanBeDecoded()
    {
        QVERIFY(QImageReader::supportedImageFormats().contains("webp"));
        QVERIFY(QImageReader::supportedImageFormats().contains("jpeg"));
    }

    void homeShowsTheAddonsLists()
    {
        QVERIFY(m_pages->isVisible());   // nothing plays: the pages fill the window
        QCOMPARE(page(), QStringLiteral("home"));
        QTRY_VERIFY(!rows("homeRows").isEmpty());
        QTRY_COMPARE(rows("homeRows").first().toMap().value("items").toList().size(), 2);
        shot("home");
    }

    void searchFindsInEveryAddon()
    {
        call("goTop", "home");
        m_core->browser().searchAll(QStringLiteral("second"), QString());
        call("go", "search");
        QCOMPARE(page(), QStringLiteral("search"));
        QTRY_COMPARE(rows("searchRows").value(0).toMap().value("items").toList().size(), 1);
        shot("search");
    }

    void discoverShowsAListByGenre()
    {
        call("goTop", "discover");
        Browser& browser = m_core->browser();
        browser.openCatalog(0);
        QTRY_COMPARE(browser.items().size(), 2);
        QCOMPARE(browser.genres(), (QStringList{"Drama", "Comedy"}));
        browser.openCatalog(0, QString(), QStringLiteral("Drama"));
        QTRY_COMPARE(browser.items().size(), 1);
        shot("discover");
    }

    void detailAndWaysToPlay()
    {
        call("goTop", "home");
        QTRY_COMPARE(rows("homeRows").value(0).toMap().value("items").toList().size(), 2);
        QVERIFY(QMetaObject::invokeMethod(&m_core->browser(), "openRowItem", Q_ARG(bool, false), Q_ARG(int, 0), Q_ARG(int, 0)));
        call("go", "detail");
        QTRY_VERIFY(!m_core->browser().detailLoading());
        QCOMPARE(m_core->browser().detail().value("name").toString(), QStringLiteral("First Film"));
        // Opening a movie asks the add-ons nothing; Find streams does.
        QQuickItem* find = m_window->findChild<QQuickItem*>(QStringLiteral("findStreams"));
        QQuickItem* list = m_window->findChild<QQuickItem*>(QStringLiteral("movieStreams"));
        QVERIFY(find && list);
        QTRY_VERIFY(find->isVisible());
        QVERIFY(!list->isVisible());
        QTest::qWait(300);
        QVERIFY(m_core->browser().streams().isEmpty());
        QVERIFY(!m_core->browser().streamsLoading());
        shot("detail-before-search");
        QTest::mouseClick(m_window, Qt::LeftButton, {}, find->mapToScene(QPointF(find->width() / 2, find->height() / 2)).toPoint());
        QTRY_COMPARE(m_core->browser().streams().size(), 2);
        QCOMPARE(page(), QStringLiteral("detail"));
        QTRY_VERIFY(list->isVisible());
        QVERIFY(!find->isVisible());
        const auto shownFiles = [list] {
            QStringList out;
            QVariant shown = list->property("shown");   // a JS array
            if (shown.metaType() == QMetaType::fromType<QJSValue>())
                shown = shown.value<QJSValue>().toVariant();
            for (const QVariant& v : shown.toList())
                out << v.toMap().value("description").toString().section(QLatin1Char('\n'), 0, 0);
            return out;
        };
        QCOMPARE(shownFiles(), (QStringList{"first.mkv", "small.mkv"}));   // best first: 1080p
        shot("detail");

        // Sorted by size, the one with a known size comes first.
        m_pages->setProperty("streamSort", QStringLiteral("largest"));
        QCOMPARE(shownFiles(), (QStringList{"small.mkv", "first.mkv"}));
        // Hiding the add-on hides its ways to play.
        m_pages->setProperty("hiddenProviders", QVariantList{QStringLiteral("Films")});
        QCOMPARE(shownFiles(), QStringList{});
        QCOMPARE(list->property("hiddenCount").toInt(), 2);
        shot("detail-filtered");
        m_pages->setProperty("streamSort", QStringLiteral("best"));
        m_pages->setProperty("hiddenProviders", QVariantList{});

        // Escape goes back.
        QTest::keyClick(m_window, Qt::Key_Escape);
        QVERIFY(page() != QStringLiteral("detail"));
    }

    void libraryAndContinueWatching()
    {
        m_core->library().add(m_core->browser().detail());
        QCOMPARE(m_core->library().items().size(), 1);
        HistoryEntry e;
        e.adapter = QStringLiteral("stremio");
        e.itemId = QStringLiteral("tt1");
        e.itemType = QStringLiteral("movie");
        e.itemName = QStringLiteral("First Film");
        e.videoId = QStringLiteral("tt1");
        e.key = QStringLiteral("stremio:tt1:tt1");
        e.position = 600;
        e.duration = 6000;
        e.updatedAt = QDateTime::currentMSecsSinceEpoch();
        e.source = PlayableSource::fromUrl(QUrl(QStringLiteral("http://127.0.0.1:1/first.mkv")), QStringLiteral("First Film"));
        // A poster with no wide art: Home blurs it behind the whole poster.
        QImage art(200, 300, QImage::Format_RGB32);
        {
            QPainter paint(&art);
            QLinearGradient g(0, 0, 200, 300);
            g.setColorAt(0, QColor(QStringLiteral("#d9822b")));
            g.setColorAt(1, QColor(QStringLiteral("#2b4f8a")));
            paint.fillRect(art.rect(), g);
            paint.setBrush(QColor(QStringLiteral("#f2e6c9")));
            paint.drawEllipse(QPoint(100, 120), 50, 50);
        }
        const QString posterFile = m_root.filePath(QStringLiteral("poster.png"));
        QVERIFY(art.save(posterFile));
        e.poster = QUrl::fromLocalFile(posterFile).toString();
        m_core->storage().record(e);
        QTRY_COMPARE(m_core->library().continueWatching().size(), 1);
        call("goTop", "library");
        shot("library");
        call("goTop", "home");
        QQuickItem* heroPoster = m_window->findChild<QQuickItem*>(QStringLiteral("heroPoster"));
        QVERIFY(heroPoster);
        QTRY_COMPARE(heroPoster->property("status").toInt(), 1);   // Image.Ready
        QVERIFY(m_window->findChild<QQuickItem*>(QStringLiteral("heroBlur"))->isVisible());
        shot("home-continue");
    }

    void addonsAndFolders()
    {
        call("goTop", "addons");
        shot("addons");

        // Installing asks first; the add-on is contacted only after Install.
        QQuickItem* addons = m_window->findChild<QQuickItem*>(QStringLiteral("addonsPage"));
        QQuickItem* consent = m_window->findChild<QQuickItem*>(QStringLiteral("addonConsent"));
        QQuickItem* cancel = m_window->findChild<QQuickItem*>(QStringLiteral("addonConsentCancel"));
        QQuickItem* install = m_window->findChild<QQuickItem*>(QStringLiteral("addonConsentInstall"));
        QVERIFY(addons && consent && cancel && install);
        QVERIFY(!consent->isVisible());
        const QString link = m_server.base() + QStringLiteral("/films/manifest.json");
        const auto click = [](QQuickItem* b) { QVERIFY(QMetaObject::invokeMethod(b, "clicked")); };
        m_server.requests.clear();
        addons->setProperty("pendingLink", link);
        QTRY_VERIFY(consent->isVisible());
        shot("addon-consent");
        click(cancel);
        QTRY_VERIFY(!consent->isVisible());
        QTest::qWait(200);
        QVERIFY(m_server.requests.isEmpty());
        addons->setProperty("pendingLink", link);
        QTRY_VERIFY(consent->isVisible());
        click(install);
        QTRY_VERIFY(!m_server.requests.isEmpty());
        QTRY_VERIFY(!m_core->browser().installing());
        call("goTop", "folders");
        shot("folders");
    }

    void settingsAndShortcuts()
    {
        // From a page whose keyboard is not in a text field (there, P is a letter).
        call("goTop", "library");
        QTest::keyClick(m_window, Qt::Key_P);
        QVERIFY(m_window->property("settingsVisible").toBool());
        shot("settings");
        // A ready-made palette switches to Custom.
        // (Repeater delegates are not QObject children: walk the items.)
        QQuickItem* plum = nullptr;
        QList<QQuickItem*> todo{m_window->contentItem()};
        while (!todo.isEmpty() && !plum) {
            QQuickItem* i = todo.takeLast();
            if (i->objectName() == QLatin1String("palette-Plum"))
                plum = i;
            todo += i->childItems();
        }
        QVERIFY(plum);
        QVERIFY(QMetaObject::invokeMethod(plum, "clicked"));
        QCOMPARE(m_core->preferences().colorMode(), QStringLiteral("custom"));
        QTest::qWait(200);
        shot("settings-custom");
        m_core->preferences().setColorMode(QStringLiteral("dark"));
        QQuickItem* panel = m_window->findChild<QQuickItem*>(QStringLiteral("settingsPanel"));
        QVERIFY(panel);
        for (const char* group : {"Picture", "Subtitles"}) {
            QVERIFY(QMetaObject::invokeMethod(panel, "openGroup", Q_ARG(QVariant, QString::fromLatin1(group))));
            QTest::qWait(300);
            shot(QStringLiteral("settings-") + QString::fromLatin1(group).toLower());
        }
        QQuickItem* badge = m_window->findChild<QQuickItem*>(QStringLiteral("upscalerBadgeToggle"));
        QVERIFY(badge && badge->isVisible());
        QQuickItem* sample = m_window->findChild<QQuickItem*>(QStringLiteral("subtitleSample"));
        QVERIFY(sample && sample->isVisible());
        QTest::keyClick(m_window, Qt::Key_Escape);
        QVERIFY(!m_window->property("settingsVisible").toBool());
    }

    // Settings opens over any page and Escape closes it from any of its
    // controls (the audit found Tab, then Escape, left it open). The other
    // looks restyle the pages.
    void settingsEscapeAndLooks()
    {
        call("goTop", "home");
        QVERIFY(QMetaObject::invokeMethod(m_window, "toggleSettings"));
        QTRY_VERIFY(m_window->property("settingsVisible").toBool());
        QCOMPARE(m_pages->property("shownPage").toString(), QStringLiteral("settings"));
        for (int i = 0; i < 6; ++i)
            QTest::keyClick(m_window, Qt::Key_Tab);
        QQuickItem* focused = m_window->activeFocusItem();
        QVERIFY(focused);
        QQuickItem* panel = m_window->findChild<QQuickItem*>(QStringLiteral("settingsPanel"));
        QVERIFY(panel);
        QVERIFY2(panel->isAncestorOf(focused) || m_pages->isAncestorOf(focused), focused->metaObject()->className());
        QTest::keyClick(m_window, Qt::Key_Escape);
        QTRY_VERIFY(!m_window->property("settingsVisible").toBool());
        QCOMPARE(page(), QStringLiteral("home"));

        Preferences& p = m_core->preferences();
        for (const char* mode : {"bright", "oled", "dark"}) {
            p.setColorMode(QString::fromLatin1(mode));
            QCOMPARE(m_pages->property("color").value<QColor>().name(),
                     QString::fromLatin1(qstrcmp(mode, "bright") == 0 ? "#f7f8f3" : qstrcmp(mode, "oled") == 0 ? "#000000" : "#171b16"));
            shot(QStringLiteral("home-") + QString::fromLatin1(mode));
            // Words on the accent stay readable: white on bright's dark olive.
            QQuickItem* resume = m_window->findChild<QQuickItem*>(QStringLiteral("resumeButton"));
            QVERIFY(resume);
            QColor words;
            for (QQuickItem* c : resume->property("contentItem").value<QQuickItem*>()->childItems())
                if (c->inherits("QQuickText"))
                    words = c->property("color").value<QColor>();
            QCOMPARE(words.name(), QString::fromLatin1(qstrcmp(mode, "bright") == 0 ? "#ffffff" : "#000000"));
        }
    }

    void playerKeys()
    {
        Session& session = m_core->session();
        session.openPath(QStringLiteral("http://127.0.0.1:1/clip.mkv"));
        m_target.startPlaying(600);
        QTRY_VERIFY(session.hasMedia());
        QTRY_VERIFY(!m_pages->isVisible());   // the video shows
        m_target.commands.clear();

        // Every key goes to the player's own bindings; the window acts on none.
        QTest::keyClick(m_window, Qt::Key_Space);
        QVERIFY(m_target.sent(QStringLiteral("keydown %1").arg(int(Qt::Key_Space))));
        QVERIFY(m_target.sent(QStringLiteral("keyup")));
        QVERIFY(!m_target.sent(QStringLiteral("pause")));
        for (int key : {Qt::Key_B, Qt::Key_P, Qt::Key_BracketRight, Qt::Key_I}) {
            QTest::keyClick(m_window, Qt::Key(key));
            QVERIFY(m_target.sent(QStringLiteral("keydown %1").arg(key)));
        }
        QVERIFY(!m_pages->isVisible());
        QVERIFY(!m_window->property("settingsVisible").toBool());
        QCOMPARE(session.speed(), 1.0);

        // What the player's keys change shows in the window.
        m_target.requestAppAction(QStringLiteral("stats"));
        QVERIFY(m_window->property("statsVisible").toBool());
        m_target.changeSetting(QStringLiteral("speed"), 1.5);
        QCOMPARE(session.speed(), 1.5);

        // A video with chapters gets the next-track button and the list.
        emit m_target.chaptersChanged({{0, QStringLiteral("Intro")}, {30, QStringLiteral("Song")}, {60, QString()}});
        QQuickItem* next = m_window->findChild<QQuickItem*>(QStringLiteral("nextTrack"));
        QQuickItem* list = m_window->findChild<QQuickItem*>(QStringLiteral("trackList"));
        QVERIFY(next && list);
        QTRY_VERIFY(next->isVisible() && list->isVisible());
        m_window->setProperty("controlsVisible", true);
        QTest::qWait(250);  // the bar fades in
        m_target.commands.clear();
        QTest::mouseClick(m_window, Qt::LeftButton, {}, next->mapToScene(QPointF(next->width() / 2, next->height() / 2)).toPoint());
        QTRY_COMPARE(m_target.last(), QStringLiteral("chapter 1"));
        shot("player");
        auto click = [this](const char* name) {
            QQuickItem* item = m_window->findChild<QQuickItem*>(QString::fromLatin1(name));
            QVERIFY2(item && item->isVisible(), name);
            QTest::mouseClick(m_window, Qt::LeftButton, {}, item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint());
        };
        auto visible = [this](const char* name) {
            QQuickItem* item = m_window->findChild<QQuickItem*>(QString::fromLatin1(name));
            return item && item->isVisible();
        };

        m_window->setProperty("controlsVisible", true);
        QTest::qWait(250);  // the bar fades in
        // Each button opens its panel above the bar; the same button closes
        // it, another switches. Esc and a click on the picture close it too,
        // without reaching the player.
        click("trackList");
        QTRY_VERIFY(visible("chaptersPanel"));
        shot("player-chapters");
        click("trackList");
        QTRY_VERIFY(!visible("chaptersPanel"));

        Track english{Track::Type::Subtitle, 3, QStringLiteral("SDH"), QStringLiteral("en"), QStringLiteral("subrip")};
        Track audio{Track::Type::Audio, 1, QString(), QStringLiteral("en"), QStringLiteral("eac3")};
        audio.selected = true;
        emit m_target.tracksChanged({english, audio});
        click("captionsButton");
        QTRY_VERIFY(visible("tracksPanel"));
        QTRY_VERIFY(visible("subtitleDelayValue"));
        shot("player-tracks");
        // Style: how subtitles look, changed from the player.
        click("styleTab");
        QTRY_VERIFY(visible("choice-subOutline"));
        QVERIFY(!visible("loadSubtitle") && !visible("subtitleDelayValue"));
        QTest::qWait(100);   // the panel grows and moves up first
        click("choice-subOutline");
        QCOMPARE(m_window->findChild<QQuickItem*>(QStringLiteral("controlBar"))->property("openChoice").toString(), QStringLiteral("subOutline"));
        QTest::qWait(150);
        shot("player-tracks-style");
        click("choice-subOutline");
        click("audioTab");
        QTRY_VERIFY(visible("loadAudio"));
        QVERIFY(!visible("subtitleDelayValue"));
        shot("player-tracks-audio");

        click("optionsButton");
        QTRY_VERIFY(visible("settingsPopup"));
        QVERIFY(!visible("tracksPanel"));
        click("choice-crop");
        QTest::qWait(50);
        shot("player-settings");
        // White panels: the player's controls turn charcoal (the pages keep white).
        {
            Preferences& prefs = m_core->preferences();
            prefs.setColorEdits({{QStringLiteral("custom"), QVariantMap{{QStringLiteral("accent"), QStringLiteral("#8a5a2b")}, {QStringLiteral("canvas"), QStringLiteral("#f6f4ef")},
                                                                         {QStringLiteral("panel"), QStringLiteral("#ffffff")}, {QStringLiteral("text"), QStringLiteral("#2a2723")}}}});
            prefs.setColorMode(QStringLiteral("custom"));
            QQuickItem* gearPanel = m_window->findChild<QQuickItem*>(QStringLiteral("settingsPopup"));
            QVERIFY(gearPanel);
            QTRY_COMPARE(gearPanel->property("color").value<QColor>().name(), QStringLiteral("#24262a"));
            shot("player-settings-charcoal");
            prefs.setKeepLightPlayer(true);
            QTRY_COMPARE(gearPanel->property("color").value<QColor>().name(), QStringLiteral("#ffffff"));
            prefs.setKeepLightPlayer(false);
            prefs.setColorEdits({});
            prefs.setColorMode(QStringLiteral("dark"));
        }
        session.setCrop(QStringLiteral("21:9"));
        QCOMPARE(m_target.last(), QStringLiteral("crop 21:9"));
        session.setCrop(QStringLiteral("5:4"));   // not offered
        QCOMPARE(session.crop(), QStringLiteral("21:9"));
        m_target.commands.clear();
        QTest::keyClick(m_window, Qt::Key_Escape);
        QTRY_VERIFY(!visible("settingsPopup"));
        QVERIFY(!m_target.sent(QStringLiteral("keydown %1").arg(int(Qt::Key_Escape))));
        click("optionsButton");
        QTRY_VERIFY(visible("settingsPopup"));
        QTest::mouseClick(m_window, Qt::LeftButton, {}, QPoint(300, 200));
        QTRY_VERIFY(!visible("settingsPopup"));
        QVERIFY(!m_target.sent(QStringLiteral("pause")));

        // The speaker opens the volume slider on a click only (not under the
        // pointer); a second click closes it. Above 100% it takes the accent.
        QQuickItem* speaker = m_window->findChild<QQuickItem*>(QStringLiteral("volumeButton"));
        QVERIFY(speaker);
        m_window->setProperty("controlsVisible", true);
        QTest::qWait(250);  // the bar fades in
        const QPoint onSpeaker = speaker->mapToScene(QPointF(speaker->width() / 2, speaker->height() / 2)).toPoint();
        QTest::mouseMove(m_window, onSpeaker);
        QTest::qWait(100);
        QVERIFY(!visible("volumePanel"));
        QTest::mouseClick(m_window, Qt::LeftButton, {}, onSpeaker);
        QTRY_VERIFY(visible("volumePanel"));
        session.setVolume(120);
        QCOMPARE(session.volume(), 120.0);
        QCOMPARE(speaker->property("iconName").toString(), QStringLiteral("volume3"));
        QCOMPARE(speaker->property("iconColor").value<QColor>().name(), QStringLiteral("#a0b583"));   // dark look's accent
        shot("player-volume");
        QTest::mouseClick(m_window, Qt::LeftButton, {}, onSpeaker);
        QTRY_VERIFY(!visible("volumePanel"));
        session.setVolume(20);
        QCOMPARE(speaker->property("iconName").toString(), QStringLiteral("volume1"));
        QCOMPARE(speaker->property("iconColor").value<QColor>().name(), QStringLiteral("#eaeee5"));
        session.setVolume(100);

        // Back and Close sit at the top right.
        QVERIFY(visible("backToPages") && visible("closeVideo"));

        // Playback settings › Keyboard shortcuts shows the keys in the middle; the ×,
        // the same item again, or Esc closes them.
        QObject* bar = m_window->findChild<QObject*>(QStringLiteral("controlBar"));
        QQuickItem* sheet = m_window->findChild<QQuickItem*>(QStringLiteral("shortcutsSheet"));
        QVERIFY(bar && sheet);
        QVERIFY(QMetaObject::invokeMethod(bar, "shortcutsRequested"));
        QVERIFY(sheet->isVisible());
        shot("player-shortcuts");
        QQuickItem* closeKeys = m_window->findChild<QQuickItem*>(QStringLiteral("closeShortcuts"));
        QVERIFY(closeKeys);
        QTest::mouseClick(m_window, Qt::LeftButton, {}, closeKeys->mapToScene(QPointF(closeKeys->width() / 2, closeKeys->height() / 2)).toPoint());
        QTRY_VERIFY(!sheet->isVisible());
        QVERIFY(QMetaObject::invokeMethod(bar, "shortcutsRequested"));
        QVERIFY(sheet->isVisible());
        QVERIFY(QMetaObject::invokeMethod(bar, "shortcutsRequested"));
        QVERIFY(!sheet->isVisible());
        QVERIFY(QMetaObject::invokeMethod(bar, "shortcutsRequested"));
        m_target.commands.clear();
        QTest::keyClick(m_window, Qt::Key_Escape);
        QVERIFY(!sheet->isVisible());
        QVERIFY(!m_target.sent(QStringLiteral("keydown %1").arg(int(Qt::Key_Escape))));
        // The diagnostics stay over the video, not over the pages.
        QQuickItem* stats = nullptr;
        for (QQuickItem* item : m_window->contentItem()->childItems())
            if (QByteArray(item->metaObject()->className()).startsWith("StatsOverlay"))
                stats = item;
        QVERIFY(stats && stats->isVisible());
        m_window->setProperty("browseVisible", true);
        QVERIFY(!stats->isVisible());
        m_window->setProperty("browseVisible", false);
        QVERIFY(stats->isVisible());
        m_target.requestAppAction(QStringLiteral("stats"));
        emit m_target.chaptersChanged({});
        QTRY_VERIFY(!next->isVisible());

        // The pages still open over the video (from the Browse button).
        m_window->setProperty("browseVisible", true);
        QVERIFY(m_pages->isVisible());
        shot("pages-over-video");
        m_window->setProperty("browseVisible", false);
        QVERIFY(!m_pages->isVisible());
    }

    // While an episode plays, the Episodes button lists the show's episodes
    // over the video; picking one lists its streams there, and picking a
    // stream plays it.
    void episodesInThePlayer()
    {
        Browser& browser = m_core->browser();
        Session& session = m_core->session();
        const QVariantMap first = rows("homeRows").value(0).toMap().value("items").toList().value(0).toMap();
        browser.openPreview(MetaPreview{first.value("adapter").toString(), first.value("provider").toString(),
                                        QStringLiteral("tt9"), QStringLiteral("series"), QStringLiteral("A Show"), {}, {}, {}});
        QTRY_VERIFY(!browser.detailLoading());
        QCOMPARE(browser.videos().size(), 2);
        browser.openVideo(0);
        QTRY_COMPARE(browser.streams().size(), 1);
        browser.play(0);
        m_target.startPlaying(1200);
        QTRY_VERIFY(session.hasMedia());
        QCOMPARE(browser.playingEpisodes().size(), 2);
        QCOMPARE(browser.playingEpisodeIndex(), 0);

        QQuickItem* panel = m_window->findChild<QQuickItem*>(QStringLiteral("episodesPanel"));
        QVERIFY(panel);
        QVERIFY(QMetaObject::invokeMethod(m_window, "toggleEpisodes"));
        QTRY_VERIFY(panel->isVisible());
        shot("episodes");
        // One panel at a time: a bar panel closes the episodes list, and the
        // list sits under Back and Close.
        QQuickItem* bar = m_window->findChild<QQuickItem*>(QStringLiteral("controlBar"));
        QQuickItem* corner = m_window->findChild<QQuickItem*>(QStringLiteral("playerCorner"));
        QVERIFY(bar && corner);
        QVERIFY(panel->mapToScene(QPointF(0, 0)).y() >= corner->mapToScene(QPointF(0, corner->height())).y());
        QVERIFY(QMetaObject::invokeMethod(bar, "togglePanel", Q_ARG(QVariant, QStringLiteral("settings"))));
        QTRY_VERIFY(!panel->isVisible());
        QVERIFY(QMetaObject::invokeMethod(m_window, "toggleEpisodes"));
        QTRY_VERIFY(panel->isVisible());
        QCOMPARE(bar->property("openPanel").toString(), QString());

        QVERIFY(QMetaObject::invokeMethod(panel, "pick", Q_ARG(QVariant, 1)));
        QTRY_COMPARE(browser.streamsTitle(), QStringLiteral("A Show 1x02"));
        QTRY_COMPARE(browser.streams().size(), 1);
        shot("episode-streams");
        QVERIFY(QMetaObject::invokeMethod(panel, "play", Q_ARG(QVariant, 0)));
        QTRY_VERIFY(!panel->isVisible());
        QCOMPARE(browser.playingEpisodeIndex(), 1);
        QCOMPARE(session.source(), QUrl(QStringLiteral("http://127.0.0.1:1/ep2.mkv")));

        // A link that is not part of the show has no episodes.
        session.openPath(QStringLiteral("http://127.0.0.1:1/clip.mkv"));
        QTRY_VERIFY(browser.playingEpisodes().isEmpty());
        QVERIFY(!QMetaObject::invokeMethod(m_window, "toggleEpisodes") || !panel->isVisible());
    }

private:
    QString page() const { return m_pages->property("page").toString(); }
    QVariantList rows(const char* name) const { return m_core->browser().property(name).toList(); }

    void call(const char* function, const char* arg)
    {
        QVERIFY(QMetaObject::invokeMethod(m_pages, function, Q_ARG(QVariant, QString::fromLatin1(arg))));
        QCoreApplication::processEvents();
    }

    // Every Text the app declares shows plain text: Qt's default would show
    // markup from a file or an add-on, and load its <img> links (src/ui/README).
    // Qt Quick Controls' own labels (button and switch labels, placeholder
    // text: QQuickMnemonicLabel, QQuickCheckLabel...) show only the app's
    // fixed words, and are left out; Chip replaces its own.
    void allTextIsPlain()
    {
        QStringList rich;
        const auto items = m_window->findChildren<QQuickItem*>();
        for (QQuickItem* item : items) {
            const QByteArray type = item->metaObject()->className();
            if (!item->inherits("QQuickText") || (type.startsWith("QQuick") && type != "QQuickText"))
                continue;
            if (item->property("textFormat").toInt() != Qt::PlainText)
                rich << QStringLiteral("%1 \"%2\"").arg(QString::fromLatin1(item->metaObject()->className()),
                                                         item->property("text").toString());
        }
        QVERIFY2(rich.isEmpty(), qPrintable(QStringLiteral("Text that is not plain: ") + rich.join(QStringLiteral(", "))));
    }

    void noWarnings()
    {
        QVERIFY2(m_warnings.isEmpty(), qPrintable(m_warnings.join(QLatin1Char('\n'))));
    }

    // Lets bindings and images settle, then saves a picture if asked.
    void shot(const QString& name)
    {
        QTest::qWait(150);
        const QString dir = qEnvironmentVariable("CJ_UI_SHOTS");
        if (!dir.isEmpty())
            QVERIFY(m_window->grabWindow().save(QDir(dir).filePath(name + QStringLiteral(".png"))));
    }

    FakeHttpServer m_server;
    QTemporaryDir m_root;
    FakeTarget m_target;
    // Gone in reverse: the window, the core (whose Session uses the target
    // and whose caches use the transport), then the transport.
    std::unique_ptr<TorrentTransport> m_torrents;
    std::unique_ptr<AppCore> m_core;
    std::unique_ptr<QQmlApplicationEngine> m_engine;
    QQuickWindow* m_window = nullptr;
    QQuickItem* m_pages = nullptr;
    QStringList m_warnings;
};

QTEST_MAIN(TestUi)
#include "tst_ui.moc"
