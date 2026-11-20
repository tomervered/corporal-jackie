#pragma once
// App-wide preferences the UI edits that are not part of playback: where and
// how torrents are stored. Saved in the app's database. Exposed to QML as
// `preferences`.
//
// The torrent engine reads these once at start, so changes apply the next
// time the app opens; `restartNeeded` says when that is the case.

#include <QObject>
#include <QString>
#include <QUrl>
#include <QVariantMap>

namespace cj {

class Storage;

class Preferences : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool torrentsInMemory READ torrentsInMemory WRITE setTorrentsInMemory NOTIFY changed)
    Q_PROPERTY(int memoryCapMB READ memoryCapMB WRITE setMemoryCapMB NOTIFY changed)
    Q_PROPERTY(int diskCacheGB READ diskCacheGB WRITE setDiskCacheGB NOTIFY changed)
    Q_PROPERTY(QString cacheFolder READ cacheFolder NOTIFY changed)
    Q_PROPERTY(bool customCacheFolder READ customCacheFolder NOTIFY changed)
    Q_PROPERTY(bool restartNeeded READ restartNeeded NOTIFY changed)
    // Which third parties the app may contact. All off until the person has
    // answered the first-run screen; each applies at once.
    Q_PROPERTY(bool privacyChosen READ privacyChosen NOTIFY privacyChanged)
    Q_PROPERTY(bool useCatalogAddon READ useCatalogAddon WRITE setUseCatalogAddon NOTIFY privacyChanged)
    Q_PROPERTY(bool useDht READ useDht WRITE setUseDht NOTIFY privacyChanged)
    Q_PROPERTY(bool usePublicTrackers READ usePublicTrackers WRITE setUsePublicTrackers NOTIFY privacyChanged)
    Q_PROPERTY(bool usePortMapping READ usePortMapping WRITE setUsePortMapping NOTIFY privacyChanged)
    // Playing torrents at all. Off: the torrent engine never starts.
    Q_PROPERTY(bool useTorrents READ useTorrents WRITE setUseTorrents NOTIFY privacyChanged)
    // Sending pieces of a torrent to other peers while it plays. Off until
    // the person turns it on; applies at once.
    Q_PROPERTY(bool allowUploads READ allowUploads WRITE setAllowUploads NOTIFY privacyChanged)
    // The answers saved before the screen was last changed, to start it from
    // when it asks again: {catalogAddon, dht, trackers, portMapping}.
    Q_PROPERTY(QVariantMap earlierChoices READ earlierChoices CONSTANT)
    // The look: "dark" (olive), "oled" or "bright", and the colours the
    // person changed in each, as {mode: {accent, canvas, panel, text}}.
    Q_PROPERTY(QString colorMode READ colorMode WRITE setColorMode NOTIFY themeChanged)
    Q_PROPERTY(QVariantMap colorEdits READ colorEdits WRITE setColorEdits NOTIFY themeChanged)
    // The upscaler badge over the video (top left).
    Q_PROPERTY(bool showUpscalerBadge READ showUpscalerBadge WRITE setShowUpscalerBadge NOTIFY upscalerBadgeChanged)
    // With white or near-white panels the player's controls turn charcoal,
    // unless this is on.
    Q_PROPERTY(QString playerHalo READ playerHalo WRITE setPlayerHalo NOTIFY playerHaloChanged)
    // Palettes the person named and saved: [{name, accent, canvas, panel, text}].
    Q_PROPERTY(QVariantList savedPalettes READ savedPalettes NOTIFY savedPalettesChanged)

public:
    // defaultCacheFolder: the torrent cache under the app's cache folder.
    Preferences(Storage& storage, const QString& defaultCacheFolder, QObject* parent = nullptr);

    bool torrentsInMemory() const { return m_inMemory; }
    int memoryCapMB() const { return m_memoryCapMB; }
    int diskCacheGB() const { return m_diskCacheGB; }
    QString cacheFolder() const;   // as a path, native separators
    bool customCacheFolder() const { return !m_cacheFolder.isEmpty(); }
    bool restartNeeded() const;

    bool privacyChosen() const { return m_privacyChosen; }
    // The default catalog add-on, WikiMaze (its lists, details and search).
    bool useCatalogAddon() const { return m_catalogAddon; }
    // The DHT and discovery on the local network, to find more peers.
    bool useDht() const { return m_dht; }
    // The app's own list of open trackers, added to public torrents.
    bool usePublicTrackers() const { return m_publicTrackers; }
    // Asking the router to open a port (UPnP, NAT-PMP).
    bool usePortMapping() const { return m_portMapping; }
    void setUseCatalogAddon(bool on);
    void setUseDht(bool on);
    void setUsePublicTrackers(bool on);
    void setUsePortMapping(bool on);
    bool useTorrents() const { return m_torrents; }
    void setUseTorrents(bool on);
    bool allowUploads() const { return m_uploads; }
    void setAllowUploads(bool on);
    QVariantMap earlierChoices() const { return m_earlier; }
    // The first-run screen's answer: records every choice and that the
    // person has chosen. Nothing is applied before this.
    Q_INVOKABLE void choosePrivacy(bool catalogAddon, bool dht, bool publicTrackers, bool portMapping,
                                   bool torrents = false, bool uploads = false);

    QString colorMode() const { return m_colorMode; }
    bool showUpscalerBadge() const { return m_upscalerBadge; }
    void setShowUpscalerBadge(bool on);
    // How strong the fade behind the player's controls is: full, half, little or off.
    QString playerHalo() const { return m_playerHalo; }
    void setPlayerHalo(const QString& level);
    QVariantList savedPalettes() const { return m_savedPalettes; }
    // Adds a palette under `name` (one of the same name is replaced). ""
    // or what went wrong.
    Q_INVOKABLE QString savePalette(const QString& name, const QVariantMap& colors);
    Q_INVOKABLE void removePalette(const QString& name);
    void setColorMode(const QString& mode);
    QVariantMap colorEdits() const { return m_colorEdits; }
    void setColorEdits(const QVariantMap& edits);
    Q_INVOKABLE void setTheme(const QString& mode, const QVariantMap& edits);
    // The four colours (accent, canvas, panel, text) to a small JSON file,
    // and back: an import is saved under its name and becomes the Custom
    // mode. "" or what went wrong.
    Q_INVOKABLE QString exportColors(const QUrl& file, const QString& name, const QVariantMap& colors) const;
    Q_INVOKABLE QString importColors(const QUrl& file);

    void setTorrentsInMemory(bool on);
    void setMemoryCapMB(int mb);
    void setDiskCacheGB(int gb);
    Q_INVOKABLE void setCacheFolder(const QUrl& folder);
    Q_INVOKABLE void resetCacheFolder();

    // What the running engine uses: call once at start, before the engine.
    bool startInMemory() const { return m_started.inMemory; }
    qint64 startMemoryCapBytes() const { return qint64(m_started.memoryCapMB) * 1024 * 1024; }
    qint64 startDiskCacheBytes() const { return qint64(m_started.diskCacheGB) * 1024 * 1024 * 1024; }
    QString startCacheFolder() const { return m_started.folder; }

signals:
    void changed();
    // Any of the four choices, or that they were made.
    void privacyChanged();
    // The catalog add-on went from off to on or back: install or remove it.
    void catalogAddonChoiceChanged(bool on);
    void appearanceChanged();
    void themeChanged();
    void upscalerBadgeChanged();
    void playerHaloChanged();
    void savedPalettesChanged();

private:
    void save();
    void savePrivacy(bool catalogBefore);
    QString effectiveFolder() const;

    Storage& m_storage;
    QString m_defaultFolder;
    bool m_inMemory = false;
    int m_memoryCapMB = 1024;
    int m_diskCacheGB = 10;
    QString m_cacheFolder;  // empty = default
    bool m_privacyChosen = false;
    bool m_catalogAddon = false;
    bool m_dht = false;
    bool m_publicTrackers = false;
    bool m_portMapping = false;
    bool m_torrents = false;
    bool m_uploads = false;
    QVariantMap m_earlier;
    QString m_colorMode = QStringLiteral("dark");
    QVariantMap m_colorEdits;
    bool m_upscalerBadge = true;
    QString m_playerHalo = QStringLiteral("full");
    QVariantList m_savedPalettes;
    void storePalettes();
    struct Started { bool inMemory; int memoryCapMB; int diskCacheGB; QString folder; } m_started;
};

} // namespace cj
