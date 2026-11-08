#include "AppCore.h"
#include "AppInfo.h"
#include "AppPaths.h"
#include "Caches.h"
#include "CrashHandler.h"
#include "DependencyInfo.h"
#include "Logging.h"
#include "MpvTarget.h"
#include "MpvVideoItem.h"
#include "Session.h"
#include "Storage.h"
#include "StremioAdapter.h"
#include "TorrentTransport.h"

#include <QCommandLineParser>
#include <QDir>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlError>
#include <QQuickWindow>
#include <QSslSocket>
#include <QTimer>
#ifdef Q_OS_WIN
#  include <QtGui/qpa/qplatformwindow_p.h>
#endif
#include <clocale>
#include <exception>
#include <memory>

int main(int argc, char* argv[])
{
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(QString::fromLatin1(cj::app::kName));
    QGuiApplication::setApplicationDisplayName(QString::fromLatin1(cj::app::kName));
    QGuiApplication::setApplicationVersion(QString::fromLatin1(cj::app::kVersion));
    // No organization name on purpose: it would add a second "Corporal Jackie"
    // folder level to every path (see AppPaths.h).
    QGuiApplication::setOrganizationDomain(QString::fromLatin1(cj::app::kOrgDomain));

    QCommandLineParser args;
    args.setApplicationDescription(QStringLiteral("A native media player that streams torrents."));
    args.addHelpOption();
    args.addVersionOption();
    const QCommandLineOption smokeTest(
        QStringLiteral("smoke-test"),
        QStringLiteral("Open the window (and play the given file), then quit. Used by CI."));
    args.addOption(smokeTest);
    args.addPositionalArgument(QStringLiteral("file"), QStringLiteral("A video file to open."));
    args.process(app);

    // mpv parses numbers with the C locale; Qt may have changed it.
    std::setlocale(LC_NUMERIC, "C");

    const cj::AppPaths paths = cj::AppPaths::resolve();
    if (!paths.ensureExist() || !cj::logging::init(paths.logs))
        qCWarning(lcApp) << "Could not create app folders; logging to stderr only";
    cj::crash::install(paths.logs);

    qCInfo(lcApp).noquote() << cj::app::kName << cj::app::kVersion << "starting";
    qCInfo(lcApp).noquote() << "data:" << paths.data << "cache:" << paths.cache;
    for (const auto& dep : cj::dependencyVersions())
        qCInfo(lcApp).noquote() << dep.name << dep.version;

    if (const QString crash = cj::crash::takePreviousReport(paths.logs); !crash.isEmpty())
        qCWarning(lcApp).noquote() << "Previous run crashed:" << crash;

    // The database, preferences and everything that is not the window.
    cj::AppCore core(paths.data, paths.cache);

    // Torrents reach mpv through this; it must outlive mpv, so it comes first.
    // Its storage settings are the preferences as they were at start; changes
    // in Settings apply the next time the app opens.
    cj::TorrentTransport torrents(core.brokerSettings());

    // Rule 3: one mpv instance for the whole app, reused for every item.
    std::unique_ptr<cj::MpvTarget> target;
    try {
        target = std::make_unique<cj::MpvTarget>();
    } catch (const std::exception& e) {
        qCCritical(lcTargets) << "Could not start mpv:" << e.what();
        return 1;
    }
    core.start(target.get(), torrents, torrents.registerWith(target->handle()));
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &core, &cj::AppCore::shutDown);
    cj::Session& session = core.session();
    if (!args.isSet(smokeTest))
        QTimer::singleShot(3000, &core, [&core] { core.addons().refreshManifests(); });

    cj::MpvVideoItem::useOpenGL();
    qmlRegisterType<cj::MpvVideoItem>("CorporalJackie", 1, 0, "VideoSurface");
    qmlRegisterUncreatableType<cj::Session>("CorporalJackie", 1, 0, "Session",
                                            QStringLiteral("Use the app's session"));
    qmlRegisterUncreatableMetaObject(cj::staticMetaObject, "CorporalJackie", 1, 0, "Playback",
                                     QStringLiteral("Enums only"));

    QQmlApplicationEngine engine;
    // Posters and other images are kept on disk, up to a limit.
    engine.setNetworkAccessManagerFactory(core.caches().factory());
    // Packaged builds keep Qt's QML modules in a "qml" folder next to the app.
    // Without this, Qt looks in its build machine's install path and fails.
    if (const QString bundled = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("qml"));
        QDir(bundled).exists())
        engine.addImportPath(bundled);
    core.exposeTo(engine.rootContext());
    // A QML warning is a bug (a broken binding, a missing name): the smoke
    // test fails on any. A poster that fails to download or decode is the
    // network's doing, not the code's, so it does not count.
    QStringList qmlWarnings;
    QObject::connect(&engine, &QQmlEngine::warnings, &app, [&qmlWarnings](const QList<QQmlError>& errors) {
        for (const QQmlError& e : errors)
            if (!e.description().contains(QLatin1String("QQuickImage")))
                qmlWarnings << e.toString();
    });
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app,
                     [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    engine.load(QUrl(QStringLiteral("qrc:/CorporalJackie/Main.qml")));
    if (engine.rootObjects().isEmpty()) {
        qCCritical(lcUi) << "Main window failed to load";
        return 1;
    }

#ifdef Q_OS_WIN
    // A borderless full-screen OpenGL window makes Windows switch it to
    // exclusive full screen, which makes the video flash and stall. Keeping a
    // 1-pixel border in full screen is Qt's documented workaround.
    if (auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first())) {
        if (auto* native = window->nativeInterface<QNativeInterface::Private::QWindowsWindow>())
            native->setHasBorderInFullScreen(true);
    }
#endif

    if (const QStringList files = args.positionalArguments(); !files.isEmpty())
        session.openPath(files.first());

    if (args.isSet(smokeTest)) {
        // With a file, the smoke test also checks that it loads and plays.
        // withFile by value: this block ends long before the timer fires.
        const bool withFile = !args.positionalArguments().isEmpty();
        QTimer::singleShot(withFile ? 4000 : 1000, &app, [&, withFile] {
            // Add-ons are reached over HTTPS: a package without Qt's TLS
            // plugin would install nothing.
            if (!QSslSocket::supportsSsl()) {
                qCCritical(lcApp) << "Smoke test failed: no TLS backend (available:"
                                  << QSslSocket::availableBackends() << ")";
                QCoreApplication::exit(3);
                return;
            }
            if (!core.storage().isOpen()) {
                qCCritical(lcApp).noquote() << "Smoke test failed: the database did not open:" << core.storage().error();
                QCoreApplication::exit(4);
                return;
            }
            if (!qmlWarnings.isEmpty()) {
                qCCritical(lcApp).noquote() << "Smoke test failed: QML warnings:\n" << qmlWarnings.join(QLatin1Char('\n'));
                QCoreApplication::exit(5);
                return;
            }
            if (withFile && !(session.hasMedia() && session.position() > 0.5)) {
                qCCritical(lcApp) << "Smoke test failed: file did not play (state:"
                                  << session.state() << "position:" << session.position() << ")";
                QCoreApplication::exit(2);
                return;
            }
            qCInfo(lcApp) << "Smoke test passed: window opened" << (withFile ? "and the file played" : "")
                          << "TLS:" << QSslSocket::activeBackend();
            QCoreApplication::quit();
        });
    }

    const int code = app.exec();
    qCInfo(lcApp) << "Exiting with code" << code;
    cj::logging::shutdown();
    return code;
}
