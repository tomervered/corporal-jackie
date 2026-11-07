#include "AppInfo.h"
#include "AppPaths.h"
#include "CrashHandler.h"
#include "DependencyInfo.h"
#include "Logging.h"
#include "MpvPlayer.h"
#include "MpvVideoItem.h"
#include "SleepBlocker.h"

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

    // Rule 3: one mpv instance for the whole app, reused for every item.
    std::unique_ptr<cj::MpvPlayer> player;
    try {
        player = std::make_unique<cj::MpvPlayer>();
    } catch (const std::exception& e) {
        qCCritical(lcTargets) << "Could not start mpv:" << e.what();
        return 1;
    }

    QSettings settings;
    player->setHwdecEnabled(settings.value(QStringLiteral("playback/hwdec"), true).toBool());
    QObject::connect(player.get(), &cj::MpvPlayer::hwdecEnabledChanged, &app, [&] {
        QSettings().setValue(QStringLiteral("playback/hwdec"), player->hwdecEnabled());
    });

    cj::SleepBlocker sleepBlocker;
    const auto updateSleep = [&] { sleepBlocker.setActive(player->fileLoaded() && !player->paused()); };
    QObject::connect(player.get(), &cj::MpvPlayer::pausedChanged, &app, updateSleep);
    QObject::connect(player.get(), &cj::MpvPlayer::fileLoadedChanged, &app, updateSleep);

    cj::MpvVideoItem::useOpenGL();
    qmlRegisterType<cj::MpvVideoItem>("CorporalJackie.Video", 1, 0, "MpvVideo");
    qmlRegisterUncreatableType<cj::MpvPlayer>("CorporalJackie.Video", 1, 0, "MpvPlayer",
                                              QStringLiteral("Use the app's player"));

    QQmlApplicationEngine engine;
    // Packaged builds keep Qt's QML modules in a "qml" folder next to the app.
    // Without this, Qt looks in its build machine's install path and fails.
    if (const QString bundled = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("qml"));
        QDir(bundled).exists())
        engine.addImportPath(bundled);
    engine.rootContext()->setContextProperty(QStringLiteral("appVersion"),
                                             QString::fromLatin1(cj::app::kVersion));
    engine.rootContext()->setContextProperty(QStringLiteral("player"), player.get());
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
        player->openPath(files.first());

    if (args.isSet(smokeTest)) {
        // With a file, the smoke test also checks that it loads and plays.
        const bool withFile = !args.positionalArguments().isEmpty();
        QTimer::singleShot(withFile ? 4000 : 1000, &app, [&] {
            if (withFile && !(player->fileLoaded() && player->position() > 0.5)) {
                qCCritical(lcApp) << "Smoke test failed: file did not play (loaded:"
                                  << player->fileLoaded() << "position:" << player->position() << ")";
                QCoreApplication::exit(2);
                return;
            }
            qCInfo(lcApp) << "Smoke test passed: window opened" << (withFile ? "and the file played" : "");
            QCoreApplication::quit();
        });
    }

    const int code = app.exec();
    qCInfo(lcApp) << "Exiting with code" << code;
    cj::logging::shutdown();
    return code;
}
