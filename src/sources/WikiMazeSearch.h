#pragma once
// Advanced search through WikiMaze's own JSON API (/api/search, /api/genres,
// /api/lists), for the search page's WikiMaze row and its filters. Used only
// when WikiMaze is installed; an older WikiMaze without the API answers 404
// and `unsupported` turns on, so the page keeps working through the ordinary
// catalog search of the add-ons.
//
// Privacy: only the typed words and the chosen filters leave the computer,
// to the host of the installed WikiMaze. Nothing is kept on disk; the genre
// and list names are held in memory for the session.

#include "JsonFetcher.h"
#include "SourceTypes.h"

#include <QObject>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

namespace cj {

class WikiMazeSearch : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool available READ available NOTIFY baseChanged)
    Q_PROPERTY(bool unsupported READ unsupported NOTIFY changed)
    Q_PROPERTY(bool loading READ loading NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(QString source READ source NOTIFY changed)       // "store", "store+live" or "live"
    Q_PROPERTY(int total READ total NOTIFY changed)
    Q_PROPERTY(QVariantList results READ results NOTIFY changed)   // plain preview maps, as every row
    Q_PROPERTY(QVariantMap genreCounts READ genreCounts NOTIFY changed)
    Q_PROPERTY(QVariantMap decadeCounts READ decadeCounts NOTIFY changed)
    Q_PROPERTY(QVariantList genres READ genres NOTIFY filtersChanged)   // [{key, label, movie, series}]
    Q_PROPERTY(QVariantList lists READ lists NOTIFY filtersChanged)     // [{id, type, name}]

public:
    static constexpr int kDebounceMs = 300;
    static constexpr int kTimeoutMs = 6000;
    static constexpr int kPageSize = 50;

    explicit WikiMazeSearch(QObject* parent = nullptr);

    // The installed WikiMaze's address (StremioAdapter::wikimazeBase()), or
    // empty. A new address clears everything.
    void setBase(const QString& base);

    bool available() const { return !m_base.isEmpty(); }
    bool unsupported() const { return m_unsupported; }
    bool loading() const { return m_loading; }
    QString error() const { return m_error; }
    QString source() const { return m_source; }
    int total() const { return m_total; }
    QVariantList results() const;
    QVariantMap genreCounts() const { return m_genreCounts; }
    QVariantMap decadeCounts() const { return m_decadeCounts; }
    QVariantList genres() const { return m_genres; }
    QVariantList lists() const { return m_lists; }
    QList<MetaPreview> items() const { return m_items; }

    // Asks after the debounce; a newer call replaces an older one, and an
    // answer to a replaced request is dropped. Empty text and no filter
    // clears the results instead. year: "2026", "2010s" or "Older".
    // list: a catalog id from lists(), empty for the whole store.
    Q_INVOKABLE void search(const QString& text, const QString& type, const QString& genre,
                            const QString& year, const QString& list, bool live);
    Q_INVOKABLE void clear();
    // The genre and list names, once per address.
    Q_INVOKABLE void loadFilters();

    // The request for a search, public so tests can check how filters map.
    static QUrl searchUrl(const QString& base, const QString& text, const QString& type, const QString& genre,
                          const QString& year, const QString& list, bool live);

signals:
    void changed();
    void baseChanged();
    void filtersChanged();

private:
    struct Request
    {
        QString text, type, genre, year, list;
        bool live = false;
    };
    void run();
    void reset();

    JsonFetcher m_searchFetcher;   // short timeout, no second try
    JsonFetcher m_filterFetcher;
    QTimer m_debounce;
    QString m_base;
    Request m_request;
    quint64 m_gen = 0;
    bool m_unsupported = false;
    bool m_loading = false;
    bool m_filtersAsked = false;
    QString m_error;
    QString m_source;
    int m_total = 0;
    QList<MetaPreview> m_items;
    QVariantMap m_genreCounts;
    QVariantMap m_decadeCounts;
    QVariantList m_genres;
    QVariantList m_lists;
};

} // namespace cj
