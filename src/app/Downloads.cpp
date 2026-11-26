#include "Downloads.h"

#include "CatalogBrowser.h"
#include "Caches.h"
#include "DownloadFiles.h"
#include "LinkRules.h"
#include "Logging.h"
#include "Preferences.h"
#include "PlaybackSession.h"
#include "LibraryDatabase.h"
#include "StreamBroker.h"
#include "TorrentDownloader.h"
#include "TorrentTransport.h"

#include <QDesktopServices>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QStorageInfo>

#include <atomic>
#include <thread>

namespace cj {

namespace {

enum class State { Starting, Running, Seeding, Done, Failed, Cancelled };
enum class Mode { Stream, Normal, Web };

constexpr auto kListKey = "downloads/list";
constexpr int kMaxSavedDownloads = 200;
constexpr qint64 kChunk = 1024 * 1024;

QString stateName(State s)
{
    switch (s) {
    case State::Starting:  return QStringLiteral("starting");
    case State::Running:   return QStringLiteral("downloading");
    case State::Seeding:   return QStringLiteral("seeding");
    case State::Done:      return QStringLiteral("done");
    case State::Failed:    return QStringLiteral("failed");
    case State::Cancelled: return QStringLiteral("cancelled");
    }
    return {};
}

QString modeName(Mode m)
{
    return m == Mode::Stream ? QStringLiteral("stream") : m == Mode::Normal ? QStringLiteral("normal") : QStringLiteral("web");
}

Mode modeFromName(const QString& name)
{
    return name == QLatin1String("stream") ? Mode::Stream : name == QLatin1String("normal") ? Mode::Normal : Mode::Web;
}

QString sizeText(qint64 bytes)
{
    if (bytes < 1024 * 1024)
        return QStringLiteral("%1 KB").arg(bytes / 1024);
    return Caches::describe(bytes);
}

// An extension for a web video that names none, from its type.
QString extensionFor(const QString& contentType)
{
    const QString t = contentType.section(QLatin1Char(';'), 0, 0).trimmed().toLower();
    if (t == QLatin1String("video/x-matroska")) return QStringLiteral(".mkv");
    if (t == QLatin1String("video/webm")) return QStringLiteral(".webm");
    if (t == QLatin1String("video/quicktime")) return QStringLiteral(".mov");
    if (t == QLatin1String("video/x-msvideo")) return QStringLiteral(".avi");
    if (t == QLatin1String("video/mp2t")) return QStringLiteral(".ts");
    return QStringLiteral(".mp4");
}

bool isPlaylist(const QUrl& url, const QString& contentType)
{
    const QString path = url.path().toLower();
    const QString type = contentType.toLower();
    return path.endsWith(QLatin1String(".m3u8")) || path.endsWith(QLatin1String(".mpd"))
        || type.contains(QLatin1String("mpegurl")) || type.contains(QLatin1String("dash+xml"));
}

} // namespace

struct Downloads::Job
{
    int id = 0;
    bool torrent = false;
    Mode mode = Mode::Web;
    QString hash;               // torrents: the info hash
    int engineId = 0;           // normal torrents: the TorrentDownloader's id
    std::atomic<bool> sharing{false};
    std::atomic<qint64> uploaded{0};
    std::atomic<State> state{State::Starting};
    std::atomic<qint64> done{0};
    std::atomic<qint64> size{-1};
    std::atomic<qint64> rate{0};
    std::atomic<int> peers{0};
    std::atomic<bool> cancelled{false};

    mutable std::mutex mutex;   // guards the strings and the handle
    QString name;
    QString error;
    QString path;               // the final file, once done
    QString part;               // the file being written
    std::shared_ptr<StreamHandle> handle;
    std::thread thread;

    // Web links, on the UI thread only.
    QPointer<QNetworkReply> reply;
    std::unique_ptr<QFile> file;
    QElapsedTimer clock;
    qint64 rateMark = 0;

    QString text(QString Job::*field) const
    {
        std::lock_guard lock(mutex);
        return this->*field;
    }
    void set(QString Job::*field, const QString& value)
    {
        std::lock_guard lock(mutex);
        this->*field = value;
    }
};

Downloads::Downloads(LibraryDatabase& storage, Preferences& preferences, TorrentTransport* torrents,
                     const QString& defaultFolder, QObject* parent)
    : QObject(parent)
    , m_storage(storage)
    , m_preferences(preferences)
    , m_torrents(torrents)
    , m_defaultFolder(defaultFolder)
{
    m_folder = storage.setting(QString::fromLatin1(kFolderKey), QString()).toString();
    m_normal = storage.setting(QString::fromLatin1(kNormalKey), false).toBool();
    m_refresh.setInterval(500);
    connect(&m_refresh, &QTimer::timeout, this, &Downloads::refresh);
    load();
}

Downloads::~Downloads() { stop(); }

// The list of downloads is kept between runs (names, sizes, where the files
// are), unless "Remember what I watch" is off. One that was still running
// when the app closed comes back as interrupted: it is not resumed.
void Downloads::save()
{
    QJsonArray list;
    if (m_storage.keepsHistory()) {
        const size_t first = m_jobs.size() > size_t(kMaxSavedDownloads) ? m_jobs.size() - kMaxSavedDownloads : 0;
        for (size_t i = first; i < m_jobs.size(); ++i) {
            const Job& j = *m_jobs[i];
            const State s = j.state;
            const bool unfinished = s == State::Starting || s == State::Running;
            list.append(QJsonObject{{QStringLiteral("name"), j.text(&Job::name)},
                                    {QStringLiteral("state"), unfinished ? QStringLiteral("interrupted")
                                                              : s == State::Seeding ? stateName(State::Done) : stateName(s)},
                                    {QStringLiteral("path"), j.text(&Job::path)},
                                    {QStringLiteral("error"), j.text(&Job::error)},
                                    {QStringLiteral("size"), qint64(j.size)},
                                    {QStringLiteral("torrent"), j.torrent},
                                    {QStringLiteral("mode"), modeName(j.mode)}});
        }
    }
    m_storage.setSetting(QString::fromLatin1(kListKey),
                         list.isEmpty() ? QString() : QString::fromUtf8(QJsonDocument(list).toJson(QJsonDocument::Compact)));
}

void Downloads::load()
{
    if (!m_storage.keepsHistory()) {
        qCInfo(lcApp) << "Downloads: the list is not kept (\"Remember what I watch\" is off)";
        return;
    }
    const QJsonArray list = QJsonDocument::fromJson(m_storage.setting(QString::fromLatin1(kListKey)).toString().toUtf8()).array();
    for (const QJsonValue& value : list) {
        const QJsonObject o = value.toObject();
        auto job = std::make_shared<Job>();
        job->id = m_nextId++;
        job->torrent = o.value(QStringLiteral("torrent")).toBool();
        job->mode = modeFromName(o.value(QStringLiteral("mode")).toString());
        job->size = qint64(o.value(QStringLiteral("size")).toDouble(-1));
        job->set(&Job::name, o.value(QStringLiteral("name")).toString());
        const QString path = o.value(QStringLiteral("path")).toString();
        const QString state = o.value(QStringLiteral("state")).toString();
        if (state == QLatin1String("done") && QFileInfo::exists(path)) {
            job->set(&Job::path, path);
            job->done = job->size.load();
            job->state = State::Done;
        } else if (state == QLatin1String("done")) {
            job->set(&Job::error, tr("The file is no longer there."));
            job->state = State::Failed;
        } else if (state == QLatin1String("cancelled")) {
            job->state = State::Cancelled;
        } else {
            job->set(&Job::error, state == QLatin1String("interrupted")
                                      ? tr("Interrupted: the app was closed before it finished.")
                                      : o.value(QStringLiteral("error")).toString());
            job->state = State::Failed;
        }
        m_jobs.push_back(job);
    }
    qCInfo(lcApp) << "Downloads: list restored with" << m_jobs.size() << "entries";
}

QString Downloads::effectiveFolder() const { return m_folder.isEmpty() ? m_defaultFolder : m_folder; }

QString Downloads::folder() const { return QDir::toNativeSeparators(effectiveFolder()); }

void Downloads::setFolder(const QUrl& folder)
{
    const QString path = folder.isLocalFile() ? folder.toLocalFile() : folder.toString();
    if (path.isEmpty() || !QDir().mkpath(path)) {
        emit notice(tr("That folder can't be used for downloads."), true);
        return;
    }
    m_folder = QDir::cleanPath(path);
    m_storage.setSetting(QString::fromLatin1(kFolderKey), m_folder);
    emit folderChanged();
}

void Downloads::resetFolder()
{
    m_folder.clear();
    m_storage.setSetting(QString::fromLatin1(kFolderKey), QString());
    emit folderChanged();
}

void Downloads::setNormalTorrents(bool on)
{
    if (on == m_normal)
        return;
    m_normal = on;
    m_storage.setSetting(QString::fromLatin1(kNormalKey), on);
    emit normalTorrentsChanged();
}

void Downloads::setNetwork(const BrokerSettings& settings)
{
    m_netSettings = settings;
    if (m_normalEngine)
        m_normalEngine->applyNetwork(settings);
}

void Downloads::openFolder() const
{
    QDir().mkpath(effectiveFolder());
    QDesktopServices::openUrl(QUrl::fromLocalFile(effectiveFolder()));
}

bool Downloads::canDownload(const PlayableSource& source)
{
    if (source.kind == PlayableSource::Kind::Torrent)
        return true;
    return source.kind == PlayableSource::Kind::Http && links::isWebLink(source.url);
}

bool Downloads::canDownloadStream(int streamIndex) const
{
    if (!m_browser)
        return false;
    const QList<StreamOption> streams = m_browser->streamList();
    return streamIndex >= 0 && streamIndex < streams.size() && canDownload(streams[streamIndex].source);
}

bool Downloads::canDownloadPlaying() const
{
    return m_session && !m_session->source().isEmpty() && canDownload(m_session->playableSource());
}

QString Downloads::downloadStream(int streamIndex)
{
    if (!canDownloadStream(streamIndex))
        return tr("This one can't be downloaded.");
    return start(m_browser->streamList()[streamIndex].source);
}

QString Downloads::downloadPlaying()
{
    if (!canDownloadPlaying())
        return tr("What is playing can't be downloaded.");
    return start(m_session->playableSource());
}

QString Downloads::start(const PlayableSource& source)
{
    if (m_stopped)
        return tr("The app is closing.");
    if (!canDownload(source))
        return tr("This one can't be downloaded.");
    if (!QDir().mkpath(effectiveFolder()))
        return tr("The download folder can't be written to. Pick another in Settings.");
    const QString error = source.kind == PlayableSource::Kind::Torrent ? startTorrent(source) : startWeb(source);
    if (error.isEmpty()) {
        m_refresh.start();
        emit itemsChanged();
    }
    return error;
}

// --- Torrents: one reader from start to end, on a thread of its own ----------------

QString Downloads::startTorrent(const PlayableSource& source)
{
    if (!m_torrents)
        return tr("Torrents are not available in this build.");
    if (!m_preferences.useTorrents())
        return tr("Torrent playback is off. Turn it on in Settings to download torrents.");

    // A magnet link, or the torrent:// URL a magnet became, with the text it
    // came from (its trackers and peers) when known.
    QString uri = source.origin;
    int fileIndex = -1;
    QUrl url = source.url;
    if (url.scheme() == QLatin1String("magnet")) {
        uri = url.toString(QUrl::FullyEncoded);
        const std::optional<PlayableSource> resolved = m_torrents->resolve(uri);
        if (!resolved)
            return tr("This torrent link can't be read.");
        url = resolved->url;
    }
    const QString hash = TorrentTransport::infoHashOf(url);
    if (hash.isEmpty())
        return tr("This torrent link can't be read.");
    if (uri.isEmpty())
        uri = hash;
    bool ok = false;
    const int index = url.path().mid(1).toInt(&ok);
    if (ok)
        fileIndex = index;

    for (const auto& j : m_jobs)
        if (j->torrent && j->hash == hash && (j->state == State::Starting || j->state == State::Running))
            return tr("This torrent is already downloading.");

    auto job = std::make_shared<Job>();
    job->id = m_nextId++;
    job->torrent = true;
    job->hash = hash;
    job->name = source.title.isEmpty() ? hash.left(12) : source.title;
    job->sharing = m_preferences.allowUploads();
    if (m_normal)
        return startNormal(job, uri, fileIndex);
    job->mode = Mode::Stream;
    m_jobs.push_back(job);
    qCInfo(lcApp) << "Download" << job->id << "started (torrent)";

    OpenRequest request;
    request.uri = uri;
    request.fileIndex = fileIndex;
    const QString folder = effectiveFolder();
    job->thread = std::thread([this, job, request, folder] {
        saveTorrentToFolder(job, request, folder);
        QMetaObject::invokeMethod(this, [this, job] { finish(job); }, Qt::QueuedConnection);
    });
    return {};
}

void Downloads::saveTorrentToFolder(const std::shared_ptr<Job>& job, const OpenRequest& request, const QString& folder)
{
    auto fail = [&](const QString& why) {
        if (!job->cancelled)
            job->set(&Job::error, why);
        job->state = job->cancelled ? State::Cancelled : State::Failed;
    };

    if (job->sharing)
        m_torrents->broker().setSharing(job->hash, true);
    BrokerError error = BrokerError::None;
    std::shared_ptr<StreamHandle> handle =
        m_torrents->broker().open(request, &error, [job] { return job->cancelled.load(); });
    if (!handle) {
        fail(error == BrokerError::MetadataTimeout ? tr("Nobody sharing this torrent was found in time.")
             : error == BrokerError::NoSuchFile    ? tr("That file is not in this torrent.")
             : error == BrokerError::InvalidUri    ? tr("This torrent link can't be read.")
                                                   : tr("The torrent stopped with an error."));
        return;
    }
    {
        std::lock_guard lock(job->mutex);
        job->handle = handle;
    }
    if (job->cancelled)
        handle->cancel();
    const qint64 size = handle->size();
    job->size = size;
    const QString name = safeFileName(handle->fileName());
    job->set(&Job::name, name);

    const QStorageInfo disk(folder);
    if (disk.isValid() && disk.bytesAvailable() >= 0 && disk.bytesAvailable() < size) {
        fail(tr("Not enough free space in the download folder (%1 needed, %2 free).")
                 .arg(sizeText(size), sizeText(disk.bytesAvailable())));
        handle->close();
        return;
    }
    const QString part = downloadFiles::begin(folder, name);
    if (part.isEmpty()) {
        fail(tr("The download folder can't be written to."));
        handle->close();
        return;
    }
    job->set(&Job::part, part);
    job->state = State::Running;

    const QString copyError = copyTorrentBytes(*job, *handle, part, size);
    handle->close();
    {
        std::lock_guard lock(job->mutex);
        job->handle.reset();
    }
    if (job->cancelled || !copyError.isEmpty()) {
        QFile::remove(part);
        fail(copyError);
    } else {
        const QString path = downloadFiles::finish(part);
        if (path.isEmpty()) {
            QFile::remove(part);
            fail(tr("Could not name the finished file."));
        } else {
            job->set(&Job::path, path);
            job->set(&Job::name, QFileInfo(path).fileName());
            job->state = State::Done;
        }
    }
}

QString Downloads::copyTorrentBytes(Job& job, StreamHandle& stream, const QString& part, qint64 size)
{
    QFile out(part);
    bool written = out.open(QIODevice::WriteOnly | QIODevice::Truncate);
    std::vector<char> buffer(static_cast<std::size_t>(kChunk));
    qint64 offset = 0;
    QElapsedTimer statsClock;
    statsClock.start();
    while (written && offset < size && !job.cancelled) {
        const qint64 readResult = stream.read(offset, buffer.data(), qMin(kChunk, size - offset));
        if (readResult <= 0)
            break;
        if (out.write(buffer.data(), readResult) != readResult) {
            written = false;
            break;
        }
        offset += readResult;
        job.done = offset;
        if (statsClock.elapsed() >= 1000) {
            statsClock.restart();
            const StreamStats st = stream.stats();
            job.rate = st.downloadRate;
            job.peers = st.peers;
            job.uploaded = st.uploadedBytes;
        }
    }
    if (!written || !out.flush())
        return tr("Could not write to the download folder (is the disk full?).");
    if (offset < size)
        return tr("The torrent stopped with an error.");
    return {};
}

// --- Normal torrents: the TorrentDownloader, polled by the refresh timer ---------------

QString Downloads::startNormal(const std::shared_ptr<Job>& job, const QString& uri, int fileIndex)
{
    job->mode = Mode::Normal;
    if (!m_normalEngine) {
        m_normalEngine = std::make_unique<TorrentDownloader>(m_netSettings);
    }
    BrokerError error = BrokerError::None;
    job->engineId = m_normalEngine->add(uri, fileIndex, effectiveFolder(), job->sharing, &error);
    if (job->engineId == 0)
        return error == BrokerError::InvalidUri ? tr("This torrent link can't be read.")
                                                : tr("This torrent can't be downloaded right now.");
    m_jobs.push_back(job);
    qCInfo(lcApp) << "Download" << job->id << "started (normal torrent)";
    return {};
}

// --- Web links, on the UI thread with Qt's network stack ----------------------------

namespace {

// The name to save under: the link's own file name if it has an extension,
// else the title. `*fromLink` says which one it was; when it was not, the
// extension comes later, from the server's content type.
QString webFileName(const PlayableSource& source, bool* fromLink)
{
    const QString name = Downloads::safeFileName(QUrl::fromPercentEncoding(source.url.fileName().toUtf8()));
    *fromLink = !QFileInfo(name).suffix().isEmpty() && name != QLatin1String("video");
    return *fromLink ? name : Downloads::safeFileName(source.title);
}

QNetworkRequest webRequest(const PlayableSource& source)
{
    QNetworkRequest request(source.url);
    for (auto it = source.headers.cbegin(); it != source.headers.cend(); ++it)
        request.setRawHeader(it.key().toUtf8(), it.value().toUtf8());
    // Headers and a user:password are for the server the link names: a
    // redirect elsewhere is refused rather than handed them.
    const bool hasSecret = !source.headers.isEmpty() || !source.url.userInfo().isEmpty();
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         hasSecret ? QNetworkRequest::SameOriginRedirectPolicy : QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(30000);   // nothing arriving for 30 s
    return request;
}

// The server's first answer is in: refuse a playlist, note the size, and
// add the extension if the link had none.
void onWebAnswerStarted(Downloads::Job& job, QNetworkReply& reply, bool nameCameFromLink)
{
    const QString type = reply.header(QNetworkRequest::ContentTypeHeader).toString();
    if (isPlaylist(reply.url(), type)) {
        job.set(&Downloads::Job::error, Downloads::tr("This link is a streaming playlist, which can't be saved as one file."));
        reply.abort();
        return;
    }
    const qint64 length = reply.header(QNetworkRequest::ContentLengthHeader).toLongLong();
    if (length > 0)
        job.size = length;
    if (!nameCameFromLink)
        job.set(&Downloads::Job::name, job.text(&Downloads::Job::name) + extensionFor(type));
}

// More bytes arrived: write them to the file, and refresh the speed once a second.
void onWebBytesArrived(Downloads::Job& job, QNetworkReply& reply)
{
    const QByteArray bytes = reply.readAll();
    if (!job.file || job.file->write(bytes) != bytes.size()) {
        job.set(&Downloads::Job::error, Downloads::tr("Could not write to the download folder (is the disk full?)."));
        reply.abort();
        return;
    }
    job.done += bytes.size();
    if (job.clock.elapsed() >= 1000) {
        job.rate = (job.done - job.rateMark) * 1000 / qMax<qint64>(1, job.clock.restart());
        job.rateMark = job.done;
    }
}

// The transfer is over: write the last bytes (if it ended well) and close the file.
void closeWebFile(Downloads::Job& job, QNetworkReply& reply)
{
    if (!job.file)
        return;
    if (reply.error() == QNetworkReply::NoError) {
        const QByteArray rest = reply.readAll();
        if (job.file->write(rest) != rest.size())
            job.set(&Downloads::Job::error, Downloads::tr("Could not write to the download folder (is the disk full?)."));
        job.done += rest.size();
    }
    job.file->close();
    job.file.reset();
}

QString webFailureMessage(const QNetworkReply& reply)
{
    const int status = reply.attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const bool refusedRedirect = reply.error() == QNetworkReply::InsecureRedirectError
                                 || reply.error() == QNetworkReply::TooManyRedirectsError;
    if (refusedRedirect)
        return Downloads::tr("The server sent the download somewhere else, which was refused.");
    if (status > 0)
        return Downloads::tr("The server answered %1.").arg(status);
    return Downloads::tr("The download stopped: %1.").arg(reply.errorString());
}

// Decides how the download ended: cancelled, failed (the half file is
// deleted) or done (the half file gets its final name).
void settleWebJob(Downloads::Job& job, const QNetworkReply& reply)
{
    using Job = Downloads::Job;
    const QString part = job.text(&Job::part);
    if (job.cancelled) {
        QFile::remove(part);
        job.state = State::Cancelled;
        return;
    }
    if (reply.error() != QNetworkReply::NoError || !job.text(&Job::error).isEmpty()) {
        QFile::remove(part);
        if (job.text(&Job::error).isEmpty())
            job.set(&Job::error, webFailureMessage(reply));
        job.state = State::Failed;
        return;
    }
    const QString path = downloadFiles::finish(part, job.text(&Job::name));
    if (path.isEmpty()) {
        QFile::remove(part);
        job.set(&Job::error, Downloads::tr("Could not name the finished file."));
        job.state = State::Failed;
        return;
    }
    job.set(&Job::path, path);
    job.set(&Job::name, QFileInfo(path).fileName());
    job.size = job.done.load();
    job.state = State::Done;
}

} // namespace

// Starts saving a web link to a file. Returns "" or why it could not start.
QString Downloads::startWeb(const PlayableSource& source)
{
    if (isPlaylist(source.url, {}))
        return tr("This link is a streaming playlist, which can't be saved as one file.");
    if (!m_network)
        m_network = new QNetworkAccessManager(this);

    bool nameCameFromLink = false;
    const QString name = webFileName(source, &nameCameFromLink);
    const QString part = downloadFiles::begin(effectiveFolder(), name);
    if (part.isEmpty())
        return tr("The download folder can't be written to.");

    auto job = std::make_shared<Job>();
    job->id = m_nextId++;
    job->name = name;
    job->part = part;
    job->file = std::make_unique<QFile>(part);
    if (!job->file->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QFile::remove(part);
        return tr("The download folder can't be written to.");
    }
    m_jobs.push_back(job);
    qCInfo(lcApp) << "Download" << job->id << "started (web)";

    QNetworkReply* reply = m_network->get(webRequest(source));
    job->reply = reply;
    job->clock.start();
    job->state = State::Running;

    connect(reply, &QNetworkReply::metaDataChanged, this,
            [job, reply, nameCameFromLink] { onWebAnswerStarted(*job, *reply, nameCameFromLink); });
    connect(reply, &QNetworkReply::readyRead, this, [job, reply] { onWebBytesArrived(*job, *reply); });
    connect(reply, &QNetworkReply::finished, this, [this, job, reply] {
        reply->deleteLater();
        closeWebFile(*job, *reply);
        settleWebJob(*job, *reply);
        finish(job);
    });
    return {};
}

// --- Both ---------------------------------------------------------------------------

void Downloads::finish(const std::shared_ptr<Job>& job)
{
    if (job->thread.joinable())
        job->thread.join();
    // A streamable download shares only while it runs.
    if (job->mode == Mode::Stream && job->sharing)
        m_torrents->broker().setSharing(job->hash, false);
    if (m_stopped)
        return;
    save();
    m_storage.flush();
    const State s = job->state;
    qCInfo(lcApp).noquote() << "Download" << job->id << stateName(s);
    if (s == State::Done || s == State::Seeding)
        emit notice(tr("Downloaded %1").arg(job->text(&Job::name)), false);
    else if (s == State::Failed)
        emit notice(tr("Download failed: %1").arg(job->text(&Job::error)), true);
    if (job->mode != Mode::Normal)
        refresh();
}

void Downloads::refresh()
{
    bool seeding = false;
    if (m_normalEngine) {
        for (const auto& j : m_jobs) {
            if (j->mode != Mode::Normal || j->state == State::Done || j->state == State::Failed
                || j->state == State::Cancelled)
                continue;
            const DownloadStatus st = m_normalEngine->status(j->engineId);
            j->peers = st.peers;
            j->rate = st.downloadRate;
            j->uploaded = st.uploadedBytes;
            j->done = st.doneBytes;
            j->size = st.sizeBytes;
            if (!st.name.isEmpty())
                j->set(&Job::name, st.name);
            const State before = j->state;
            switch (st.phase) {
            case DownloadStatus::Phase::FindingPeers:
            case DownloadStatus::Phase::FetchingMetadata:
                j->state = State::Starting;
                break;
            case DownloadStatus::Phase::Downloading:
                j->state = State::Running;
                break;
            case DownloadStatus::Phase::Finished:
                j->set(&Job::path, st.path);
                j->state = st.seeding ? State::Seeding : State::Done;
                break;
            case DownloadStatus::Phase::Failed:
                j->set(&Job::error, st.error == QLatin1String("no such file") ? tr("That file is not in this torrent.")
                                                                             : tr("The torrent stopped with an error."));
                j->state = State::Failed;
                break;
            case DownloadStatus::Phase::Gone:
                j->state = State::Done;
                break;
            }
            const bool ended = j->state == State::Seeding || j->state == State::Done || j->state == State::Failed;
            const bool wasRunning = before == State::Starting || before == State::Running;
            if (ended && wasRunning)
                finish(j);
            if (j->state == State::Seeding)
                seeding = true;
        }
    }
    if (activeCount() == 0 && !seeding)
        m_refresh.stop();
    emit itemsChanged();
}

int Downloads::activeCount() const
{
    int n = 0;
    for (const auto& j : m_jobs)
        if (j->state == State::Starting || j->state == State::Running)
            ++n;
    return n;
}

QVariantList Downloads::items() const
{
    QVariantList out;
    for (auto it = m_jobs.rbegin(); it != m_jobs.rend(); ++it) {
        const Job& j = **it;
        const State s = j.state;
        const qint64 size = j.size, done = j.done;
        const int percent = size > 0 ? int(done * 100 / size) : 0;
        QString detail;
        switch (s) {
        case State::Starting:
            detail = !j.torrent ? tr("Connecting…")
                   : j.peers > 0 ? tr("Getting the file list…") : tr("Looking for peers…");
            break;
        case State::Running:
            detail = size > 0 ? tr("%1 of %2").arg(sizeText(done), sizeText(size)) : sizeText(done);
            if (j.rate > 0)
                detail += QStringLiteral(" · %1/s").arg(sizeText(j.rate));
            if (j.torrent)
                detail += j.peers == 1 ? tr(" · 1 peer") : tr(" · %1 peers").arg(j.peers.load());
            if (j.torrent && j.sharing && j.uploaded > 0)
                detail += tr(" · %1 sent").arg(sizeText(j.uploaded));
            break;
        case State::Seeding:
            detail = tr("Sharing · %1 sent").arg(sizeText(j.uploaded)) + (j.peers == 1 ? tr(" · 1 peer") : tr(" · %1 peers").arg(j.peers.load()));
            break;
        case State::Done:
            detail = QDir::toNativeSeparators(j.text(&Job::path));
            break;
        case State::Failed:
            detail = j.text(&Job::error);
            break;
        case State::Cancelled:
            detail = tr("Cancelled");
            break;
        }
        out << QVariantMap{{QStringLiteral("id"), j.id},
                           {QStringLiteral("name"), j.text(&Job::name)},
                           {QStringLiteral("state"), stateName(s)},
                           {QStringLiteral("percent"), s == State::Done ? 100 : percent},
                           {QStringLiteral("detail"), detail},
                           {QStringLiteral("path"), j.text(&Job::path)},
                           {QStringLiteral("torrent"), j.torrent},
                           {QStringLiteral("mode"), modeName(j.mode)},
                           {QStringLiteral("sharing"), j.sharing.load()},
                           // A streamable download shares only while it runs.
                           {QStringLiteral("canShare"), j.torrent && (s == State::Starting || s == State::Running
                                                                      || (j.mode == Mode::Normal && s == State::Seeding))}};
    }
    return out;
}

void Downloads::cancel(int id)
{
    for (const auto& job : m_jobs) {
        if (job->id == id) {
            cancelJob(job);
            return;
        }
    }
}

void Downloads::cancelJob(const std::shared_ptr<Job>& job)
{
    if (job->state != State::Starting && job->state != State::Running)
        return;
    job->cancelled = true;
    std::shared_ptr<StreamHandle> handle;
    {
        std::lock_guard lock(job->mutex);
        handle = job->handle;
    }
    if (handle)
        handle->cancel();
    if (job->reply)
        job->reply->abort();
    if (job->mode == Mode::Normal && m_normalEngine) {
        m_normalEngine->remove(job->engineId, true);
        job->state = State::Cancelled;
        qCInfo(lcApp) << "Download" << job->id << "cancelled";
        save();
        emit itemsChanged();
    }
}

void Downloads::setSharing(int id, bool on)
{
    for (const auto& j : m_jobs) {
        if (j->id != id || !j->torrent || j->sharing == on)
            continue;
        if (j->mode == Mode::Stream && (j->state == State::Starting || j->state == State::Running)) {
            j->sharing = on;
            m_torrents->broker().setSharing(j->hash, on);
        } else if (j->mode == Mode::Normal && m_normalEngine
                   && (j->state == State::Starting || j->state == State::Running || j->state == State::Seeding)) {
            j->sharing = on;
            m_normalEngine->setSharing(j->engineId, on);
            if (!on && j->state == State::Seeding)
                j->state = State::Done;
        }
        emit itemsChanged();
    }
}

void Downloads::clearFinished()
{
    std::erase_if(m_jobs, [](const std::shared_ptr<Job>& j) {
        return j->state == State::Done || j->state == State::Failed || j->state == State::Cancelled;
    });
    save();
    emit itemsChanged();
}

void Downloads::stop()
{
    if (m_stopped)
        return;
    m_stopped = true;
    m_refresh.stop();
    save();   // before the cancelling below: what is running is kept as interrupted
    m_storage.flush();   // to disk now: the app is closing
    // Unfinished downloads are dropped with what they wrote: nothing picks
    // them up again next time (they stay on the list as interrupted).
    // Finished files stay.
    for (const auto& job : m_jobs)
        cancelJob(job);
    for (const auto& j : m_jobs)
        if (j->thread.joinable())
            j->thread.join();
    m_normalEngine.reset();   // seeding stops with the app
    // A web download aborted above has finished by now (abort() emits
    // finished at once); anything left is dropped with its .part file.
    for (const auto& j : m_jobs) {
        if (j->file) {
            j->file->close();
            j->file.reset();
            QFile::remove(j->text(&Job::part));
        }
    }
}

} // namespace cj
