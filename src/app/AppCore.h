#pragma once
// Everything the app is made of except the window: the database, the
// preferences, the Session and how it is wired to the torrent transport, the
// source adapters, the browse model, the library and the caches. main()
// builds one around the real mpv target; tests build one around a fake.
//
// Built in two steps, because the torrent transport has to exist before mpv
// (mpv is handed its stream protocol) yet takes its settings from the
// preferences:
//
//   AppCore core(dataDir, cacheDir);                // database, preferences
//   TorrentTransport torrents(core.brokerSettings());
//   MpvTarget target;
//   core.start(&target, torrents);                  // the rest

#include "BrokerTypes.h"

#include <QObject>
#include <QString>

#include <memory>

class QQmlContext;

namespace cj {

class Browser;
class Caches;
class Library;
class LocalLibrary;
class PlaybackTarget;
class Preferences;
class Session;
class SleepBlocker;
class Storage;
class StremioAdapter;
class TorrentTransport;

class AppCore : public QObject
{
    Q_OBJECT

public:
    // The database lives in dataDir; torrents and images are cached under
    // cacheDir unless the preferences say otherwise.
    AppCore(const QString& dataDir, const QString& cacheDir, QObject* parent = nullptr);
    ~AppCore() override;

    // Torrent storage as the preferences were at start.
    BrokerSettings brokerSettings() const;

    // Builds the Session around `target` and everything that depends on it.
    // `registerTorrents`: the transport was added to the target as a stream
    // protocol, so magnet links can be resolved.
    void start(PlaybackTarget* target, TorrentTransport& torrents, bool registerTorrents);

    // Gives QML its context properties: session, browser, preferences,
    // library, caches and appVersion.
    void exposeTo(QQmlContext* context) const;

    // Records where playback is and stops the torrent waits; call when the
    // app is about to quit.
    void shutDown();

    Storage& storage() const { return *m_storage; }
    Preferences& preferences() const { return *m_preferences; }
    Session& session() const { return *m_session; }
    StremioAdapter& addons() const { return *m_addons; }
    LocalLibrary& localFiles() const { return *m_localFiles; }
    Browser& browser() const { return *m_browser; }
    Library& library() const { return *m_library; }
    Caches& caches() const { return *m_caches; }

    // The database key that records the one-time copy of older settings.
    static constexpr auto kImportedOldSettings = "app/importedOldSettings";

private:
    void importOldSettings();
    void restorePlaybackSettings();
    void restoreSources();
    void dropCinemetaUntilChosen();
    void syncCinemeta(bool on);

    QString m_dataDir;
    QString m_cacheDir;
    TorrentTransport* m_torrents = nullptr;
    // Destroyed in reverse: the library and browser go before the Session,
    // the Session before the adapters' data, the database last.
    std::unique_ptr<Storage> m_storage;
    std::unique_ptr<Preferences> m_preferences;
    std::unique_ptr<Session> m_session;
    std::unique_ptr<SleepBlocker> m_sleepBlocker;
    std::unique_ptr<StremioAdapter> m_addons;
    std::unique_ptr<LocalLibrary> m_localFiles;
    std::unique_ptr<Browser> m_browser;
    std::unique_ptr<Library> m_library;
    std::unique_ptr<Caches> m_caches;
};

} // namespace cj
