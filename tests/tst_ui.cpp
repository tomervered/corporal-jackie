// The window as the app shows it, against the fake player and a fake add-on:
// every page opens, the keys do what the shortcut list says, and nothing in
// QML warns along the way (a warning is a broken binding or a missing name).
//
// Set CJ_UI_SHOTS to a folder to save a picture of each page there.

#include "AppCore.h"
#include "Browser.h"
#include "FakeHttpServer.h"
#include "FakeTarget.h"
#include "Library.h"
#include "Session.h"
#include "Storage.h"
#include "TorrentTransport.h"

#include <QDateTime>
#include <QDir>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlError>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
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
    return R"({"id":"org.example.films","version":"1.0.0","name":"Films","types":["movie"],"idPrefixes":["tt"],
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
        // No GPU needed, and pictures can be taken of the window.
        QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
        qmlRegisterType<FakeVideoSurface>("CorporalJackie.Player", 1, 0, "VideoSurface");
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
        m_server.setJson("/films/stream/movie/tt1.json",
                         R"({"streams":[{"name":"Films\n1080p","title":"first.mkv","url":"http:\/\/127.0.0.1:1\/first.mkv"}]})");

        QVERIFY(m_root.isValid());
        m_core = std::make_unique<AppCore>(m_root.filePath(QStringLiteral("data")), m_root.filePath(QStringLiteral("cache")));
        BrokerSettings s = m_core->brokerSettings();
        s.loopbackOnly = true;
        m_torrents = std::make_unique<TorrentTransport>(s);
        m_core->start(&m_target, *m_torrents, false);

        // Only the fake add-on: Cinemeta would go to the internet.
        Browser& browser = m_core->browser();
        browser.removeAddon(QStringLiteral("com.linvo.cinemeta"));
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

    void cleanup() { noWarnings(); }

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
        shot("detail");

        // Enter on the focused button asks for ways to play.
        QTRY_VERIFY(m_window->activeFocusItem());
        QTest::keyClick(m_window, Qt::Key_Return);
        QTRY_COMPARE(page(), QStringLiteral("streams"));
        QTRY_COMPARE(m_core->browser().streams().size(), 1);
        shot("streams");

        // Escape goes back to the details.
        QTest::keyClick(m_window, Qt::Key_Escape);
        QCOMPARE(page(), QStringLiteral("detail"));
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
        m_core->storage().record(e);
        QTRY_COMPARE(m_core->library().continueWatching().size(), 1);
        call("goTop", "library");
        shot("library");
        call("goTop", "home");
        shot("home-continue");
    }

    void addonsAndFolders()
    {
        call("goTop", "addons");
        shot("addons");
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
        QTest::keyClick(m_window, Qt::Key_Escape);
        QVERIFY(!m_window->property("settingsVisible").toBool());
        m_window->setProperty("helpVisible", true);
        shot("shortcuts");
        m_window->setProperty("helpVisible", false);
    }

    void playerKeys()
    {
        Session& session = m_core->session();
        session.openPath(QStringLiteral("http://127.0.0.1:1/clip.mkv"));
        m_target.startPlaying(600);
        QTRY_VERIFY(session.hasMedia());
        QTRY_VERIFY(!m_pages->isVisible());   // the video shows
        m_target.commands.clear();

        QTest::keyClick(m_window, Qt::Key_Space);
        QVERIFY(m_target.sent(QStringLiteral("pause")));
        QTest::keyClick(m_window, Qt::Key_Right);
        QVERIFY(m_target.sent(QStringLiteral("seek")));
        QTest::keyClick(m_window, Qt::Key_BracketRight);
        QCOMPARE(session.speed(), 1.25);
        QTest::keyClick(m_window, Qt::Key_Backspace);
        QCOMPARE(session.speed(), 1.0);
        QTest::keyClick(m_window, Qt::Key_M);
        QVERIFY(session.muted());
        QTest::keyClick(m_window, Qt::Key_X);
        QCOMPARE(session.subtitleDelay(), 0.1);
        QTest::keyClick(m_window, Qt::Key_I);
        QVERIFY(m_window->property("statsVisible").toBool());
        shot("player");
        QTest::keyClick(m_window, Qt::Key_I);

        // B opens the pages over the video, and again closes them.
        QTest::keyClick(m_window, Qt::Key_B);
        QVERIFY(m_pages->isVisible());
        shot("pages-over-video");
        QTest::keyClick(m_window, Qt::Key_B);
        QVERIFY(!m_pages->isVisible());
    }

private:
    QString page() const { return m_pages->property("page").toString(); }
    QVariantList rows(const char* name) const { return m_core->browser().property(name).toList(); }

    void call(const char* function, const char* arg)
    {
        QVERIFY(QMetaObject::invokeMethod(m_pages, function, Q_ARG(QVariant, QString::fromLatin1(arg))));
        QCoreApplication::processEvents();
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
