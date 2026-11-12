#include "AppInfo.h"
#include "AppPaths.h"
#include "CrashHandler.h"
#include "DependencyInfo.h"
#include "Logging.h"

#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

class TestCommon : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setApplicationName(QString::fromLatin1(cj::app::kName));
    }

    void appIdentityIsSet()
    {
        QCOMPARE(QString::fromLatin1(cj::app::kBundleId),
                 QStringLiteral("com.tomervered.corporaljackie"));
        QVERIFY(!QString::fromLatin1(cj::app::kVersion).isEmpty());
    }

    void appPathsResolveAndCreate()
    {
        const cj::AppPaths paths = cj::AppPaths::resolve();
        QVERIFY(!paths.data.isEmpty());
        QVERIFY(!paths.cache.isEmpty());
        QVERIFY(paths.logs.startsWith(paths.data));
        QVERIFY(paths.ensureExist());
    }

    // Proves every native dependency links and loads.
    void dependenciesLoad()
    {
        const auto deps = cj::dependencyVersions();
        QCOMPARE(deps.size(), 4);
        for (const auto& dep : deps) {
            QVERIFY2(!dep.version.isEmpty(), qPrintable(dep.name));
            qInfo().noquote() << dep.name << dep.version;
        }
    }

    void logLineHasLevelAndCategory()
    {
        const QString line = cj::logging::formatLine(QtWarningMsg, "cj.broker", "no peers");
        QVERIFY(line.contains(QStringLiteral("WARN")));
        QVERIFY(line.contains(QStringLiteral("[cj.broker]")));
        QVERIFY(line.endsWith(QStringLiteral("no peers")));
    }

    void redactUrlKeepsOnlyTheServer_data()
    {
        QTest::addColumn<QString>("url");
        QTest::addColumn<QString>("expected");
        QTest::newRow("query") << "https://cdn.example.com/v.mkv?token=s3cret" << "https://cdn.example.com/…";
        QTest::newRow("userinfo") << "http://user:s3cret@example.com:8080/v.mkv" << "http://example.com:8080/…";
        QTest::newRow("path") << "https://addon.example.com/s3cret/manifest.json" << "https://addon.example.com/…";
        QTest::newRow("bare") << "https://example.com" << "https://example.com";
        QTest::newRow("torrent") << "torrent://0123456789abcdef0123456789abcdef01234567/2"
                                 << "torrent://0123456789abcdef0123456789abcdef01234567/2";
    }

    void redactUrlKeepsOnlyTheServer()
    {
        QFETCH(QString, url);
        QFETCH(QString, expected);
        QCOMPARE(cj::logging::redactUrl(QUrl(url)), expected);
    }

    void redactTextFindsLinksInMessages()
    {
        const QString text = cj::logging::redactText(
            "ffmpeg: Failed to open 'https://u:p@cdn.example.com/a/b.mkv?sig=s3cret' (404)");
        QVERIFY2(!text.contains("s3cret"), qPrintable(text));
        QVERIFY2(!text.contains("u:p"), qPrintable(text));
        QVERIFY(text.contains("cdn.example.com"));
        QVERIFY(text.endsWith("(404)"));
        QCOMPARE(cj::logging::redactUrl(QUrl::fromLocalFile("/home/someone/Movies/x.mkv")), "file:…/x.mkv");
    }

    // A link that reaches the log file by any route is cut down there too.
    void logFileNeverKeepsATokenInALink()
    {
        QTemporaryDir dir;
        QVERIFY(cj::logging::init(dir.path()));
        qCInfo(lcStorage).noquote() << "GET https://example.com/path?token=s3cretToken failed";
        const QString path = cj::logging::currentLogFile();
        cj::logging::shutdown();
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QByteArray all = file.readAll();
        QVERIFY(!all.contains("s3cretToken"));
        QVERIFY(all.contains("https://example.com/…"));
    }

    void logWritesToFile()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QVERIFY(cj::logging::init(dir.path()));
        qCInfo(lcStorage) << "hello from the test";
        const QString path = cj::logging::currentLogFile();
        cj::logging::shutdown();

        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QVERIFY(file.readAll().contains("hello from the test"));
    }

    void noCrashReportMeansEmpty()
    {
        QTemporaryDir dir;
        QVERIFY(cj::crash::takePreviousReport(dir.path()).isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestCommon)
#include "tst_common.moc"
