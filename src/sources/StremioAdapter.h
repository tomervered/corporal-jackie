#pragma once
// Stremio add-ons as one source adapter: installs add-ons from manifest
// links, keeps the list on disk, and asks every add-on that serves a
// resource for it. Add-ons run elsewhere; this only speaks their HTTP+JSON
// protocol (see StremioProtocol.h).

#include "JsonFetcher.h"
#include "SourceAdapter.h"
#include "StremioProtocol.h"

#include <QUrl>

namespace cj {

struct InstalledAddon
{
    QUrl manifestUrl;
    stremio::Manifest manifest;
    QJsonObject rawManifest;  // kept on disk so catalogs show without network
    QString lastError;        // the last failed request, in words; empty when fine
};

class StremioAdapter : public SourceAdapter
{
    Q_OBJECT

public:
    static constexpr int kMaxAddons = 50;
    static constexpr auto kCinemetaUrl = "https://v3-cinemeta.strem.io/manifest.json";

    // storeFile: a JSON file the installed list is kept in. Empty: no file;
    // the app keeps the list in its database through storedList(), restore()
    // and addonsChanged. With preinstall and no file yet, a first run starts
    // with Cinemeta, which has catalogs and details but no streams.
    explicit StremioAdapter(const QString& storeFile = {}, bool preinstall = true, QObject* parent = nullptr);

    QString id() const override { return QStringLiteral("stremio"); }
    QString name() const override { return QStringLiteral("Add-ons"); }

    QList<CatalogRef> catalogs() const override;
    void fetchCatalog(const CatalogRef& catalog, const QString& search, int skip, CatalogDone done) override;
    void fetchMeta(const MetaPreview& item, MetaDone done) override;
    void fetchStreams(const QString& type, const QString& videoId, StreamsPart part,
                      std::function<void()> finished) override;
    void fetchSubtitles(const QString& type, const QString& videoId, SubtitlesDone done) override;

    QList<InstalledAddon> addons() const { return m_addons; }

    // Accepts https://…/manifest.json and stremio://…/manifest.json. An
    // add-on already installed is updated in place. `done` gets an empty
    // error on success.
    void install(const QString& link, std::function<void(const QString& error)> done);
    bool uninstall(const QString& addonId);
    // Moves an add-on up (-1) or down (+1) in the list; earlier ones are
    // asked first and their lists come first.
    bool move(const QString& addonId, int delta);
    // Fetches every manifest again (add-ons change their catalogs).
    void refreshManifests();

    // The installed list as stored: [{transportUrl, manifest}, ...].
    QJsonArray storedList() const;
    // Replaces the installed list with a stored one; bad entries are skipped.
    void restore(const QJsonArray& list);
    // What a first run starts with: Cinemeta.
    void installDefaults();

    JsonFetcher& fetcher() { return m_fetcher; }

signals:
    void addonsChanged();

private:
    void load(bool preinstall);
    void save() const;
    void setManifest(const QUrl& url, const stremio::Manifest& manifest, const QJsonObject& raw);
    void noteResult(const QString& addonId, const QString& error);
    const InstalledAddon* find(const QString& addonId) const;
    QList<const InstalledAddon*> serving(const QString& resource, const QString& type, const QString& id) const;
    void tryMeta(QStringList addonIds, const MetaPreview& item, MetaDone done);

    QString m_storeFile;
    JsonFetcher m_fetcher;
    QList<InstalledAddon> m_addons;
};

} // namespace cj
