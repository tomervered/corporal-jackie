#include "AppInfo.h"
#include "AppPaths.h"
#include "Browser.h"
#include "Caches.h"
#include "CrashHandler.h"
#include "DependencyInfo.h"
#include "Library.h"
#include "LocalLibrary.h"
#include "Logging.h"
#include "MpvTarget.h"
#include "MpvVideoItem.h"
#include "Preferences.h"
#include "Session.h"
#include "SleepBlocker.h"
#include "Storage.h"
#include "StremioAdapter.h"
#include "TorrentTransport.h"

#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QSettings>
#include <QSslSocket>
#include <QTimer>
#ifdef Q_OS_WIN
#  include <QtGui/qpa/qplatformwindow_p.h>
#endif
#include <clocale>
#include <exception>
#include <memory>

namespace {

// Before the database, settings were in QSettings and the add-ons and
// folders in two JSON files. Copied over once; the old ones stay as they were.
void importOldSettings(cj::Storage& storage, const QString& dataDir)
{
    const QString done = QStringLiteral("app/importedOldSettings");
    if (!storage.isOpen() || storage.setting(done).toBool())
        return;
    QSettings old;
    int copied = 0;
    const auto copy = [&](const QString& key, QMetaType::Type type) {
        if (old.contains(key) && !storage.setting(key).isValid()) {
            QVariant v = old.value(key);
            v.convert(QMetaType(type));
            storage.setSetting(key, v);
            ++copied;
        }
    };
    copy(QStringLiteral("playback/hwdec"), QMetaType::Bool);
    copy(QStringLiteral("torrents/inMemory"), QMetaType::Bool);
    copy(QStringLiteral("torrents/memoryCapMB"), QMetaType::Int);
    copy(QStringLiteral("torrents/diskCacheGB"), QMetaType::Int);
    copy(QStringLiteral("torrents/cacheFolder"), QMetaType::QString);

    const auto readJson = [&](const QString& name) {
        QFile file(QDir(dataDir).filePath(name));
        return file.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(file.read(16 * 1024 * 1024)).object()
                                              : QJsonObject();
    };
    if (!storage.addons()) {
        const QJsonObject json = readJson(QStringLiteral("addons.json"));
        if (json.contains(QLatin1String("addons"))) {
            storage.saveAddons(json.value("addons").toArray());
            ++copied;
        }
    }
    if (!storage.folders()) {
        const QJsonObject json = readJson(QStringLiteral("local-folders.json"));
        if (json.contains(QLatin1String("folders"))) {
            QStringList folders;
            for (const QJsonValue& v : json.value("folders").toArray())
                folders << v.toString();
            storage.saveFolders(folders);
            ++copied;
        }
    }
    storage.setSetting(done, true);
    if (copied > 0)
        qCInfo(lcApp) << "Copied" << copied << "old settings and lists into the database";
}

} // namespace

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
    // Torrent storage comes from the preferences as they were at start;
    // changes in Settings apply the next time the app opens.
    // Step 6: settings, add-ons, folders, library and history, in one file.
    cj::Storage storage(QDir(paths.data).filePath(QStringLiteral("corporal-jackie.sqlite")));
    importOldSettings(storage, paths.data);

    cj::Preferences preferences(storage, QDir(paths.cache).filePath(QStringLiteral("torrents")));
    cj::BrokerSettings brokerSettings;
    brokerSettings.cacheDir = preferences.startCacheFolder();
    brokerSettings.cacheCapBytes = preferences.startDiskCacheBytes();
    brokerSettings.memoryOnly = preferences.startInMemory();
    brokerSettings.memoryCapBytes = preferences.startMemoryCapBytes();
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

    session.setHardwareDecoding(storage.setting(QStringLiteral("playback/hwdec"), true).toBool());
    QObject::connect(&session, &cj::Session::hardwareDecodingChanged, &app, [&] {
        storage.setSetting(QStringLiteral("playback/hwdec"), session.hardwareDecoding());
    });

    // Subtitle size and look, kept between runs.
    {
        const QVariantMap stored = storage.setting(QStringLiteral("subtitles/style")).toMap();
        cj::SubtitleStyle style;
        style.font = stored.value(QStringLiteral("font"), style.font).toString();
        style.color = stored.value(QStringLiteral("color"), style.color).toString();
        style.outline = stored.value(QStringLiteral("outline"), style.outline).toDouble();
        style.background = stored.value(QStringLiteral("background"), style.background).toInt();
        style.restyleAll = stored.value(QStringLiteral("restyleAll"), style.restyleAll).toBool();
        session.setSubtitleStyle(style);
        session.setSubtitleScale(storage.setting(QStringLiteral("subtitles/scale"), 1.0).toDouble());
    }
    QObject::connect(&session, &cj::Session::subtitleStyleChanged, &app, [&] {
        const cj::SubtitleStyle style = session.subtitleStyle();
        storage.setSetting(QStringLiteral("subtitles/style"),
                           QVariantMap{{QStringLiteral("font"), style.font}, {QStringLiteral("color"), style.color},
                                       {QStringLiteral("outline"), style.outline},
                                       {QStringLiteral("background"), style.background},
                                       {QStringLiteral("restyleAll"), style.restyleAll}});
    });
    QObject::connect(&session, &cj::Session::subtitleScaleChanged, &app, [&] {
        storage.setSetting(QStringLiteral("subtitles/scale"), session.subtitleScale());
    });

    cj::SleepBlocker sleepBlocker;
    QObject::connect(&session, &cj::Session::stateChanged, &app, [&] {
        sleepBlocker.setActive(session.state() == cj::PlaybackState::Playing);
    });

    // Step 5: where things to watch come from. Each source adapter only
    // produces PlayableSources; the browse panel hands them to the Session.
    // Their lists are kept in the database; a fresh install starts with
    // Cinemeta.
    cj::StremioAdapter addons;
    if (const auto stored = storage.addons())
        addons.restore(*stored);
    else
        addons.installDefaults();
    storage.saveAddons(addons.storedList());
    QObject::connect(&addons, &cj::StremioAdapter::addonsChanged, &storage,
                     [&] { storage.saveAddons(addons.storedList()); });
    cj::LocalLibrary localFiles;
    if (const auto stored = storage.folders(); stored && !stored->isEmpty())
        localFiles.restoreFolders(*stored);
    QObject::connect(&localFiles, &cj::LocalLibrary::foldersChanged, &storage,
                     [&] { storage.saveFolders(localFiles.folders()); });
    // An import may bring add-ons and folders.
    QObject::connect(&storage, &cj::Storage::addonsImported, &addons, [&] { addons.restore(*storage.addons()); });
    QObject::connect(&storage, &cj::Storage::foldersImported, &localFiles,
                     [&] { localFiles.restoreFolders(*storage.folders()); });

    cj::Browser browser(&session);
    browser.setStremio(&addons);
    browser.setLocal(&localFiles);

    // Watch history, Continue Watching and saved items.
    cj::Library library(storage, session, browser);
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &library, &cj::Library::saveNow);
    cj::Caches caches(storage, torrents, QDir(paths.cache).filePath(QStringLiteral("images")));
    // A YouTube adapter would be added here with browser.addAdapter().
    if (!args.isSet(smokeTest))
        QTimer::singleShot(3000, &addons, [&] { addons.refreshManifests(); });

    cj::MpvVideoItem::useOpenGL();
    qmlRegisterType<cj::MpvVideoItem>("CorporalJackie", 1, 0, "VideoSurface");
    qmlRegisterUncreatableType<cj::Session>("CorporalJackie", 1, 0, "Session",
                                            QStringLiteral("Use the app's session"));
    qmlRegisterUncreatableMetaObject(cj::staticMetaObject, "CorporalJackie", 1, 0, "Playback",
                                     QStringLiteral("Enums only"));

    QQmlApplicationEngine engine;
    // Posters and other images are kept on disk, up to a limit.
    engine.setNetworkAccessManagerFactory(caches.factory());
    // Packaged builds keep Qt's QML modules in a "qml" folder next to the app.
    // Without this, Qt looks in its build machine's install path and fails.
    if (const QString bundled = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("qml"));
        QDir(bundled).exists())
        engine.addImportPath(bundled);
    engine.rootContext()->setContextProperty(QStringLiteral("appVersion"),
                                             QString::fromLatin1(cj::app::kVersion));
    engine.rootContext()->setContextProperty(QStringLiteral("session"), &session);
    engine.rootContext()->setContextProperty(QStringLiteral("browser"), &browser);
    engine.rootContext()->setContextProperty(QStringLiteral("preferences"), &preferences);
    engine.rootContext()->setContextProperty(QStringLiteral("library"), &library);
    engine.rootContext()->setContextProperty(QStringLiteral("caches"), &caches);
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
            if (!storage.isOpen()) {
                qCCritical(lcApp).noquote() << "Smoke test failed: the database did not open:" << storage.error();
                QCoreApplication::exit(4);
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

    // mpv waits for its streams when it shuts down; a torrent still looking
    // for peers must not hold that up.
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &torrents, &cj::TorrentTransport::abort);

    const int code = app.exec();
    qCInfo(lcApp) << "Exiting with code" << code;
    cj::logging::shutdown();
    return code;
}
