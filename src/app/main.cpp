#include "AppInfo.h"
#include "AppPaths.h"
#include "CrashHandler.h"
#include "DependencyInfo.h"
#include "Logging.h"
#include "MpvTarget.h"
#include "MpvVideoItem.h"
#include "Session.h"
#include "SleepBlocker.h"
#include "TorrentTransport.h"

#include <QCommandLineParser>
#include <QDir>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QSettings>
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

    // Torrents reach mpv through this; it must outlive mpv, so it comes first.
    cj::BrokerSettings brokerSettings;
    brokerSettings.cacheDir = QDir(paths.cache).filePath(QStringLiteral("torrents"));
    cj::TorrentTransport torrents(brokerSettings);

    // Rule 3: one mpv instance for the whole app, reused for every item.
    std::unique_ptr<cj::MpvTarget> target;
    try {
        target = std::make_unique<cj::MpvTarget>();
    } catch (const std::exception& e) {
        qCCritical(lcTargets) << "Could not start mpv:" << e.what();
        return 1;
    }

    // The UI talks only to the Session (rule 1).
    cj::Session session(target.get());

    if (torrents.registerWith(target->handle())) {
        session.addSourceResolver([&](const QString& input) { return torrents.resolve(input); });
        QObject::connect(&session, &cj::Session::sourceChanged, &torrents, [&] {
            torrents.setCurrent(session.source());
        });
        QObject::connect(&session, &cj::Session::stateChanged, &torrents, [&] {
            if (session.state() == cj::PlaybackState::Idle)
                torrents.setCurrent({});
        });
        QObject::connect(&session, &cj::Session::durationChanged, &torrents, [&] {
            torrents.setDuration(session.duration());
        });
        QObject::connect(&torrents, &cj::TorrentTransport::statusChanged, &session, &cj::Session::setStreamStatus);
        QObject::connect(&torrents, &cj::TorrentTransport::openFailed, &session, &cj::Session::reportSourceError);
    }

    QSettings settings;
    session.setHardwareDecoding(settings.value(QStringLiteral("playback/hwdec"), true).toBool());
    QObject::connect(&session, &cj::Session::hardwareDecodingChanged, &app, [&] {
        QSettings().setValue(QStringLiteral("playback/hwdec"), session.hardwareDecoding());
    });

    // Resume points are saved to the library in Step 6; for now they are logged.
    QObject::connect(&session, &cj::Session::resumePoint, &app,
                     [](const cj::PlayableSource& source, double position, double duration) {
        qCDebug(lcSession).noquote() << "Resume point" << source.url.toDisplayString() << position << "/" << duration;
    });

    cj::SleepBlocker sleepBlocker;
    QObject::connect(&session, &cj::Session::stateChanged, &app, [&] {
        sleepBlocker.setActive(session.state() == cj::PlaybackState::Playing);
    });

    cj::MpvVideoItem::useOpenGL();
    qmlRegisterType<cj::MpvVideoItem>("CorporalJackie", 1, 0, "VideoSurface");
    qmlRegisterUncreatableType<cj::Session>("CorporalJackie", 1, 0, "Session",
                                            QStringLiteral("Use the app's session"));
    qmlRegisterUncreatableMetaObject(cj::staticMetaObject, "CorporalJackie", 1, 0, "Playback",
                                     QStringLiteral("Enums only"));

    QQmlApplicationEngine engine;
    // Packaged builds keep Qt's QML modules in a "qml" folder next to the app.
    // Without this, Qt looks in its build machine's install path and fails.
    if (const QString bundled = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("qml"));
        QDir(bundled).exists())
        engine.addImportPath(bundled);
    engine.rootContext()->setContextProperty(QStringLiteral("appVersion"),
                                             QString::fromLatin1(cj::app::kVersion));
    engine.rootContext()->setContextProperty(QStringLiteral("session"), &session);
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
            if (withFile && !(session.hasMedia() && session.position() > 0.5)) {
                qCCritical(lcApp) << "Smoke test failed: file did not play (state:"
                                  << session.state() << "position:" << session.position() << ")";
                QCoreApplication::exit(2);
                return;
            }
            qCInfo(lcApp) << "Smoke test passed: window opened" << (withFile ? "and the file played" : "");
            QCoreApplication::quit();
        });
    }

    // mpv waits for its streams when it shuts down; a torrent still looking
    // for peers must not hold that up.
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &torrents, &cj::TorrentTransport::abort);

    const int code = app.exec();
    qCInfo(lcApp) << "Exiting with code" << code;
    cj::logging::shutdown();
    return code;
}
