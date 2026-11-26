#pragma once
// Watch history, Continue Watching and the saved library, for the UI
// (exposed to QML as `library`). Records where the user got to from the
// PlaybackSession's resume points, tells the PlaybackSession where to start an item, and
// keeps the LibraryDatabase up to date.

#include "SourceTypes.h"
#include "LibraryDatabase.h"

#include <QHash>
#include <QObject>
#include <QUrl>
#include <QVariantList>

#include <optional>

namespace cj {

class CatalogBrowser;
class PlaybackSession;

class Library : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList continueWatching READ continueWatching NOTIFY historyChanged)
    Q_PROPERTY(int historyCount READ historyCount NOTIFY historyChanged)
    // Off: nothing new goes into the history (LibraryDatabase::keepsHistory).
    Q_PROPERTY(bool keepHistory READ keepHistory WRITE setKeepHistory NOTIFY keepHistoryChanged)
    Q_PROPERTY(QVariantList items READ items NOTIFY itemsChanged)
    // Why saving failed, in words, or "": shown in Settings.
    Q_PROPERTY(QString storageError READ storageError NOTIFY storageErrorChanged)

public:
    Library(LibraryDatabase& storage, PlaybackSession& session, CatalogBrowser& browser, QObject* parent = nullptr);

    // Watched to the end, or close enough that the rest is credits.
    static bool isFinished(double position, double duration);

    QVariantList continueWatching() const;
    int historyCount() const { return int(m_storage.history().size()); }
    bool keepHistory() const { return m_storage.keepsHistory(); }
    void setKeepHistory(bool keep);
    QVariantList items() const;
    QString storageError() const { return m_storage.error(); }

    // Plays a history entry again from where it was left.
    Q_INVOKABLE void resume(const QString& key);
    // Removes a show or movie from Continue Watching and the history.
    Q_INVOKABLE void forget(const QString& itemKey);
    // The history, and the log files, which name what played.
    Q_INVOKABLE void clearHistory();
    // Records where the current item is now (the app is closing).
    void saveNow();

    Q_INVOKABLE bool contains(const QString& key) const;
    // How far into one video (a history key): 0-1, 1 when finished, -1 never played.
    Q_INVOKABLE double progress(const QString& key) const;
    // An item as the browse panel shows it (CatalogBrowser::detail()).
    Q_INVOKABLE void add(const QVariantMap& item);
    Q_INVOKABLE void remove(const QString& key);
    // Shows a saved item, or the show of a history entry, in the browse panel.
    Q_INVOKABLE void open(const QString& key);

    // Every list as one file, and back. Each returns an error in words, or "".
    // The export leaves out logins and signed links unless `everything`.
    Q_INVOKABLE QString exportTo(const QUrl& file, bool everything = false) const;
    // An import adds add-ons and folders only when `withAddonsAndFolders`;
    // importAsks() lists them first so the user can say yes or no (an export
    // may come from someone else; see LibraryDatabase::importJson).
    Q_INVOKABLE QStringList importAsks(const QUrl& file) const;
    Q_INVOKABLE QString importFrom(const QUrl& file, bool withAddonsAndFolders = false);

signals:
    void historyChanged();
    void keepHistoryChanged();
    void itemsChanged();
    void storageErrorChanged();
    // The browse panel should open to show an item.
    void openRequested();

private:
    struct Current
    {
        HistoryEntry entry;  // everything but the position
        QUrl url;
    };
    static HistoryEntry entryFor(const PlayContext& context);
    void bindPending();
    void save(const PlayableSource& source, double position, double duration, bool ended);
    double resumeAt(const PlayableSource& source) const;

    LibraryDatabase& m_storage;
    PlaybackSession& m_session;
    CatalogBrowser& m_browser;
    std::optional<HistoryEntry> m_pending;  // picked, not yet loaded
    std::optional<Current> m_current;       // what is loaded now, when picked
    // progress() by history key, built on first use after each history
    // change: episode lists ask once per row, and history changes every few
    // seconds during playback.
    mutable std::optional<QHash<QString, double>> m_progress;
};

} // namespace cj
