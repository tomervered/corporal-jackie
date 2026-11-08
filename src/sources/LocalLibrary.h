#pragma once
// Videos in the user's own folders, as a source adapter: two catalogs
// (movies, shows), details with episodes, and one stream per file with the
// subtitle files found next to it. Folders are scanned on a pool thread and
// watched, so new files show up without a restart.

#include "LocalNames.h"
#include "Mailbox.h"
#include "SourceAdapter.h"

#include <QDateTime>
#include <QFileSystemWatcher>
#include <QTimer>

#include <atomic>
#include <memory>

namespace cj {

struct LocalVideo
{
    QString path;
    qint64 size = 0;
    QDateTime modified;
    local::ParsedName parsed;
};

class LocalLibrary : public SourceAdapter
{
    Q_OBJECT

public:
    static constexpr int kMaxFiles = 50000;       // per scan, over all folders
    static constexpr int kMaxDepth = 8;
    static constexpr int kMaxWatchedDirs = 2000;  // inotify and kqueue have limits
    static constexpr int kPageSize = 100;

    // storeFile: a JSON file the folder list is kept in. Empty: no file; the
    // app keeps the list in its database through folders(), restoreFolders()
    // and foldersChanged.
    explicit LocalLibrary(const QString& storeFile = {}, QObject* parent = nullptr);
    ~LocalLibrary() override;

    QString id() const override { return QStringLiteral("local"); }
    QString name() const override { return QStringLiteral("This computer"); }

    QList<CatalogRef> catalogs() const override;
    void fetchCatalog(const CatalogRef& catalog, const QString& search, int skip, CatalogDone done) override;
    void fetchMeta(const MetaPreview& item, MetaDone done) override;
    void fetchStreams(const QString& type, const QString& videoId, StreamsPart part,
                      std::function<void()> finished) override;

    QStringList folders() const { return m_folders; }
    bool addFolder(const QString& path);
    bool removeFolder(const QString& path);
    // Replaces the folder list with a stored one and scans it.
    void restoreFolders(const QStringList& folders);
    void rescan();

    bool scanning() const { return m_scanning; }
    QList<LocalVideo> videos() const { return m_videos; }

    // Changes on disk are picked up after this quiet time.
    void setRescanDelay(int ms) { m_rescanTimer.setInterval(ms); }

signals:
    void foldersChanged();
    void scanningChanged();
    void libraryChanged();

private:
    struct ScanResult
    {
        QList<LocalVideo> videos;
        QStringList dirs;
        bool truncated = false;
        quint64 generation = 0;
    };
    static ScanResult scan(const QStringList& folders, const std::shared_ptr<std::atomic<bool>>& cancel);
    void applyScan(ScanResult result);
    void save() const;
    static QString seriesKey(const QString& title);

    QString m_storeFile;
    QStringList m_folders;
    QList<LocalVideo> m_videos;
    bool m_scanning = false;
    quint64 m_generation = 0;
    std::shared_ptr<std::atomic<bool>> m_cancel;
    QFileSystemWatcher m_watcher;
    QTimer m_rescanTimer;
    Mailbox m_mailbox;
};

} // namespace cj
