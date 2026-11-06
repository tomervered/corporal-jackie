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
