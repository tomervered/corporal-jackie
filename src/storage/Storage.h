#pragma once
// The app's database: settings, installed add-ons, local folders, the
// library and watch history, in one SQLite file. See src/storage/README.md.
//
// Every query runs on a worker thread. The UI thread reads from an in-memory
// copy that is loaded once at start and kept up to date by each change, so a
// read never waits on the disk and nothing polls; changes are written behind
// in order. Signals say when something changed.

#include "PlaybackTypes.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QStringList>
#include <QVariant>

#include <functional>
#include <memory>
#include <mutex>
#include <optional>

struct sqlite3;

namespace cj {

// Where the user got to in one video. `key` is "<adapter>:<item>:<video>" for
// items from a source adapter, or "url:" + Storage::sourceKey() for anything
// opened directly (a file, a link, a magnet).
struct HistoryEntry
{
    QString key;
    QString adapter;      // "stremio", "local", or "" for a direct open
    QString provider;
    QString itemId;       // the show or movie; equal for every episode of a show
    QString itemType;     // "movie", "series", ...
    QString itemName;
    QString poster;
    QString videoId;      // the episode, or the movie's own id
    QString videoTitle;
    int season = 0;
    int episode = 0;
    PlayableSource source;  // what played, to play it again
    double position = 0;    // seconds
    double duration = 0;
    qint64 updatedAt = 0;   // ms since the epoch
    bool finished = false;  // watched to (nearly) the end

    // One row per show or movie in Continue Watching.
    QString itemKey() const { return adapter.isEmpty() ? key : adapter + QLatin1Char(':') + itemId; }
};

// An item the user saved to their library.
struct LibraryItem
{
    QString adapter;
    QString provider;
    QString itemId;
    QString type;
    QString name;
    QString poster;
    QString releaseInfo;
    qint64 addedAt = 0;

    QString key() const { return adapter + QLatin1Char(':') + itemId; }
};

class Storage : public QObject
{
    Q_OBJECT

public:
    static constexpr int kSchemaVersion = 1;
    static constexpr int kMaxHistory = 2000;   // oldest entries go first
    static constexpr int kMaxLibrary = 5000;
    static constexpr double kMinResumeSeconds = 30;  // earlier than this starts from the beginning

    // Opens (or creates and migrates) the database and loads the in-memory
    // copy; returns once that is done. With an empty path, an in-memory
    // database (for tests).
    explicit Storage(const QString& file, QObject* parent = nullptr);
    ~Storage() override;  // waits for pending writes

    bool isOpen() const { return m_open; }
    // Why the database could not be opened, or why the last write failed,
    // in words; empty when all is well. errorChanged() says when it changes.
    // A failed write leaves the in-memory copy ahead of the file.
    QString error() const;

    // --- Settings ---------------------------------------------------------------
    QVariant setting(const QString& key, const QVariant& fallback = {}) const;
    void setSetting(const QString& key, const QVariant& value);

    // --- Add-ons and folders: stored as the source adapters hand them over -------
    // nullopt until the first save: a fresh install, so the caller seeds defaults.
    std::optional<QJsonArray> addons() const { return m_addons; }
    void saveAddons(const QJsonArray& addons);
    std::optional<QStringList> folders() const { return m_folders; }
    void saveFolders(const QStringList& folders);

    // --- Watch history -----------------------------------------------------------
    QList<HistoryEntry> history() const { return m_history; }  // newest first
    void record(const HistoryEntry& entry);  // adds or updates by key
    std::optional<HistoryEntry> find(const QString& key) const;
    std::optional<HistoryEntry> findBySource(const PlayableSource& source) const;
    void forget(const QString& itemKey);     // every entry of one show or movie
    void clearHistory();
    // One entry per show or movie, newest first: unfinished ones past
    // kMinResumeSeconds.
    QList<HistoryEntry> continueWatching(int limit = 20) const;
    // Where to start a source: its saved position, or 0 if new or finished.
    double resumePosition(const PlayableSource& source) const;

    // --- Library -----------------------------------------------------------------
    QList<LibraryItem> library() const { return m_library; }  // newest first
    bool inLibrary(const QString& key) const;
    void addToLibrary(const LibraryItem& item);
    void removeFromLibrary(const QString& key);

    // --- Export and import: library, history, add-ons and folders -----------------
    // Shareable (the default) leaves out what lets someone else in: request
    // headers, signed and add-on stream links (history keeps the item's ids,
    // and playing it again asks the add-on for a stream), tracker passkeys in
    // magnets, and add-ons whose link holds settings (named under
    // "addonsWithSettings", to set up again). Full keeps everything, for a
    // private backup.
    enum class ExportMode { Shareable, Full };
    QByteArray exportJson(ExportMode mode = ExportMode::Shareable) const;
    // Merges: newer history wins, library items and folders are added, add-ons
    // are added when not installed. Checks the whole file first, then writes
    // it in one transaction and waits for it: either all of it is saved, or
    // nothing changes. Returns an error in words, or empty.
    QString importJson(const QByteArray& json);

    // Waits until every write so far is on disk (tests, shutdown).
    void flush();

    // What identifies a source across runs: what it was opened from (a
    // magnet link, say) when that was resolved to a local stream URL, which
    // changes from run to run; otherwise its URL. A magnet counts by its info
    // hash and file only.
    static QString sourceKey(const PlayableSource& source);
    static QJsonObject sourceToJson(const PlayableSource& source);
    static PlayableSource sourceFromJson(const QJsonObject& json);

signals:
    void historyChanged();
    void libraryChanged();
    void addonsImported();   // import added add-ons or folders: adapters reload
    void foldersImported();
    void errorChanged();

private:
    class Worker;
    // Queues a change; `job` returns false when it failed, and error() says why.
    void write(std::function<bool(sqlite3*)> job);
    void setWriteError(const QString& cause);
    void trimHistory();
    void trimLibrary();
    void keepInMemory(const HistoryEntry& e);
    void keepInMemory(const LibraryItem& i);

    std::unique_ptr<Worker> m_worker;
    bool m_open = false;
    mutable std::mutex m_errorMutex;  // the worker sets m_error too
    QString m_error;
    QVariantMap m_settings;
    std::optional<QJsonArray> m_addons;
    std::optional<QStringList> m_folders;
    QList<HistoryEntry> m_history;
    QList<LibraryItem> m_library;
};

} // namespace cj
