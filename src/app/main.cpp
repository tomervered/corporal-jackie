#include "PlayerApplication.h"
#include "KeyedListModel.h"
#include "AppInfo.h"
#include "AppPaths.h"
#include "Caches.h"
#include "CrashHandler.h"
#include "DependencyInfo.h"
#include "Logging.h"
#include "MpvPlayer.h"
#include "MpvVideoItem.h"
#include "Preferences.h"
#include "PlaybackSession.h"
#include "LibraryDatabase.h"
#include "StremioAdapter.h"
#include "TorrentTransport.h"

#include <QCollator>
#include <QCommandLineParser>
#include <QDir>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlError>
#include <QQuickWindow>
#include <QSslSocket>
#include <QStandardPaths>
#include <QStringDecoder>
#include <QTimeZone>
#include <QTimer>
#ifdef Q_OS_WIN
#  include <QtGui/qpa/qplatformwindow_p.h>
#endif
#include <clocale>
#include <exception>
#include <functional>
#include <memory>

namespace {

// The Windows and macOS packages ship a trimmed copy of ICU's data
// (packaging/trim-icu-data.py). This checks the three things Qt uses it for.
// Empty when they all work, else what broke.
QString icuProblem()
{
    QCollator german{QLocale(QLocale::German)};
    if (german.compare(QStringLiteral("\u00e4"), QStringLiteral("z")) >= 0)
        return QStringLiteral("sorting (German \u00e4 after z)");
    const QTimeZone berlin(QByteArrayLiteral("Europe/Berlin"));
    if (!berlin.isValid() || berlin.displayName(QTimeZone::StandardTime, QTimeZone::LongName, QLocale(QLocale::German)).isEmpty())
        return QStringLiteral("time zones (no name for Europe/Berlin)");
    QStringDecoder hebrew("windows-1255");
    const QString alef = hebrew.isValid() ? QString(hebrew("\xe0")) : QString();
    if (alef != QChar(0x05D0))
        return QStringLiteral("text encodings (windows-1255)");
    return {};
}

// Why the smoke test failed, as the process exit code, so a CI log that only
// shows the number still says what broke.
enum SmokeTestExitCode {
    SmokePassed = 0,
    SmokeFileDidNotPlay = 2,
    SmokeNoTls = 3,
    SmokeDatabaseDidNotOpen = 4,
    SmokeQmlWarnings = 5,
    SmokeUpscalerTurnedOff = 6,
    SmokeIcuDataMissing = 7,
};

// The name, version and icon that the window, the taskbar and the folders use.
void setAppIdentity()
{
    QGuiApplication::setApplicationName(QString::fromLatin1(cj::app::kName));
    QGuiApplication::setApplicationDisplayName(QString::fromLatin1(cj::app::kName));
    QGuiApplication::setApplicationVersion(QString::fromLatin1(cj::app::kVersion));
    // The taskbar, title bar and Alt+Tab show this: the side bar's badge without its dark outline
    // (that outline vanishes on a dark taskbar and made the icon look smaller than others).
    QGuiApplication::setWindowIcon(QIcon(QStringLiteral(":/CorporalJackie/images/app-icon.png")));
    // No organization name on purpose: it would add a second "Corporal Jackie"
    // folder level to every path (see AppPaths.h).
    QGuiApplication::setOrganizationDomain(QString::fromLatin1(cj::app::kOrgDomain));
}

// The first lines of every log: what is running, where its files are, and
// whether the last run crashed.
void logStartup(const cj::AppPaths& paths)
{
    qCInfo(lcApp).noquote() << cj::app::kName << cj::app::kVersion << "starting";
    qCInfo(lcApp).noquote() << "data:" << paths.data << "cache:" << paths.cache;
    for (const auto& dep : cj::dependencyVersions())
        qCInfo(lcApp).noquote() << dep.name << dep.version;

    if (const QString crash = cj::crash::takePreviousReport(paths.logs); !crash.isEmpty())
        qCWarning(lcApp).noquote() << "Previous run crashed:" << crash;
}

// Runs `job` once, when the user answers the first-run privacy question.
// Used for anything that connects to the internet before that answer exists.
void runAfterPrivacyAnswer(cj::PlayerApplication& core, std::function<void()> job)
{
    auto* once = new QObject(&core);
    QObject::connect(&core.preferences(), &cj::Preferences::privacyChanged, once, [once, job = std::move(job)] {
        job();
        once->deleteLater();
    });
}

// The add-ons' manifests are fetched again after the first-run answer,
// never before it.
void refreshAddonsWhenAllowed(cj::PlayerApplication& core)
{
    if (core.preferences().privacyChosen())
        QTimer::singleShot(3000, &core, [&core] { core.addons().refreshManifests(); });
    else
        runAfterPrivacyAnswer(core, [&core] { core.addons().refreshManifests(); });
}

// The C++ classes that the QML screens create or refer to by name.
void registerQmlTypes()
{
    // The C++ types have a module of their own: sharing the QML files' URI
    // made Qt 6.12 call "CorporalJackie" ambiguous in the packaged app.
    qmlRegisterType<cj::MpvVideoItem>("CorporalJackie.Player", 1, 0, "VideoSurface");
    qmlRegisterType<cj::KeyedListModel>("CorporalJackie.Player", 1, 0, "KeyedList");
    qmlRegisterUncreatableType<cj::PlaybackSession>("CorporalJackie.Player", 1, 0, "Session",
                                                    QStringLiteral("Use the app's session"));
    qmlRegisterUncreatableMetaObject(cj::staticMetaObject, "CorporalJackie.Player", 1, 0, "Playback",
                                     QStringLiteral("Enums only"));
}

// Loads Main.qml into `engine`. Every QML warning is added to `qmlWarnings`,
// which the smoke test reads. Returns false if the window could not be made.
bool loadMainWindow(QQmlApplicationEngine& engine, cj::PlayerApplication& core, QStringList& qmlWarnings)
{
    // Posters and other images are kept on disk, up to a limit.
    engine.setNetworkAccessManagerFactory(core.caches().factory());
    // Packaged builds keep Qt's QML modules in a "qml" folder next to the app.
    // Without this, Qt looks in its build machine's install path and fails.
    const QString bundled = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("qml"));
    if (QDir(bundled).exists())
        engine.addImportPath(bundled);
    core.exposeTo(engine.rootContext());

    // A QML warning is a bug (a broken binding, a missing name): the smoke
    // test fails on any. A poster that fails to download or decode is the
    // network's doing, not the code's, so it does not count.
    QObject::connect(&engine, &QQmlEngine::warnings, &engine, [&qmlWarnings](const QList<QQmlError>& errors) {
        for (const QQmlError& e : errors)
            if (!e.description().contains(QLatin1String("QQuickImage")))
                qmlWarnings << e.toString();
    });
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &engine,
                     [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    engine.load(QUrl(QStringLiteral("qrc:/CorporalJackie/Main.qml")));
    if (engine.rootObjects().isEmpty()) {
        qCCritical(lcUi) << "Main window failed to load";
        return false;
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
    return true;
}

// Opens a link or file named on the command line. It waits for the first-run
// answer, because opening a torrent connects to other people's computers.
// The smoke test does not wait.
void openFileFromCommandLine(cj::PlayerApplication& core, const QString& file, bool smokeTest)
{
    cj::PlaybackSession& session = core.session();
    if (core.preferences().privacyChosen() || smokeTest)
        session.openPath(file);
    else
        runAfterPrivacyAnswer(core, [&session, file] { session.openPath(file); });
}

// CJ_SMOKE_UPSCALER=fsr: play with that upscaler on, and fail if the
// graphics driver could not run it (the player turns it off then).
// One this computer does not have (MetalFX on a CI Mac without a GPU for it,
// say) is skipped. Returns the upscaler to check, or "" for none.
QString turnOnSmokeTestUpscaler(cj::PlaybackSession& session)
{
    const QString upscaler = qEnvironmentVariable("CJ_SMOKE_UPSCALER");
    if (upscaler.isEmpty())
        return {};
    if (!session.upscalers().contains(upscaler)) {
        qCInfo(lcApp).noquote() << "Smoke test: upscaler" << upscaler << "is not available here; not tested";
        return {};
    }
    session.setUpscaler(upscaler);
    return upscaler;
}

// The smoke test's checks, run once the window has had time to open (and the
// file, if one was given, time to start). Returns SmokePassed or the first
// failure, after logging it.
SmokeTestExitCode checkSmokeTest(cj::PlayerApplication& core, const QStringList& qmlWarnings,
                                 bool withFile, const QString& upscaler)
{
    cj::PlaybackSession& session = core.session();
    // Add-ons are reached over HTTPS: a package without Qt's TLS plugin
    // would install nothing.
    if (!QSslSocket::supportsSsl()) {
        qCCritical(lcApp) << "Smoke test failed: no TLS backend (available:"
                          << QSslSocket::availableBackends() << ")";
        return SmokeNoTls;
    }
    // CJ_SMOKE_ICU=1 on the packaged Windows and macOS builds, whose ICU
    // data is trimmed. Linux builds may use Qt without ICU.
    if (qEnvironmentVariableIntValue("CJ_SMOKE_ICU") == 1) {
        if (const QString problem = icuProblem(); !problem.isEmpty()) {
            qCCritical(lcApp).noquote() << "Smoke test failed: ICU data is missing something Qt needs:" << problem;
            return SmokeIcuDataMissing;
        }
    }
    if (!core.storage().isOpen()) {
        qCCritical(lcApp).noquote() << "Smoke test failed: the database did not open:" << core.storage().error();
        return SmokeDatabaseDidNotOpen;
    }
    if (!qmlWarnings.isEmpty()) {
        qCCritical(lcApp).noquote() << "Smoke test failed: QML warnings:\n" << qmlWarnings.join(QLatin1Char('\n'));
        return SmokeQmlWarnings;
    }
    if (withFile && !(session.hasMedia() && session.position() > 0.5)) {
        qCCritical(lcApp) << "Smoke test failed: file did not play (state:"
                          << session.state() << "position:" << session.position() << ")";
        return SmokeFileDidNotPlay;
    }
    if (!upscaler.isEmpty() && session.upscaler() != upscaler) {
        qCCritical(lcApp).noquote() << "Smoke test failed: upscaler" << upscaler << "is" << session.upscaler();
        return SmokeUpscalerTurnedOff;
    }
    qCInfo(lcApp) << "Smoke test passed: window opened" << (withFile ? "and the file played" : "")
                  << "TLS:" << QSslSocket::activeBackend();
    return SmokePassed;
}

// --smoke-test: CI opens the window (and plays a file, if given), checks
// that everything came up, and the app quits with a SmokeTestExitCode.
void startSmokeTest(cj::PlayerApplication& core, const QStringList& qmlWarnings, bool withFile)
{
    const QString upscaler = turnOnSmokeTestUpscaler(core.session());
    const int waitMs = withFile ? 4000 : 1000;
    QTimer::singleShot(waitMs, &core, [&core, &qmlWarnings, withFile, upscaler] {
        const SmokeTestExitCode result = checkSmokeTest(core, qmlWarnings, withFile, upscaler);
        if (result == SmokePassed)
            QCoreApplication::quit();  // closes the window the normal way
        else
            QCoreApplication::exit(result);
    });
}

} // namespace

int main(int argc, char* argv[])
{
    QGuiApplication app(argc, argv);
    setAppIdentity();

    QCommandLineParser args;
    args.setApplicationDescription(QStringLiteral("A native media player that streams torrents."));
    args.addHelpOption();
    args.addVersionOption();
    const QCommandLineOption smokeTestOption(
        QStringLiteral("smoke-test"),
        QStringLiteral("Open the window (and play the given file), then quit. Used by CI."));
    args.addOption(smokeTestOption);
    args.addPositionalArgument(QStringLiteral("file"), QStringLiteral("A video file to open."));
    args.process(app);
    const bool smokeTest = args.isSet(smokeTestOption);
    const QStringList filesToOpen = args.positionalArguments();
    if (smokeTest)
        QStandardPaths::setTestModeEnabled(true);
    cj::PlayerApplication::useLeftToRight();

    // mpv parses numbers with the C locale; Qt may have changed it.
    std::setlocale(LC_NUMERIC, "C");

    const cj::AppPaths paths = cj::AppPaths::resolve();
    if (!paths.ensureExist() || !cj::logging::init(paths.logs))
        qCWarning(lcApp) << "Could not create app folders; logging to stderr only";
    cj::crash::install(paths.logs);
    logStartup(paths);

    // Declared in the order they must outlive each other: the transport
    // outlives mpv (mpv reads its streams until it closes), and both outlive
    // the core, whose PlaybackSession drives mpv and whose cache worker asks the
    // transport. They are made after the core, which holds their settings.
    std::unique_ptr<cj::TorrentTransport> transport;
    std::unique_ptr<cj::MpvPlayer> player;

    // The database, preferences and everything that is not the window.
    cj::PlayerApplication core(paths.data, paths.cache);

    // Torrents reach mpv through this. Its storage settings are the
    // preferences as they were at start; changes in Settings apply the next
    // time the app opens.
    transport = std::make_unique<cj::TorrentTransport>(core.brokerSettings());

    // Rule 3: one mpv instance for the whole app, reused for every item.
    try {
        player = std::make_unique<cj::MpvPlayer>();
    } catch (const std::exception& e) {
        qCCritical(lcTargets) << "Could not start mpv:" << e.what();
        return 1;
    }
    core.start(player.get(), *transport, transport->registerWith(player->handle()));
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &core, &cj::PlayerApplication::shutDown);
    if (!smokeTest)
        refreshAddonsWhenAllowed(core);

    cj::MpvVideoItem::useOpenGL();
    registerQmlTypes();
    QStringList qmlWarnings;  // declared first: the engine writes to it until it is gone
    QQmlApplicationEngine engine;
    if (!loadMainWindow(engine, core, qmlWarnings))
        return 1;

    if (!filesToOpen.isEmpty())
        openFileFromCommandLine(core, filesToOpen.first(), smokeTest);
    if (smokeTest)
        startSmokeTest(core, qmlWarnings, !filesToOpen.isEmpty());

    const int code = app.exec();
    qCInfo(lcApp) << "Exiting with code" << code;
    cj::logging::shutdown();
    return code;
}
