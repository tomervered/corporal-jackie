#include "AppCore.h"

#include "AppInfo.h"
#include "Browser.h"
#include "Caches.h"
#include "Library.h"
#include "LocalLibrary.h"
#include "Logging.h"
#include "Preferences.h"
#include "Session.h"
#include "SleepBlocker.h"
#include "Storage.h"
#include "StremioAdapter.h"
#include "TorrentTransport.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlContext>
#include <QSettings>

namespace cj {

namespace {
const QString kHwdec = QStringLiteral("playback/hwdec");
const QString kSubtitleStyle = QStringLiteral("subtitles/style");
const QString kSubtitleScale = QStringLiteral("subtitles/scale");

QVariantMap styleToMap(const SubtitleStyle& s)
{
    return {{QStringLiteral("font"), s.font}, {QStringLiteral("color"), s.color},
            {QStringLiteral("outline"), s.outline}, {QStringLiteral("background"), s.background},
            {QStringLiteral("restyleAll"), s.restyleAll}};
}

SubtitleStyle styleFromMap(const QVariantMap& m)
{
    SubtitleStyle s;
    s.font = m.value(QStringLiteral("font"), s.font).toString();
    s.color = m.value(QStringLiteral("color"), s.color).toString();
    s.outline = m.value(QStringLiteral("outline"), s.outline).toDouble();
    s.background = m.value(QStringLiteral("background"), s.background).toInt();
    s.restyleAll = m.value(QStringLiteral("restyleAll"), s.restyleAll).toBool();
    return s;
}
} // namespace

AppCore::AppCore(const QString& dataDir, const QString& cacheDir, QObject* parent)
    : QObject(parent)
    , m_dataDir(dataDir)
    , m_cacheDir(cacheDir)
    , m_storage(std::make_unique<Storage>(QDir(dataDir).filePath(QStringLiteral("corporal-jackie.sqlite"))))
{
    importOldSettings();
    m_preferences = std::make_unique<Preferences>(*m_storage, QDir(cacheDir).filePath(QStringLiteral("torrents")));
}

AppCore::~AppCore() = default;

BrokerSettings AppCore::brokerSettings() const
{
    BrokerSettings s;
    s.cacheDir = m_preferences->startCacheFolder();
    s.cacheCapBytes = m_preferences->startDiskCacheBytes();
    s.memoryOnly = m_preferences->startInMemory();
    s.memoryCapBytes = m_preferences->startMemoryCapBytes();
    return s;
}

void AppCore::start(PlaybackTarget* target, TorrentTransport& torrents, bool registerTorrents)
{
    m_torrents = &torrents;
    // The UI talks only to the Session (rule 1).
    m_session = std::make_unique<Session>(target);
    Session* session = m_session.get();

    if (registerTorrents) {
        session->addSourceResolver([&torrents](const QString& input) { return torrents.resolve(input); });
        connect(session, &Session::sourceChanged, &torrents, [session, &torrents] {
            torrents.setCurrent(session->source());
        });
        connect(session, &Session::stateChanged, &torrents, [session, &torrents] {
            if (session->state() == PlaybackState::Idle)
                torrents.setCurrent({});
        });
        connect(session, &Session::durationChanged, &torrents, [session, &torrents] {
            torrents.setDuration(session->duration());
        });
        connect(&torrents, &TorrentTransport::statusChanged, session, &Session::setStreamStatus);
        connect(&torrents, &TorrentTransport::openFailed, session, &Session::reportSourceError);
    }

    restorePlaybackSettings();

    m_sleepBlocker = std::make_unique<SleepBlocker>();
    connect(session, &Session::stateChanged, this, [this] {
        m_sleepBlocker->setActive(m_session->state() == PlaybackState::Playing);
    });

    restoreSources();
    m_browser = std::make_unique<Browser>(session);
    m_browser->setStremio(m_addons.get());
    m_browser->setLocal(m_localFiles.get());
    // A YouTube adapter would be added here with m_browser->addAdapter().

    m_library = std::make_unique<Library>(*m_storage, *session, *m_browser);
    m_caches = std::make_unique<Caches>(*m_storage, torrents, QDir(m_cacheDir).filePath(QStringLiteral("images")));
}

void AppCore::exposeTo(QQmlContext* context) const
{
    context->setContextProperty(QStringLiteral("appVersion"), QString::fromLatin1(app::kVersion));
    context->setContextProperty(QStringLiteral("session"), m_session.get());
    context->setContextProperty(QStringLiteral("browser"), m_browser.get());
    context->setContextProperty(QStringLiteral("preferences"), m_preferences.get());
    context->setContextProperty(QStringLiteral("library"), m_library.get());
    context->setContextProperty(QStringLiteral("caches"), m_caches.get());
}

void AppCore::shutDown()
{
    if (m_library)
        m_library->saveNow();
    if (m_caches)
        m_caches->stop();
    // mpv waits for its streams when it shuts down; a torrent still looking
    // for peers must not hold that up.
    if (m_torrents)
        m_torrents->abort();
}

// Hardware decoding and the subtitle look, kept between runs.
void AppCore::restorePlaybackSettings()
{
    Session* session = m_session.get();
    Storage* storage = m_storage.get();
    session->setHardwareDecoding(storage->setting(kHwdec, true).toBool());
    session->setSubtitleStyle(styleFromMap(storage->setting(kSubtitleStyle).toMap()));
    session->setSubtitleScale(storage->setting(kSubtitleScale, 1.0).toDouble());

    connect(session, &Session::hardwareDecodingChanged, this,
            [session, storage] { storage->setSetting(kHwdec, session->hardwareDecoding()); });
    connect(session, &Session::subtitleStyleChanged, this,
            [session, storage] { storage->setSetting(kSubtitleStyle, styleToMap(session->subtitleStyle())); });
    connect(session, &Session::subtitleScaleChanged, this,
            [session, storage] { storage->setSetting(kSubtitleScale, session->subtitleScale()); });
}

// The add-on and folder lists live in the database; a fresh install starts
// with Cinemeta. Changes are saved as they happen, and an import reloads them.
void AppCore::restoreSources()
{
    Storage* storage = m_storage.get();
    m_addons = std::make_unique<StremioAdapter>();
    StremioAdapter* addons = m_addons.get();
    if (const auto stored = storage->addons())
        addons->restore(*stored);
    else
        addons->installDefaults();
    storage->saveAddons(addons->storedList());
    connect(addons, &StremioAdapter::addonsChanged, storage, [storage, addons] { storage->saveAddons(addons->storedList()); });

    m_localFiles = std::make_unique<LocalLibrary>();
    LocalLibrary* local = m_localFiles.get();
    if (const auto stored = storage->folders(); stored && !stored->isEmpty())
        local->restoreFolders(*stored);
    connect(local, &LocalLibrary::foldersChanged, storage, [storage, local] { storage->saveFolders(local->folders()); });

    connect(storage, &Storage::addonsImported, addons, [storage, addons] { addons->restore(*storage->addons()); });
    connect(storage, &Storage::foldersImported, local, [storage, local] { local->restoreFolders(*storage->folders()); });
}

// Before the database, settings were in QSettings and the add-ons and
// folders in two JSON files. Copied over once; the old ones stay as they were.
void AppCore::importOldSettings()
{
    Storage& storage = *m_storage;
    const QString done = QString::fromLatin1(kImportedOldSettings);
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
    copy(kHwdec, QMetaType::Bool);
    copy(QStringLiteral("torrents/inMemory"), QMetaType::Bool);
    copy(QStringLiteral("torrents/memoryCapMB"), QMetaType::Int);
    copy(QStringLiteral("torrents/diskCacheGB"), QMetaType::Int);
    copy(QStringLiteral("torrents/cacheFolder"), QMetaType::QString);

    const auto readJson = [&](const QString& name) {
        QFile file(QDir(m_dataDir).filePath(name));
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

} // namespace cj
