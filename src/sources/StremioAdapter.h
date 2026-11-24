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
    bool disabled = false;    // switched off: kept, but never asked for anything
    QStringList hiddenCatalogs;   // "type/id" of lists switched off (kept, not shown anywhere)
};

class StremioAdapter : public SourceAdapter
{
    Q_OBJECT

public:
    static constexpr int kMaxAddons = 50;
    // The add-ons a first run starts with: WikiMaze (lists and details, no
    // streams) and Public Domain Movies (a list of public-domain films with
    // their torrents).
    static constexpr auto kDefaultAddonId = "community.wikimaze";
    static constexpr auto kDefaultAddonUrl = "https://wikimaze-seven.vercel.app/manifest.json";
    static constexpr auto kPublicDomainId = "org.stremio.pubdomainmovies";
    static constexpr auto kPublicDomainUrl = "https://caching.stremio.net/publicdomainmovies.now.sh/manifest.json";
    static QStringList defaultAddonIds();
    // The default before WikiMaze: removed once from installs that had it.
    static constexpr auto kCinemetaId = "com.linvo.cinemeta";

    // storeFile: a JSON file the installed list is kept in. Empty: no file;
    // the app keeps the list in its database through storedList(), restore()
    // and addonsChanged. With preinstall and no file yet, a first run starts
    // with the default add-ons (installDefaults()).
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

    // WikiMaze's address (its manifest link without "/manifest.json", with a
    // language prefix such as /he-il if the link has one), for its search API.
    // Empty when it is not installed or switched off.
    QString wikimazeBase() const;

    // Accepts https://…/manifest.json and stremio://…/manifest.json. An
    // add-on already installed is updated in place. `done` gets an empty
    // error on success.
    void install(const QString& link, std::function<void(const QString& error)> done);
    bool uninstall(const QString& addonId);
    // Moves an add-on up (-1) or down (+1) in the list; earlier ones are
    // asked first and their lists come first.
    bool move(const QString& addonId, int delta);
    // Switches an add-on off (kept in the list, never asked) or on again.
    bool setEnabled(const QString& addonId, bool enabled);
    // Switches one of an add-on's lists off (hidden from Home, Discover and search) or on again.
    bool setCatalogEnabled(const QString& addonId, const QString& type, const QString& catalogId, bool enabled);
    // Fetches every manifest again (add-ons change their catalogs).
    void refreshManifests();

    // The installed list as stored: [{transportUrl, manifest}, ...].
    QJsonArray storedList() const;
    // Replaces the installed list with a stored one; bad entries are skipped.
    void restore(const QJsonArray& list);
    // What a first run starts with: WikiMaze and Public Domain Movies, from
    // built-in copies of their manifests that the next refresh replaces.
    // Ones already installed are left as they are. `only`: just these ids.
    void installDefaults(const QStringList& only = {});
    // Removes the default add-ons. True if any was installed.
    bool uninstallDefaults();

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
