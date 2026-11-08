#include "Storage.h"

#include "Logging.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSet>

#include <sqlite3.h>

#include <algorithm>
#include <condition_variable>
#include <deque>
#include <future>
#include <mutex>
#include <thread>

namespace cj {

namespace {

// Each entry moves the schema up one version. Never edit one that shipped:
// add the next.
const char* const kMigrations[] = {
    // 1: first release
    R"sql(
    CREATE TABLE meta(key TEXT PRIMARY KEY, value TEXT NOT NULL);
    CREATE TABLE settings(key TEXT PRIMARY KEY, value TEXT NOT NULL);
    CREATE TABLE addons(position INTEGER NOT NULL, transport_url TEXT PRIMARY KEY, manifest TEXT NOT NULL);
    CREATE TABLE folders(position INTEGER NOT NULL, path TEXT PRIMARY KEY);
    CREATE TABLE history(
        key TEXT PRIMARY KEY, adapter TEXT, provider TEXT, item_id TEXT, item_type TEXT,
        item_name TEXT, poster TEXT, video_id TEXT, video_title TEXT, season INTEGER, episode INTEGER,
        source TEXT, position REAL, duration REAL, updated_at INTEGER, finished INTEGER);
    CREATE INDEX history_by_time ON history(updated_at DESC);
    CREATE TABLE library(
        key TEXT PRIMARY KEY, adapter TEXT, provider TEXT, item_id TEXT, type TEXT, name TEXT,
        poster TEXT, release_info TEXT, added_at INTEGER);
    )sql",
};
static_assert(std::size(kMigrations) == Storage::kSchemaVersion, "one migration per schema version");

QString text(sqlite3_stmt* s, int col)
{
    const auto* p = reinterpret_cast<const char*>(sqlite3_column_text(s, col));
    return p ? QString::fromUtf8(p, sqlite3_column_bytes(s, col)) : QString();
}

// A prepared statement that binds Qt types and finalizes itself.
class Statement
{
public:
    Statement(sqlite3* db, const char* sql)
    {
        if (sqlite3_prepare_v2(db, sql, -1, &m_s, nullptr) != SQLITE_OK)
            qCWarning(lcStorage).noquote() << "SQL error:" << sqlite3_errmsg(db) << "in" << sql;
    }
    ~Statement() { sqlite3_finalize(m_s); }
    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;

    Statement& bind(int i, const QString& v)
    {
        const QByteArray u = v.toUtf8();
        sqlite3_bind_text(m_s, i, u.constData(), int(u.size()), SQLITE_TRANSIENT);
        return *this;
    }
    Statement& bind(int i, double v) { sqlite3_bind_double(m_s, i, v); return *this; }
    Statement& bind(int i, qint64 v) { sqlite3_bind_int64(m_s, i, v); return *this; }
    Statement& bind(int i, int v) { sqlite3_bind_int(m_s, i, v); return *this; }
    bool step() { return m_s && sqlite3_step(m_s) == SQLITE_ROW; }
    bool run()
    {
        if (!m_s)
            return false;
        const int rc = sqlite3_step(m_s);
        if (rc != SQLITE_DONE && rc != SQLITE_ROW) {
            qCWarning(lcStorage).noquote() << "SQL error:" << sqlite3_errmsg(sqlite3_db_handle(m_s));
            return false;
        }
        return true;
    }
    sqlite3_stmt* get() const { return m_s; }

private:
    sqlite3_stmt* m_s = nullptr;
};

bool exec(sqlite3* db, const char* sql)
{
    char* err = nullptr;
    if (sqlite3_exec(db, sql, nullptr, nullptr, &err) != SQLITE_OK) {
        qCWarning(lcStorage).noquote() << "SQL error:" << (err ? err : "?");
        sqlite3_free(err);
        return false;
    }
    return true;
}

QString settingToText(const QVariant& v)
{
    return QString::fromUtf8(QJsonDocument(QJsonArray{QJsonValue::fromVariant(v)}).toJson(QJsonDocument::Compact));
}

QVariant settingFromText(const QString& t)
{
    return QJsonDocument::fromJson(t.toUtf8()).array().at(0).toVariant();
}

QJsonObject entryToJson(const HistoryEntry& e)
{
    return {{"key", e.key}, {"adapter", e.adapter}, {"provider", e.provider}, {"itemId", e.itemId},
            {"itemType", e.itemType}, {"itemName", e.itemName}, {"poster", e.poster}, {"videoId", e.videoId},
            {"videoTitle", e.videoTitle}, {"season", e.season}, {"episode", e.episode},
            {"source", Storage::sourceToJson(e.source)}, {"position", e.position}, {"duration", e.duration},
            {"updatedAt", double(e.updatedAt)}, {"finished", e.finished}};
}

HistoryEntry entryFromJson(const QJsonObject& o)
{
    HistoryEntry e;
    e.key = o.value("key").toString();
    e.adapter = o.value("adapter").toString();
    e.provider = o.value("provider").toString();
    e.itemId = o.value("itemId").toString();
    e.itemType = o.value("itemType").toString();
    e.itemName = o.value("itemName").toString();
    e.poster = o.value("poster").toString();
    e.videoId = o.value("videoId").toString();
    e.videoTitle = o.value("videoTitle").toString();
    e.season = o.value("season").toInt();
    e.episode = o.value("episode").toInt();
    e.source = Storage::sourceFromJson(o.value("source").toObject());
    e.position = o.value("position").toDouble();
    e.duration = o.value("duration").toDouble();
    e.updatedAt = qint64(o.value("updatedAt").toDouble());
    e.finished = o.value("finished").toBool();
    return e;
}

QJsonObject itemToJson(const LibraryItem& i)
{
    return {{"adapter", i.adapter}, {"provider", i.provider}, {"itemId", i.itemId}, {"type", i.type},
            {"name", i.name}, {"poster", i.poster}, {"releaseInfo", i.releaseInfo}, {"addedAt", double(i.addedAt)}};
}

LibraryItem itemFromJson(const QJsonObject& o)
{
    LibraryItem i;
    i.adapter = o.value("adapter").toString();
    i.provider = o.value("provider").toString();
    i.itemId = o.value("itemId").toString();
    i.type = o.value("type").toString();
    i.name = o.value("name").toString();
    i.poster = o.value("poster").toString();
    i.releaseInfo = o.value("releaseInfo").toString();
    i.addedAt = qint64(o.value("addedAt").toDouble());
    return i;
}

void writeEntry(sqlite3* db, const HistoryEntry& e)
{
    Statement s(db, "INSERT OR REPLACE INTO history(key, adapter, provider, item_id, item_type, item_name, poster,"
                    " video_id, video_title, season, episode, source, position, duration, updated_at, finished)"
                    " VALUES(?1,?2,?3,?4,?5,?6,?7,?8,?9,?10,?11,?12,?13,?14,?15,?16)");
    s.bind(1, e.key).bind(2, e.adapter).bind(3, e.provider).bind(4, e.itemId).bind(5, e.itemType)
        .bind(6, e.itemName).bind(7, e.poster).bind(8, e.videoId).bind(9, e.videoTitle).bind(10, e.season)
        .bind(11, e.episode)
        .bind(12, QString::fromUtf8(QJsonDocument(Storage::sourceToJson(e.source)).toJson(QJsonDocument::Compact)))
        .bind(13, e.position).bind(14, e.duration).bind(15, e.updatedAt).bind(16, e.finished ? 1 : 0);
    s.run();
}

void writeItem(sqlite3* db, const LibraryItem& i)
{
    Statement s(db, "INSERT OR REPLACE INTO library(key, adapter, provider, item_id, type, name, poster,"
                    " release_info, added_at) VALUES(?1,?2,?3,?4,?5,?6,?7,?8,?9)");
    s.bind(1, i.key()).bind(2, i.adapter).bind(3, i.provider).bind(4, i.itemId).bind(5, i.type).bind(6, i.name)
        .bind(7, i.poster).bind(8, i.releaseInfo).bind(9, i.addedAt);
    s.run();
}

} // namespace

// --- Worker: one thread that owns the connection ------------------------------

class Storage::Worker
{
public:
    explicit Worker(const QString& file)
        : m_file(file)
        , m_thread([this] { loop(); })
    {
    }
    ~Worker()
    {
        {
            std::lock_guard lock(m_mutex);
            m_stop = true;
        }
        m_cv.notify_all();
        m_thread.join();
    }

    void post(std::function<void(sqlite3*)> job)
    {
        {
            std::lock_guard lock(m_mutex);
            m_jobs.push_back(std::move(job));
        }
        m_cv.notify_all();
    }

    // Runs `job` on the worker and waits for it.
    void run(std::function<void(sqlite3*)> job)
    {
        std::promise<void> done;
        post([&](sqlite3* db) {
            job(db);
            done.set_value();
        });
        done.get_future().wait();
    }

private:
    void loop()
    {
        sqlite3* db = nullptr;
        const QByteArray path = m_file.isEmpty() ? QByteArray(":memory:") : QFile::encodeName(m_file);
        if (sqlite3_open_v2(path.constData(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_NOMUTEX,
                            nullptr) != SQLITE_OK) {
            qCCritical(lcStorage).noquote() << "Cannot open the database" << m_file << ":" << sqlite3_errmsg(db);
            sqlite3_close(db);
            db = nullptr;
        } else {
            // WAL: a crash or kill loses at most the write in flight, and
            // reads never wait for writes. NORMAL sync is safe with WAL
            // against app crashes, and writes far less than FULL.
            exec(db, "PRAGMA journal_mode=WAL; PRAGMA synchronous=NORMAL; PRAGMA foreign_keys=ON;");
            sqlite3_busy_timeout(db, 2000);
        }
        for (;;) {
            std::function<void(sqlite3*)> job;
            {
                std::unique_lock lock(m_mutex);
                m_cv.wait(lock, [this] { return m_stop || !m_jobs.empty(); });
                if (m_jobs.empty())
                    break;  // stopping, and every job is done
                job = std::move(m_jobs.front());
                m_jobs.pop_front();
            }
            job(db);
        }
        if (db)
            sqlite3_close(db);
    }

    QString m_file;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::deque<std::function<void(sqlite3*)>> m_jobs;
    bool m_stop = false;
    std::thread m_thread;
};

// --- Storage ---------------------------------------------------------------------

Storage::Storage(const QString& file, QObject* parent)
    : QObject(parent)
{
    if (!file.isEmpty())
        QDir().mkpath(QFileInfo(file).absolutePath());
    m_worker = std::make_unique<Worker>(file);

    // Open, migrate and load the in-memory copy: the one time the caller
    // waits for the database, at start.
    m_worker->run([this](sqlite3* db) {
        if (!db) {
            m_error = QStringLiteral("The database could not be opened.");
            return;
        }
        int version = 0;
        {
            Statement s(db, "PRAGMA user_version");
            if (s.step())
                version = sqlite3_column_int(s.get(), 0);
        }
        if (version > kSchemaVersion) {
            m_error = QStringLiteral("The database is from a newer version of the app (schema %1).").arg(version);
            return;
        }
        for (int v = version; v < kSchemaVersion; ++v) {
            exec(db, "BEGIN");
            const QByteArray bump = "PRAGMA user_version=" + QByteArray::number(v + 1);
            if (!exec(db, kMigrations[v]) || !exec(db, bump.constData())) {
                exec(db, "ROLLBACK");
                m_error = QStringLiteral("Updating the database to schema %1 failed.").arg(v + 1);
                return;
            }
            exec(db, "COMMIT");
            qCInfo(lcStorage) << "Database schema now at version" << v + 1;
        }

        {
            Statement s(db, "SELECT key, value FROM settings");
            while (s.step())
                m_settings.insert(text(s.get(), 0), settingFromText(text(s.get(), 1)));
        }
        QSet<QString> stored;
        {
            Statement s(db, "SELECT key FROM meta WHERE key LIKE 'stored.%'");
            while (s.step())
                stored.insert(text(s.get(), 0));
        }
        if (stored.contains(QStringLiteral("stored.addons"))) {
            QJsonArray list;
            Statement s(db, "SELECT transport_url, manifest FROM addons ORDER BY position");
            while (s.step())
                list.append(QJsonObject{{"transportUrl", text(s.get(), 0)},
                                        {"manifest", QJsonDocument::fromJson(text(s.get(), 1).toUtf8()).object()}});
            m_addons = list;
        }
        if (stored.contains(QStringLiteral("stored.folders"))) {
            QStringList list;
            Statement s(db, "SELECT path FROM folders ORDER BY position");
            while (s.step())
                list << text(s.get(), 0);
            m_folders = list;
        }
        {
            Statement s(db, "SELECT key, adapter, provider, item_id, item_type, item_name, poster, video_id,"
                            " video_title, season, episode, source, position, duration, updated_at, finished"
                            " FROM history ORDER BY updated_at DESC");
            while (s.step()) {
                HistoryEntry e;
                e.key = text(s.get(), 0);
                e.adapter = text(s.get(), 1);
                e.provider = text(s.get(), 2);
                e.itemId = text(s.get(), 3);
                e.itemType = text(s.get(), 4);
                e.itemName = text(s.get(), 5);
                e.poster = text(s.get(), 6);
                e.videoId = text(s.get(), 7);
                e.videoTitle = text(s.get(), 8);
                e.season = sqlite3_column_int(s.get(), 9);
                e.episode = sqlite3_column_int(s.get(), 10);
                e.source = sourceFromJson(QJsonDocument::fromJson(text(s.get(), 11).toUtf8()).object());
                e.position = sqlite3_column_double(s.get(), 12);
                e.duration = sqlite3_column_double(s.get(), 13);
                e.updatedAt = sqlite3_column_int64(s.get(), 14);
                e.finished = sqlite3_column_int(s.get(), 15) != 0;
                m_history.append(e);
            }
        }
        {
            Statement s(db, "SELECT adapter, provider, item_id, type, name, poster, release_info, added_at"
                            " FROM library ORDER BY added_at DESC");
            while (s.step()) {
                LibraryItem i;
                i.adapter = text(s.get(), 0);
                i.provider = text(s.get(), 1);
                i.itemId = text(s.get(), 2);
                i.type = text(s.get(), 3);
                i.name = text(s.get(), 4);
                i.poster = text(s.get(), 5);
                i.releaseInfo = text(s.get(), 6);
                i.addedAt = sqlite3_column_int64(s.get(), 7);
                m_library.append(i);
            }
        }
        m_open = true;
    });
    if (m_open)
        qCInfo(lcStorage).noquote() << "Database" << (file.isEmpty() ? QStringLiteral("(in memory)") : file) << ":"
                                    << m_history.size() << "history entries," << m_library.size() << "library items";
    else
        qCCritical(lcStorage).noquote() << m_error;
}

Storage::~Storage()
{
    m_worker.reset();  // runs every pending write, then closes
}

void Storage::write(std::function<void(sqlite3*)> job)
{
    if (!m_open)
        return;
    m_worker->post([job = std::move(job)](sqlite3* db) {
        if (db)
            job(db);
    });
}

void Storage::flush()
{
    m_worker->run([](sqlite3*) {});
}

// --- Settings -----------------------------------------------------------------------

QVariant Storage::setting(const QString& key, const QVariant& fallback) const
{
    return m_settings.value(key, fallback);
}

void Storage::setSetting(const QString& key, const QVariant& value)
{
    if (m_settings.value(key) == value && m_settings.contains(key))
        return;
    m_settings.insert(key, value);
    const QString t = settingToText(value);
    write([key, t](sqlite3* db) {
        Statement(db, "INSERT OR REPLACE INTO settings(key, value) VALUES(?1, ?2)").bind(1, key).bind(2, t).run();
    });
}

// --- Add-ons and folders ----------------------------------------------------------------

void Storage::saveAddons(const QJsonArray& addons)
{
    m_addons = addons;
    write([addons](sqlite3* db) {
        exec(db, "BEGIN");
        exec(db, "DELETE FROM addons");
        int position = 0;
        for (const QJsonValue& v : addons) {
            const QJsonObject o = v.toObject();
            Statement(db, "INSERT OR REPLACE INTO addons(position, transport_url, manifest) VALUES(?1, ?2, ?3)")
                .bind(1, position++)
                .bind(2, o.value("transportUrl").toString())
                .bind(3, QString::fromUtf8(QJsonDocument(o.value("manifest").toObject()).toJson(QJsonDocument::Compact)))
                .run();
        }
        exec(db, "INSERT OR REPLACE INTO meta(key, value) VALUES('stored.addons', '1')");
        exec(db, "COMMIT");
    });
}

void Storage::saveFolders(const QStringList& folders)
{
    m_folders = folders;
    write([folders](sqlite3* db) {
        exec(db, "BEGIN");
        exec(db, "DELETE FROM folders");
        int position = 0;
        for (const QString& f : folders)
            Statement(db, "INSERT OR REPLACE INTO folders(position, path) VALUES(?1, ?2)").bind(1, position++).bind(2, f).run();
        exec(db, "INSERT OR REPLACE INTO meta(key, value) VALUES('stored.folders', '1')");
        exec(db, "COMMIT");
    });
}

// --- History ----------------------------------------------------------------------------

void Storage::record(const HistoryEntry& entry)
{
    if (entry.key.isEmpty())
        return;
    HistoryEntry e = entry;
    if (e.updatedAt == 0)
        e.updatedAt = QDateTime::currentMSecsSinceEpoch();
    m_history.erase(std::remove_if(m_history.begin(), m_history.end(),
                                   [&](const HistoryEntry& h) { return h.key == e.key; }),
                    m_history.end());
    // Newest first; an import may bring older ones.
    auto at = std::find_if(m_history.begin(), m_history.end(),
                           [&](const HistoryEntry& h) { return h.updatedAt <= e.updatedAt; });
    m_history.insert(at, e);
    write([e](sqlite3* db) { writeEntry(db, e); });
    trimHistory();
    emit historyChanged();
}

void Storage::trimHistory()
{
    if (m_history.size() <= kMaxHistory)
        return;
    const qint64 cutoff = m_history.at(kMaxHistory - 1).updatedAt;
    m_history.resize(kMaxHistory);
    write([cutoff](sqlite3* db) {
        Statement(db, "DELETE FROM history WHERE key NOT IN"
                      " (SELECT key FROM history ORDER BY updated_at DESC LIMIT ?1) AND updated_at <= ?2")
            .bind(1, kMaxHistory).bind(2, cutoff).run();
    });
}

std::optional<HistoryEntry> Storage::find(const QString& key) const
{
    for (const HistoryEntry& e : m_history)
        if (e.key == key)
            return e;
    return std::nullopt;
}

std::optional<HistoryEntry> Storage::findBySource(const PlayableSource& source) const
{
    const QString key = sourceKey(source);
    for (const HistoryEntry& e : m_history)
        if (sourceKey(e.source) == key)
            return e;
    return std::nullopt;
}

void Storage::forget(const QString& itemKey)
{
    QStringList keys;
    for (const HistoryEntry& e : std::as_const(m_history))
        if (e.itemKey() == itemKey)
            keys << e.key;
    if (keys.isEmpty())
        return;
    m_history.erase(std::remove_if(m_history.begin(), m_history.end(),
                                   [&](const HistoryEntry& e) { return keys.contains(e.key); }),
                    m_history.end());
    write([keys](sqlite3* db) {
        for (const QString& k : keys)
            Statement(db, "DELETE FROM history WHERE key = ?1").bind(1, k).run();
    });
    emit historyChanged();
}

void Storage::clearHistory()
{
    m_history.clear();
    write([](sqlite3* db) { exec(db, "DELETE FROM history"); });
    emit historyChanged();
}

QList<HistoryEntry> Storage::continueWatching(int limit) const
{
    QList<HistoryEntry> out;
    QSet<QString> seen;
    for (const HistoryEntry& e : m_history) {
        if (out.size() >= limit)
            break;
        // The newest entry of each show decides: an episode just finished
        // hides the show until the next one is started (Step 7 adds "next
        // episode").
        if (seen.contains(e.itemKey()))
            continue;
        seen.insert(e.itemKey());
        if (!e.finished && e.position >= kMinResumeSeconds)
            out.append(e);
    }
    return out;
}

double Storage::resumePosition(const PlayableSource& source) const
{
    const auto e = findBySource(source);
    return e && !e->finished && e->position >= kMinResumeSeconds ? e->position : 0;
}

// --- Library -------------------------------------------------------------------------------

bool Storage::inLibrary(const QString& key) const
{
    return std::any_of(m_library.begin(), m_library.end(), [&](const LibraryItem& i) { return i.key() == key; });
}

void Storage::addToLibrary(const LibraryItem& item)
{
    if (item.itemId.isEmpty())
        return;
    LibraryItem i = item;
    if (i.addedAt == 0)
        i.addedAt = QDateTime::currentMSecsSinceEpoch();
    m_library.erase(std::remove_if(m_library.begin(), m_library.end(),
                                   [&](const LibraryItem& o) { return o.key() == i.key(); }),
                    m_library.end());
    auto at = std::find_if(m_library.begin(), m_library.end(),
                           [&](const LibraryItem& o) { return o.addedAt <= i.addedAt; });
    m_library.insert(at, i);
    write([i](sqlite3* db) { writeItem(db, i); });
    if (m_library.size() > kMaxLibrary) {
        const QString oldest = m_library.takeLast().key();
        write([oldest](sqlite3* db) { Statement(db, "DELETE FROM library WHERE key = ?1").bind(1, oldest).run(); });
    }
    emit libraryChanged();
}

void Storage::removeFromLibrary(const QString& key)
{
    const auto before = m_library.size();
    m_library.erase(std::remove_if(m_library.begin(), m_library.end(),
                                   [&](const LibraryItem& i) { return i.key() == key; }),
                    m_library.end());
    if (m_library.size() == before)
        return;
    write([key](sqlite3* db) { Statement(db, "DELETE FROM library WHERE key = ?1").bind(1, key).run(); });
    emit libraryChanged();
}

// --- Export and import -----------------------------------------------------------------------

QByteArray Storage::exportJson() const
{
    QJsonArray history;
    for (const HistoryEntry& e : m_history)
        history.append(entryToJson(e));
    QJsonArray library;
    for (const LibraryItem& i : m_library)
        library.append(itemToJson(i));
    const QJsonObject root{
        {"format", "corporal-jackie-export"},
        {"version", 1},
        {"exportedAt", QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
        {"addons", m_addons.value_or(QJsonArray())},
        {"folders", QJsonArray::fromStringList(m_folders.value_or(QStringList()))},
        {"library", library},
        {"history", history},
    };
    return QJsonDocument(root).toJson();
}

QString Storage::importJson(const QByteArray& json)
{
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    const QJsonObject root = doc.object();
    if (doc.isNull() || root.value("format").toString() != QLatin1String("corporal-jackie-export"))
        return QStringLiteral("That file is not a Corporal Jackie export.");
    if (root.value("version").toInt() > 1)
        return QStringLiteral("That export is from a newer version of the app.");

    int added = 0;
    for (const QJsonValue& v : root.value("history").toArray()) {
        const HistoryEntry e = entryFromJson(v.toObject());
        if (e.key.isEmpty())
            continue;
        const auto mine = find(e.key);
        if (!mine || mine->updatedAt < e.updatedAt) {
            record(e);
            ++added;
        }
    }
    for (const QJsonValue& v : root.value("library").toArray()) {
        const LibraryItem i = itemFromJson(v.toObject());
        if (!i.itemId.isEmpty() && !inLibrary(i.key())) {
            addToLibrary(i);
            ++added;
        }
    }
    QJsonArray addons = m_addons.value_or(QJsonArray());
    bool addonsAdded = false;
    for (const QJsonValue& v : root.value("addons").toArray()) {
        const QString url = v.toObject().value("transportUrl").toString();
        const bool have = std::any_of(addons.begin(), addons.end(), [&](const QJsonValue& a) {
            return a.toObject().value("transportUrl").toString() == url;
        });
        if (!url.isEmpty() && !have) {
            addons.append(v);
            addonsAdded = true;
        }
    }
    if (addonsAdded) {
        saveAddons(addons);
        emit addonsImported();
    }
    QStringList folders = m_folders.value_or(QStringList());
    bool foldersAdded = false;
    for (const QJsonValue& v : root.value("folders").toArray()) {
        if (const QString f = v.toString(); !f.isEmpty() && !folders.contains(f)) {
            folders << f;
            foldersAdded = true;
        }
    }
    if (foldersAdded) {
        saveFolders(folders);
        emit foldersImported();
    }
    qCInfo(lcStorage) << "Imported" << added << "history and library entries" << (addonsAdded ? "and add-ons" : "")
                      << (foldersAdded ? "and folders" : "");
    return {};
}

// --- Playable sources as JSON --------------------------------------------------------------------

QString Storage::sourceKey(const PlayableSource& source)
{
    return source.origin.isEmpty() ? source.url.toString() : source.origin;
}

QJsonObject Storage::sourceToJson(const PlayableSource& s)
{
    QJsonObject headers;
    for (auto it = s.headers.begin(); it != s.headers.end(); ++it)
        headers.insert(it.key(), it.value());
    QJsonArray subtitles;
    for (const SubtitleSource& sub : s.subtitles)
        subtitles.append(QJsonObject{{"url", sub.url.toString()}, {"language", sub.language}, {"label", sub.label}});
    const char* kinds[] = {"file", "http", "torrent"};
    QJsonObject o{{"url", s.url.toString()}, {"title", s.title}, {"kind", kinds[int(s.kind)]}};
    if (!s.origin.isEmpty())
        o.insert("origin", s.origin);
    if (!headers.isEmpty())
        o.insert("headers", headers);
    if (!subtitles.isEmpty())
        o.insert("subtitles", subtitles);
    if (!s.adapter.isEmpty())
        o.insert("adapter", s.adapter);
    return o;
}

PlayableSource Storage::sourceFromJson(const QJsonObject& o)
{
    PlayableSource s;
    s.url = QUrl(o.value("url").toString());
    s.title = o.value("title").toString();
    const QString kind = o.value("kind").toString();
    s.kind = kind == QLatin1String("torrent") ? PlayableSource::Kind::Torrent
           : kind == QLatin1String("http")    ? PlayableSource::Kind::Http
                                              : PlayableSource::Kind::LocalFile;
    s.origin = o.value("origin").toString();
    const QJsonObject headers = o.value("headers").toObject();
    for (auto it = headers.begin(); it != headers.end(); ++it)
        s.headers.insert(it.key(), it.value().toString());
    for (const QJsonValue& v : o.value("subtitles").toArray()) {
        const QJsonObject sub = v.toObject();
        s.subtitles.append({QUrl(sub.value("url").toString()), sub.value("language").toString(),
                            sub.value("label").toString()});
    }
    s.adapter = o.value("adapter").toString();
    return s;
}

} // namespace cj
