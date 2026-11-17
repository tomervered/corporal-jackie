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

#include <algorithm>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QLocale>
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
const QString kCinemetaRetired = QStringLiteral("addons/cinemetaRetired");

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

void AppCore::useLeftToRight()
{
    // Qt Quick Controls mirror themselves when the locale reads right to
    // left, whatever the application's direction: the locale changes too.
    // Sizes and numbers then read the same as the rest of the window.
    QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates));
    QGuiApplication::setLayoutDirection(Qt::LeftToRight);
}

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
    // What the person allowed (nothing until they answer the first-run
    // screen), as it is now: these apply while running.
    s.dht = m_preferences->useDht();
    s.localDiscovery = m_preferences->useDht();
    s.portMapping = m_preferences->usePortMapping();
    s.publicTrackers = m_preferences->usePublicTrackers();
    s.torrents = m_preferences->useTorrents();
    s.upload = m_preferences->allowUploads();
    return s;
}

void AppCore::start(PlaybackTarget* target, TorrentTransport& torrents, bool registerTorrents)
{
    m_torrents = &torrents;
    // The UI talks only to the Session (rule 1).
    m_session = std::make_unique<Session>(target);
    Session* session = m_session.get();

    // Torrents while the person has not turned them on: refused here with
    // their own message (the transport refuses too, if one gets past).
    session->setSourceCheck([this](const PlayableSource& s) -> std::optional<std::pair<PlaybackError, QString>> {
        if (s.kind == PlayableSource::Kind::Torrent && !m_preferences->useTorrents())
            return std::pair{PlaybackError::TorrentsOff, QStringLiteral("Torrent playback is off.")};
        return std::nullopt;
    });
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
    // The choices apply as they are made.
    connect(m_preferences.get(), &Preferences::privacyChanged, this,
            [this] { m_torrents->setNetwork(brokerSettings()); });
    connect(m_preferences.get(), &Preferences::catalogAddonChoiceChanged, this, [this](bool on) { syncDefaultAddon(on); });

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

// The default add-on (WikiMaze) is installed only when the person says yes
// on the first-run screen: a fresh install starts with no add-ons, and an
// install from before that screen (or an import from one) loses it until
// they answer.
void AppCore::dropDefaultAddonUntilChosen()
{
    if (!m_preferences->privacyChosen())
        m_addons->uninstall(QString::fromLatin1(StremioAdapter::kDefaultAddonId));
}

// Cinemeta (Stremio's server) was the default add-on before WikiMaze. An
// install from that time loses it once, at its first start with this
// version; an imported list always does. Pasting its link adds it again.
void AppCore::retireCinemeta(bool always)
{
    if (!always && m_storage->setting(kCinemetaRetired, false).toBool())
        return;
    m_addons->uninstall(QString::fromLatin1(StremioAdapter::kCinemetaId));
    m_storage->setSetting(kCinemetaRetired, true);
}

void AppCore::syncDefaultAddon(bool on)
{
    const QString id = QString::fromLatin1(StremioAdapter::kDefaultAddonId);
    const auto& list = m_addons->addons();
    const bool has = std::any_of(list.begin(), list.end(), [&](const InstalledAddon& a) { return a.manifest.id == id; });
    if (on && !has)
        m_addons->installDefaults();
    else if (!on && has)
        m_addons->uninstall(id);
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
// with WikiMaze. Changes are saved as they happen, and an import reloads them.
void AppCore::restoreSources()
{
    Storage* storage = m_storage.get();
    m_addons = std::make_unique<StremioAdapter>();
    StremioAdapter* addons = m_addons.get();
    if (const auto stored = storage->addons())
        addons->restore(*stored);
    retireCinemeta(false);
    dropDefaultAddonUntilChosen();
    storage->saveAddons(addons->storedList());
    connect(addons, &StremioAdapter::addonsChanged, storage, [storage, addons] { storage->saveAddons(addons->storedList()); });

    m_localFiles = std::make_unique<LocalLibrary>();
    LocalLibrary* local = m_localFiles.get();
    if (const auto stored = storage->folders(); stored && !stored->isEmpty())
        local->restoreFolders(*stored);
    connect(local, &LocalLibrary::foldersChanged, storage, [storage, local] { storage->saveFolders(local->folders()); });

    connect(storage, &Storage::addonsImported, addons, [this, storage, addons] {
        addons->restore(*storage->addons());
        retireCinemeta(true);
        dropDefaultAddonUntilChosen();
    });
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
