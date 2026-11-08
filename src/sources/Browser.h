#pragma once
// What the browse panel binds to: catalogs from every source adapter, the
// items of one catalog, one item's details and episodes, and the ranked
// stream choices for one video. Playing a choice hands its PlayableSource to
// the Session; the panel never sees an adapter or the player.
//
// Each list is a QVariantList of plain maps, small enough for a ListView.
// A newer request makes older answers stale: they are dropped when they
// arrive (rule 4), and every network request is bounded by a timeout.

#include "SourceAdapter.h"

#include <QObject>
#include <QVariantList>
#include <QVariantMap>

namespace cj {

class LocalLibrary;
class Session;
class StremioAdapter;

class Browser : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList addons READ addons NOTIFY addonsChanged)
    Q_PROPERTY(bool installing READ installing NOTIFY addonsChanged)
    Q_PROPERTY(QStringList folders READ folders NOTIFY foldersChanged)
    Q_PROPERTY(bool scanning READ scanning NOTIFY foldersChanged)
    Q_PROPERTY(int localVideoCount READ localVideoCount NOTIFY foldersChanged)
    Q_PROPERTY(QVariantList catalogs READ catalogs NOTIFY catalogsChanged)
    Q_PROPERTY(int catalogIndex READ catalogIndex NOTIFY itemsChanged)
    Q_PROPERTY(QString search READ search NOTIFY itemsChanged)
    Q_PROPERTY(QStringList genres READ genres NOTIFY itemsChanged)
    Q_PROPERTY(QString genre READ genre NOTIFY itemsChanged)
    Q_PROPERTY(QVariantList homeRows READ homeRows NOTIFY homeChanged)
    Q_PROPERTY(QVariantList searchRows READ searchRows NOTIFY searchRowsChanged)
    Q_PROPERTY(QString searchAllText READ searchAllText NOTIFY searchRowsChanged)
    Q_PROPERTY(QVariantList items READ items NOTIFY itemsChanged)
    Q_PROPERTY(bool itemsLoading READ itemsLoading NOTIFY itemsChanged)
    Q_PROPERTY(QString itemsError READ itemsError NOTIFY itemsChanged)
    Q_PROPERTY(bool canLoadMore READ canLoadMore NOTIFY itemsChanged)
    Q_PROPERTY(QVariantMap detail READ detail NOTIFY detailChanged)
    Q_PROPERTY(QVariantList videos READ videos NOTIFY detailChanged)
    Q_PROPERTY(bool detailLoading READ detailLoading NOTIFY detailChanged)
    Q_PROPERTY(QString detailError READ detailError NOTIFY detailChanged)
    Q_PROPERTY(QString streamsTitle READ streamsTitle NOTIFY streamsChanged)
    Q_PROPERTY(QVariantList streams READ streams NOTIFY streamsChanged)
    Q_PROPERTY(bool streamsLoading READ streamsLoading NOTIFY streamsChanged)
    Q_PROPERTY(QStringList streamProblems READ streamProblems NOTIFY streamsChanged)

public:
    static constexpr int kHomeRows = 20;
    static constexpr int kRowItems = 30;

    explicit Browser(Session* session, QObject* parent = nullptr);

    // The adapters, in the order their catalogs and streams are listed.
    // v1 has the Stremio add-ons and local folders; a YouTube adapter is
    // added here later, nothing else changes.
    void addAdapter(SourceAdapter* adapter);
    void setStremio(StremioAdapter* stremio);
    void setLocal(LocalLibrary* local);

    QVariantList addons() const;
    bool installing() const { return m_installing > 0; }
    QStringList folders() const;
    bool scanning() const;
    int localVideoCount() const;
    QVariantList catalogs() const;
    int catalogIndex() const { return m_catalogIndex; }
    QString search() const { return m_search; }
    QStringList genres() const { return m_catalogs.value(m_catalogIndex).genres; }
    QString genre() const { return m_genre; }
    QVariantList homeRows() const { return rowsMap(m_homeRows); }
    QVariantList searchRows() const { return rowsMap(m_searchRows); }
    QString searchAllText() const { return m_searchAllText; }
    QVariantList items() const;
    bool itemsLoading() const { return m_itemsLoading; }
    QString itemsError() const { return m_itemsError; }
    bool canLoadMore() const { return m_canLoadMore; }
    QVariantMap detail() const;
    QVariantList videos() const;
    bool detailLoading() const { return m_detailLoading; }
    QString detailError() const { return m_detailError; }
    QString streamsTitle() const { return m_streamsTitle; }
    QVariantList streams() const;
    bool streamsLoading() const { return m_streamsLoading; }
    QStringList streamProblems() const { return m_streamProblems; }

    Q_INVOKABLE void installAddon(const QString& link);
    Q_INVOKABLE void removeAddon(const QString& addonId);
    // Moves an add-on up (-1) or down (+1): its lists and streams come first.
    Q_INVOKABLE void moveAddon(const QString& addonId, int delta);
    Q_INVOKABLE bool addFolder(const QUrl& folder);
    Q_INVOKABLE void removeFolder(const QString& path);
    Q_INVOKABLE void rescanFolders();

    // genre: one of the catalog's genres, or empty for none.
    Q_INVOKABLE void openCatalog(int index, const QString& search = {}, const QString& genre = {});
    // The home page: the first page of every list that can be browsed.
    Q_INVOKABLE void loadHome();
    // Searches every list that can be searched (of one type, or all types
    // when empty); each answers into its own row.
    Q_INVOKABLE void searchAll(const QString& text, const QString& type = {});
    // An item of a home or search row: its details.
    Q_INVOKABLE void openRowItem(bool searchRow, int row, int index);
    Q_INVOKABLE void loadMore();
    Q_INVOKABLE void openItem(int index);
    // Shows an item that is not in the list: one from the library or history.
    void openPreview(const MetaPreview& item);
    // A video of the open item (an episode), or -1 for the item itself (a movie).
    Q_INVOKABLE void openVideo(int index);
    Q_INVOKABLE void play(int streamIndex);

    // For tests and the app: the current state as plain types.
    QList<CatalogRef> catalogRefs() const { return m_catalogs; }
    QList<MetaPreview> itemList() const { return m_items; }
    QList<StreamOption> streamList() const { return m_streams; }

signals:
    void addonsChanged();
    void foldersChanged();
    void catalogsChanged();
    void itemsChanged();
    void detailChanged();
    void streamsChanged();
    void homeChanged();
    void searchRowsChanged();
    // Short messages for the panel: "Example installed", "Not an add-on link".
    void notice(const QString& text, bool isError);
    // A choice is about to be handed to the Session, then was.
    void aboutToPlay(const cj::PlayContext& context);
    void playStarted(const QString& title);

private:
    // One list on the home page or in search results.
    struct Row
    {
        int catalog = -1;   // index in m_catalogs
        QString name;
        QString type;
        QList<MetaPreview> items;
        bool loading = true;
        QString error;
    };
    static QVariantList rowsMap(const QList<Row>& rows);
    void fetchRows(QList<Row>* rows, quint64* gen, const QString& search, void (Browser::*changed)());
    void rebuildCatalogs();
    void fetchItems(bool append);
    SourceAdapter* adapterFor(const QString& id) const;

    Session* m_session;
    QList<SourceAdapter*> m_adapters;
    StremioAdapter* m_stremio = nullptr;
    LocalLibrary* m_local = nullptr;
    int m_installing = 0;

    QList<CatalogRef> m_catalogs;
    int m_catalogIndex = -1;
    QString m_search;
    QString m_genre;
    QStringList m_homeKeys;   // the catalogs the home rows were loaded for
    QList<Row> m_homeRows;
    quint64 m_homeGen = 0;
    QList<Row> m_searchRows;
    QString m_searchAllText;
    quint64 m_searchGen = 0;
    QList<MetaPreview> m_items;
    bool m_itemsLoading = false;
    QString m_itemsError;
    bool m_canLoadMore = false;
    quint64 m_itemsGen = 0;

    std::optional<MetaDetail> m_detail;
    bool m_detailLoading = false;
    QString m_detailError;
    quint64 m_detailGen = 0;

    QString m_streamsTitle;
    QString m_streamsType;
    QString m_streamsVideoId;
    VideoEntry m_streamsVideo;
    QList<StreamOption> m_streams;
    bool m_streamsLoading = false;
    QStringList m_streamProblems;
    quint64 m_streamsGen = 0;

    // What play() last started, to keep the place when the user switches to
    // another source of the same video.
    QString m_playingVideoId;
    QUrl m_playingUrl;
};

} // namespace cj
