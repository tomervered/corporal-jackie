#include "LocalLibrary.h"

#include "Logging.h"
#include "StreamRanking.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QRegularExpression>
#include <QSaveFile>

#include <algorithm>

namespace cj {
namespace {

const QString kMoviePrefix = QStringLiteral("local:");
const QString kSeriesPrefix = QStringLiteral("localseries:");
const QRegularExpression kSampleName(QStringLiteral(R"(\b(sample|trailer)\b)"), QRegularExpression::CaseInsensitiveOption);

QString clean(const QString& path)
{
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

bool insideFolder(const QString& path, const QString& folder)
{
    return path == folder || path.startsWith(folder.endsWith(QLatin1Char('/')) ? folder : folder + QLatin1Char('/'));
}

QString displayTitle(const local::ParsedName& p)
{
    return p.year > 0 ? QStringLiteral("%1 (%2)").arg(p.title).arg(p.year) : p.title;
}

} // namespace

LocalLibrary::LocalLibrary(const QString& storeFile, QObject* parent)
    : SourceAdapter(parent)
    , m_storeFile(storeFile)
{
    m_rescanTimer.setSingleShot(true);
    m_rescanTimer.setInterval(2000);
    connect(&m_rescanTimer, &QTimer::timeout, this, &LocalLibrary::rescan);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this] { m_rescanTimer.start(); });

    QFile file(m_storeFile);
    if (file.open(QIODevice::ReadOnly)) {
        for (const QJsonValue& v : QJsonDocument::fromJson(file.read(1024 * 1024)).object().value("folders").toArray())
            if (const QString f = v.toString(); !f.isEmpty() && !m_folders.contains(f))
                m_folders << f;
    }
    if (!m_folders.isEmpty())
        rescan();
}

LocalLibrary::~LocalLibrary()
{
    if (m_cancel)
        *m_cancel = true;
}

void LocalLibrary::save() const
{
    QDir().mkpath(QFileInfo(m_storeFile).absolutePath());
    QSaveFile file(m_storeFile);
    if (!file.open(QIODevice::WriteOnly)
        || file.write(QJsonDocument(QJsonObject{{"version", 1}, {"folders", QJsonArray::fromStringList(m_folders)}}).toJson()) < 0
        || !file.commit())
        qCWarning(lcSources).noquote() << "Could not save" << m_storeFile << file.errorString();
}

bool LocalLibrary::addFolder(const QString& path)
{
    const QString folder = clean(path);
    if (folder.isEmpty() || !QFileInfo(folder).isDir())
        return false;
    for (const QString& f : std::as_const(m_folders))
        if (insideFolder(folder, f))
            return false;  // already covered
    // A parent replaces the folders inside it.
    m_folders.erase(std::remove_if(m_folders.begin(), m_folders.end(),
                                   [&](const QString& f) { return insideFolder(f, folder); }),
                    m_folders.end());
    m_folders << folder;
    save();
    emit foldersChanged();
    rescan();
    return true;
}

bool LocalLibrary::removeFolder(const QString& path)
{
    if (!m_folders.removeOne(path) && !m_folders.removeOne(clean(path)))
        return false;
    save();
    emit foldersChanged();
    rescan();
    return true;
}

void LocalLibrary::rescan()
{
    m_rescanTimer.stop();
    if (m_cancel)
        *m_cancel = true;  // a newer scan replaces one still running
    m_cancel = std::make_shared<std::atomic<bool>>(false);
    const quint64 generation = ++m_generation;
    if (!m_scanning) {
        m_scanning = true;
        emit scanningChanged();
    }
    const QStringList folders = m_folders;
    auto cancel = m_cancel;
    m_mailbox.run(
        this,
        [folders, cancel, generation] {
            ScanResult r = scan(folders, cancel);
            r.generation = generation;
            return r;
        },
        [this](ScanResult r) { applyScan(std::move(r)); });
}

LocalLibrary::ScanResult LocalLibrary::scan(const QStringList& folders, const std::shared_ptr<std::atomic<bool>>& cancel)
{
    ScanResult r;
    for (const QString& folder : folders) {
        r.dirs << folder;
        QDirIterator it(folder, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Readable,
                        QDirIterator::Subdirectories);  // no symlinks followed: no loops
        const int baseDepth = int(folder.count(QLatin1Char('/')));
        while (it.hasNext()) {
            if (*cancel)
                return r;
            it.next();
            const QFileInfo info = it.fileInfo();
            if (info.fileName().startsWith(QLatin1Char('.')))
                continue;
            if (info.isDir()) {
                if (info.filePath().count(QLatin1Char('/')) - baseDepth <= kMaxDepth && r.dirs.size() < kMaxWatchedDirs)
                    r.dirs << info.filePath();
                continue;
            }
            if (!local::isVideoFile(info.fileName()))
                continue;
            if (info.filePath().count(QLatin1Char('/')) - baseDepth > kMaxDepth + 1)
                continue;
            // Samples and trailers that ship with releases are not the video.
            if (info.size() < 50 * 1024 * 1024
                && info.completeBaseName().contains(kSampleName))
                continue;
            LocalVideo v;
            v.path = info.absoluteFilePath();
            v.size = info.size();
            v.modified = info.lastModified();
            v.parsed = local::parseVideoPath(v.path);
            r.videos << v;
            if (r.videos.size() >= kMaxFiles) {
                r.truncated = true;
                return r;
            }
        }
    }
    return r;
}

void LocalLibrary::applyScan(ScanResult r)
{
    if (r.generation != m_generation)
        return;  // a newer scan is on its way
    m_videos = std::move(r.videos);
    if (r.truncated)
        qCWarning(lcSources) << "Local scan stopped at" << kMaxFiles << "videos";
    qCInfo(lcSources) << "Local scan found" << m_videos.size() << "videos in" << m_folders.size() << "folders";

    // Watch the folders that exist now, and nothing else.
    const QStringList watched = m_watcher.directories();
    if (!watched.isEmpty())
        m_watcher.removePaths(watched);
    if (!r.dirs.isEmpty())
        m_watcher.addPaths(r.dirs);

    m_scanning = false;
    emit scanningChanged();
    emit libraryChanged();
    emit catalogsChanged();
}

QString LocalLibrary::seriesKey(const QString& title)
{
    return title.toCaseFolded().remove(QRegularExpression(QStringLiteral("[^\\w]")));
}

QList<CatalogRef> LocalLibrary::catalogs() const
{
    if (m_folders.isEmpty())
        return {};
    CatalogRef movies;
    movies.adapter = id();
    movies.id = QStringLiteral("movies");
    movies.type = QStringLiteral("movie");
    movies.name = QStringLiteral("This computer · Movies");
    movies.searchable = true;
    CatalogRef shows = movies;
    shows.id = QStringLiteral("series");
    shows.type = QStringLiteral("series");
    shows.name = QStringLiteral("This computer · Shows");
    return {movies, shows};
}

void LocalLibrary::fetchCatalog(const CatalogRef& catalog, const QString& search, int skip, CatalogDone done)
{
    const QString needle = search.trimmed();
    QList<MetaPreview> items;
    if (catalog.type == QLatin1String("movie")) {
        for (const LocalVideo& v : std::as_const(m_videos)) {
            if (v.parsed.isEpisode())
                continue;
            MetaPreview m;
            m.adapter = id();
            m.id = kMoviePrefix + v.path;
            m.type = QStringLiteral("movie");
            m.name = v.parsed.title;
            m.releaseInfo = v.parsed.year > 0 ? QString::number(v.parsed.year) : QString();
            m.description = QDir::toNativeSeparators(v.path);
            items << m;
        }
    } else {
        QHash<QString, int> seen;
        for (const LocalVideo& v : std::as_const(m_videos)) {
            if (!v.parsed.isEpisode())
                continue;
            const QString key = seriesKey(v.parsed.title);
            if (seen.contains(key))
                continue;
            seen.insert(key, int(items.size()));
            MetaPreview m;
            m.adapter = id();
            m.id = kSeriesPrefix + key;
            m.type = QStringLiteral("series");
            m.name = v.parsed.title;
            m.releaseInfo = v.parsed.year > 0 ? QString::number(v.parsed.year) : QString();
            items << m;
        }
    }
    if (!needle.isEmpty())
        items.removeIf([&](const MetaPreview& m) { return !m.name.contains(needle, Qt::CaseInsensitive); });
    std::stable_sort(items.begin(), items.end(), [](const MetaPreview& a, const MetaPreview& b) {
        return QString::localeAwareCompare(a.name, b.name) < 0;
    });
    items = items.mid(skip, kPageSize);
    QTimer::singleShot(0, this, [done, items] { done(items, {}); });
}

void LocalLibrary::fetchMeta(const MetaPreview& item, MetaDone done)
{
    MetaDetail d;
    d.preview = item;
    if (item.id.startsWith(kSeriesPrefix)) {
        const QString key = item.id.mid(kSeriesPrefix.size());
        for (const LocalVideo& v : std::as_const(m_videos)) {
            if (!v.parsed.isEpisode() || seriesKey(v.parsed.title) != key)
                continue;
            VideoEntry e;
            e.id = kMoviePrefix + v.path;
            e.season = v.parsed.season;
            e.episode = v.parsed.episode;
            e.title = v.parsed.episodeTitle.isEmpty() ? QFileInfo(v.path).completeBaseName() : v.parsed.episodeTitle;
            d.videos << e;
        }
        std::stable_sort(d.videos.begin(), d.videos.end(), [](const VideoEntry& a, const VideoEntry& b) {
            return a.season != b.season ? a.season < b.season : a.episode < b.episode;
        });
    }
    QTimer::singleShot(0, this, [done, d] { done(d, {}); });
}

void LocalLibrary::fetchStreams(const QString& /*type*/, const QString& videoId, StreamsPart part,
                                std::function<void()> finished)
{
    if (!videoId.startsWith(kMoviePrefix)) {
        QTimer::singleShot(0, this, finished);
        return;
    }
    const QString path = videoId.mid(kMoviePrefix.size());
    // Only files from the scan: an id is not a way to open any path.
    const auto it = std::find_if(m_videos.cbegin(), m_videos.cend(), [&](const LocalVideo& v) { return v.path == path; });
    if (it == m_videos.cend()) {
        QTimer::singleShot(0, this, [this, part, finished] {
            part({}, name(), QStringLiteral("the file is no longer in your folders"));
            finished();
        });
        return;
    }
    const LocalVideo video = *it;
    m_mailbox.run(
        this,
        [video] {
            StreamOption s;
            s.source = PlayableSource::fromUrl(QUrl::fromLocalFile(video.path),
                                               video.parsed.isEpisode()
                                                   ? QStringLiteral("%1 %2x%3").arg(video.parsed.title).arg(video.parsed.season).arg(video.parsed.episode, 2, 10, QLatin1Char('0'))
                                                   : displayTitle(video.parsed));
            s.source.adapter = QStringLiteral("local");
            s.source.subtitles = local::findSubtitlesFor(video.path);  // reads the folder: off the UI thread
            s.providerName = QStringLiteral("This computer");
            s.name = QStringLiteral("Local file");
            s.description = QFileInfo(video.path).fileName();
            s.quality = video.parsed.quality;
            s.sizeBytes = video.size;
            return s;
        },
        [this, part, finished](StreamOption s) {
            part({s}, name(), {});
            finished();
        });
}

} // namespace cj
