#include "MpvTarget.h"

#include "Logging.h"

#include <QFile>
#include <QMetaObject>
#include <mpv/client.h>

#include <stdexcept>
#include <utility>
#include <vector>

namespace cj {
namespace {

// Ids for observed properties, so events can be matched without string compares.
enum PropertyId : uint64_t {
    PropPause = 1,
    PropTimePos,
    PropDuration,
    PropVolume,
    PropMute,
    PropEof,
    PropTitle,
    PropTracks,
    PropChapters,
    PropPausedForCache,
    PropCacheState,
    PropHwdecCurrent,
    PropVideoCodec,
    PropWidth,
    PropHeight,
    PropFps,
    PropDropped,
};

void wakeup(void* ctx)
{
    // Called by mpv on one of its own threads: hop to the owner's thread.
    QMetaObject::invokeMethod(static_cast<MpvTarget*>(ctx), "processEvents", Qt::QueuedConnection);
}

QString stringOf(const mpv_event_property* p)
{
    return (p->format == MPV_FORMAT_STRING && p->data) ? QString::fromUtf8(*static_cast<char**>(p->data))
                                                       : QString();
}
double doubleOf(const mpv_event_property* p)
{
    return (p->format == MPV_FORMAT_DOUBLE && p->data) ? *static_cast<double*>(p->data) : 0.0;
}
int64_t intOf(const mpv_event_property* p)
{
    return (p->format == MPV_FORMAT_INT64 && p->data) ? *static_cast<int64_t*>(p->data) : 0;
}
bool flagOf(const mpv_event_property* p)
{
    return p->format == MPV_FORMAT_FLAG && p->data && *static_cast<int*>(p->data);
}

// Reads one key of an mpv map node.
const mpv_node* field(const mpv_node& map, const char* key)
{
    if (map.format != MPV_FORMAT_NODE_MAP)
        return nullptr;
    for (int i = 0; i < map.u.list->num; ++i)
        if (qstrcmp(map.u.list->keys[i], key) == 0)
            return &map.u.list->values[i];
    return nullptr;
}
QString nodeString(const mpv_node& map, const char* key)
{
    const mpv_node* n = field(map, key);
    return n && n->format == MPV_FORMAT_STRING ? QString::fromUtf8(n->u.string) : QString();
}
int64_t nodeInt(const mpv_node& map, const char* key)
{
    const mpv_node* n = field(map, key);
    return n && n->format == MPV_FORMAT_INT64 ? n->u.int64 : 0;
}
double nodeDouble(const mpv_node& map, const char* key)
{
    const mpv_node* n = field(map, key);
    if (!n) return 0;
    if (n->format == MPV_FORMAT_DOUBLE) return n->u.double_;
    if (n->format == MPV_FORMAT_INT64)  return double(n->u.int64);
    return 0;
}
bool nodeFlag(const mpv_node& map, const char* key)
{
    const mpv_node* n = field(map, key);
    return n && n->format == MPV_FORMAT_FLAG && n->u.flag;
}

void setOption(mpv_handle* mpv, const char* name, const char* value)
{
    if (mpv_set_option_string(mpv, name, value) < 0)
        qCWarning(lcTargets) << "mpv rejected option" << name << "=" << value;
}

QByteArray number(double v) { return QByteArray::number(v, 'f', 3); }

// "16:9" or "2.39:1" or "1.85" -> width/height, or 0 if not a ratio.
double parseRatio(const QString& text)
{
    const QStringList parts = text.split(QLatin1Char(':'));
    bool ok1 = false, ok2 = true;
    const double a = parts.value(0).toDouble(&ok1);
    const double b = parts.size() > 1 ? parts.at(1).toDouble(&ok2) : 1.0;
    return ok1 && ok2 && a > 0 && b > 0 ? a / b : 0.0;
}

} // namespace

MpvTarget::MpvTarget(Mode mode, QObject* parent)
    : PlaybackTarget(parent)
    , m_mode(mode)
{
    m_mpv = mpv_create();
    if (!m_mpv)
        throw std::runtime_error("mpv_create failed");

    if (mode == Mode::Video) {
        setOption(m_mpv, "vo", "libmpv");
    } else {
        setOption(m_mpv, "vo", "null");
        setOption(m_mpv, "ao", "null");
    }
    setOption(m_mpv, "hwdec", "auto-safe");
    setOption(m_mpv, "keep-open", "yes");      // stay on the last frame at the end
    setOption(m_mpv, "idle", "yes");
    setOption(m_mpv, "terminal", "no");
    setOption(m_mpv, "input-default-bindings", "no");
    setOption(m_mpv, "osc", "no");
    setOption(m_mpv, "config", "no");          // ignore any mpv.conf on the user's machine
    setOption(m_mpv, "screenshot-format", "png");
    // CJ_MPV_OPTS="name=value,name=value" sets extra mpv options, for troubleshooting.
    for (const QByteArray& pair : qgetenv("CJ_MPV_OPTS").split(',')) {
        if (const qsizetype eq = pair.indexOf('='); eq > 0)
            setOption(m_mpv, pair.left(eq).constData(), pair.mid(eq + 1).constData());
    }

    if (mpv_initialize(m_mpv) < 0) {
        mpv_terminate_destroy(m_mpv);
        m_mpv = nullptr;
        throw std::runtime_error("mpv_initialize failed");
    }

    // CJ_MPV_LOG=v (or debug) shows more of mpv's own log, for troubleshooting.
    const QByteArray mpvLog = qgetenv("CJ_MPV_LOG");
    mpv_request_log_messages(m_mpv, mpvLog.isEmpty() ? "warn" : mpvLog.constData());

    struct { PropertyId id; const char* name; mpv_format format; } observed[] = {
        {PropPause,          "pause",                  MPV_FORMAT_FLAG},
        {PropTimePos,        "time-pos",               MPV_FORMAT_DOUBLE},
        {PropDuration,       "duration",               MPV_FORMAT_DOUBLE},
        {PropVolume,         "volume",                 MPV_FORMAT_DOUBLE},
        {PropMute,           "mute",                   MPV_FORMAT_FLAG},
        {PropEof,            "eof-reached",            MPV_FORMAT_FLAG},
        {PropTitle,          "media-title",            MPV_FORMAT_STRING},
        {PropTracks,         "track-list",             MPV_FORMAT_NODE},
        {PropChapters,       "chapter-list",           MPV_FORMAT_NODE},
        {PropPausedForCache, "paused-for-cache",       MPV_FORMAT_FLAG},
        {PropCacheState,     "cache-buffering-state",  MPV_FORMAT_INT64},
        {PropHwdecCurrent,   "hwdec-current",          MPV_FORMAT_STRING},
        {PropVideoCodec,     "video-codec",            MPV_FORMAT_STRING},
        {PropWidth,          "width",                  MPV_FORMAT_INT64},
        {PropHeight,         "height",                 MPV_FORMAT_INT64},
        {PropFps,            "estimated-vf-fps",       MPV_FORMAT_DOUBLE},
        {PropDropped,        "frame-drop-count",       MPV_FORMAT_INT64},
    };
    for (const auto& o : observed)
        mpv_observe_property(m_mpv, o.id, o.name, o.format);

    mpv_set_wakeup_callback(m_mpv, wakeup, this);
}

MpvTarget::~MpvTarget()
{
    if (m_mpv) {
        mpv_set_wakeup_callback(m_mpv, nullptr, nullptr);
        mpv_terminate_destroy(m_mpv);
    }
}

QString MpvTarget::name() const { return tr("This computer"); }

Capabilities MpvTarget::capabilities() const
{
    return Capability::SubtitleDelay | Capability::SubtitleStyle | Capability::AudioDelay
         | Capability::Chapters | Capability::FrameStep | Capability::Screenshot
         | Capability::AbLoop | Capability::AspectRatio | Capability::Crop
         | Capability::VideoEqualizer | Capability::HardwareDecoding;
}

// --- Plumbing -----------------------------------------------------------------

void MpvTarget::command(std::initializer_list<const char*> args, uint64_t replyId)
{
    std::vector<const char*> argv(args);
    argv.push_back(nullptr);
    if (const int err = mpv_command_async(m_mpv, replyId, argv.data()); err < 0)
        qCWarning(lcTargets) << "mpv command" << argv.front() << "failed:" << mpv_error_string(err);
}

void MpvTarget::setProperty(const char* name, const QByteArray& value)
{
    // Strings are converted by mpv to the property's own type.
    const char* v = value.constData();
    mpv_set_property_async(m_mpv, 0, name, MPV_FORMAT_STRING, &v);
}

void MpvTarget::setState(PlaybackState s)
{
    if (s == m_state)
        return;
    m_state = s;
    emit stateChanged(s);
}

void MpvTarget::updateState()
{
    if (!m_fileLoaded)
        return;
    setState(m_eof ? PlaybackState::Ended : m_paused ? PlaybackState::Paused : PlaybackState::Playing);
}

// --- Commands -------------------------------------------------------------------

void MpvTarget::load(const PlayableSource& source, double startSeconds)
{
    const QUrl& url = source.url;
    const QByteArray target = url.isLocalFile() ? QFile::encodeName(url.toLocalFile())
                                                : url.toString(QUrl::FullyEncoded).toUtf8();
    m_fileLoaded = false;
    m_eof = false;
    setState(PlaybackState::Loading);
    if (m_mode == Mode::Video && !m_renderReady) {
        qCInfo(lcTargets).noquote() << "Will open" << QString::fromUtf8(target) << "once video output is ready";
        m_pendingTarget = target;
        m_pendingStart = startSeconds;
        return;
    }
    startLoad(target, startSeconds);
}

void MpvTarget::startLoad(const QByteArray& target, double startSeconds)
{
    qCInfo(lcTargets).noquote() << "Opening" << QString::fromUtf8(target);
    // "start" applies to the next file loaded; reset it every time.
    mpv_set_property_string(m_mpv, "start", startSeconds > 0 ? number(startSeconds).constData() : "none");
    mpv_set_property_string(m_mpv, "pause", "no");
    ++m_loadsInFlight;
    command({"loadfile", target.constData(), "replace"});
}

void MpvTarget::markRenderReady()
{
    if (m_renderReady)
        return;
    m_renderReady = true;
    if (!m_pendingTarget.isEmpty())
        startLoad(std::exchange(m_pendingTarget, {}), m_pendingStart);
}

void MpvTarget::play()  { setProperty("pause", "no"); }
void MpvTarget::pause() { setProperty("pause", "yes"); }

void MpvTarget::stop()
{
    m_pendingTarget.clear();
    command({"stop"});
}

void MpvTarget::seek(double seconds)
{
    const QByteArray pos = number(qMax(0.0, seconds));
    command({"seek", pos.constData(), "absolute"});
}

void MpvTarget::setAudioTrack(int id)    { setProperty("aid", id < 0 ? "no" : QByteArray::number(id)); }
void MpvTarget::setSubtitleTrack(int id) { setProperty("sid", id < 0 ? "no" : QByteArray::number(id)); }

void MpvTarget::addExternalSubtitle(const QUrl& url)
{
    const QByteArray path = url.isLocalFile() ? QFile::encodeName(url.toLocalFile()) : url.toString().toUtf8();
    command({"sub-add", path.constData(), "select"});
}

void MpvTarget::setSpeed(double factor)  { setProperty("speed", number(factor)); }
void MpvTarget::setVolume(double volume) { setProperty("volume", number(qBound(0.0, volume, 100.0))); }
void MpvTarget::setMuted(bool muted)     { setProperty("mute", muted ? "yes" : "no"); }

void MpvTarget::setSubtitleDelay(double seconds) { setProperty("sub-delay", number(seconds)); }
void MpvTarget::setSubtitleScale(double factor)  { setProperty("sub-scale", number(factor)); }
void MpvTarget::setAudioDelay(double seconds)    { setProperty("audio-delay", number(seconds)); }
void MpvTarget::seekChapter(int index)           { setProperty("chapter", QByteArray::number(index)); }

void MpvTarget::frameStep(bool forward)
{
    command({forward ? "frame-step" : "frame-back-step"});
}

void MpvTarget::screenshot(const QString& filePath)
{
    const uint64_t id = m_nextReplyId++;
    m_pendingScreenshots.insert(id, filePath);
    const QByteArray path = QFile::encodeName(filePath);
    command({"screenshot-to-file", path.constData(), "video"}, id);
}

void MpvTarget::setAbLoop(double a, double b)
{
    setProperty("ab-loop-a", a < 0 ? "no" : number(a));
    setProperty("ab-loop-b", b < 0 ? "no" : number(b));
}

void MpvTarget::setAspectRatio(const QString& ratio)
{
    setProperty("video-aspect-override", ratio.isEmpty() ? "no" : ratio.toUtf8());
}

void MpvTarget::setCrop(const QString& ratio)
{
    m_cropRatio = ratio;
    applyCrop();
}

// Crop is given as an aspect ratio; mpv wants pixels, so it is recomputed
// whenever the video size is known or changes.
void MpvTarget::applyCrop()
{
    const double r = parseRatio(m_cropRatio);
    if (r <= 0 || m_videoWidth <= 0 || m_videoHeight <= 0) {
        setProperty("video-crop", "");
        return;
    }
    int w = m_videoWidth, h = m_videoHeight, x = 0, y = 0;
    if (double(w) / h > r) {
        w = int(h * r);
        x = (m_videoWidth - w) / 2;
    } else {
        h = int(w / r);
        y = (m_videoHeight - h) / 2;
    }
    setProperty("video-crop", QStringLiteral("%1x%2+%3+%4").arg(w).arg(h).arg(x).arg(y).toUtf8());
}

void MpvTarget::setVideoEqualizer(const VideoEqualizer& eq)
{
    setProperty("brightness", QByteArray::number(eq.brightness));
    setProperty("contrast",   QByteArray::number(eq.contrast));
    setProperty("saturation", QByteArray::number(eq.saturation));
    setProperty("gamma",      QByteArray::number(eq.gamma));
    setProperty("hue",        QByteArray::number(eq.hue));
}

void MpvTarget::setHardwareDecoding(bool enabled)
{
    m_hwdecEnabled = enabled;
    setProperty("hwdec", enabled ? "auto-safe" : "no");
    qCInfo(lcTargets) << "Hardware decoding" << (enabled ? "on" : "off");
    emitStats();
}

// --- Events -----------------------------------------------------------------------

void MpvTarget::processEvents()
{
    while (m_mpv) {
        mpv_event* event = mpv_wait_event(m_mpv, 0);
        switch (event->event_id) {
        case MPV_EVENT_NONE:
            return;
        case MPV_EVENT_PROPERTY_CHANGE:
            handleProperty(event->reply_userdata, static_cast<mpv_event_property*>(event->data));
            break;
        case MPV_EVENT_START_FILE:
            m_loadsInFlight = qMax(0, m_loadsInFlight - 1);
            m_fileLoaded = false;
            setState(PlaybackState::Loading);
            break;
        case MPV_EVENT_FILE_LOADED:
            qCInfo(lcTargets) << "File loaded";
            m_fileLoaded = true;
            m_eof = false;
            applyCrop();
            updateState();
            break;
        case MPV_EVENT_END_FILE: {
            const auto* end = static_cast<mpv_event_end_file*>(event->data);
            if (end->reason == MPV_END_FILE_REASON_ERROR) {
                m_fileLoaded = false;
                if (m_loadsInFlight == 0)
                    setState(PlaybackState::Idle);
                const QString message = QString::fromUtf8(mpv_error_string(end->error));
                const PlaybackError kind = end->error == MPV_ERROR_UNKNOWN_FORMAT  ? PlaybackError::UnsupportedFile
                                         : end->error == MPV_ERROR_LOADING_FAILED ? PlaybackError::LoadFailed
                                                                                  : PlaybackError::Internal;
                qCWarning(lcTargets).noquote() << "Playback failed:" << message;
                emit errorOccurred(kind, message);
            } else if (end->reason == MPV_END_FILE_REASON_STOP || end->reason == MPV_END_FILE_REASON_QUIT) {
                // STOP also fires when a new file replaces this one; stay
                // in Loading then instead of flashing Idle.
                m_fileLoaded = false;
                if (m_loadsInFlight == 0)
                    setState(PlaybackState::Idle);
            }
            break;
        }
        case MPV_EVENT_COMMAND_REPLY:
            if (const auto it = m_pendingScreenshots.find(event->reply_userdata); it != m_pendingScreenshots.end()) {
                const QString path = it.value();
                m_pendingScreenshots.erase(it);
                if (event->error >= 0) {
                    qCInfo(lcTargets).noquote() << "Screenshot saved to" << path;
                    emit screenshotSaved(path);
                } else {
                    qCWarning(lcTargets) << "Screenshot failed:" << mpv_error_string(event->error);
                }
            }
            break;
        case MPV_EVENT_LOG_MESSAGE: {
            const auto* msg = static_cast<mpv_event_log_message*>(event->data);
            qCWarning(lcTargets).noquote() << "mpv" << msg->prefix << ":"
                                           << QString::fromUtf8(msg->text).trimmed();
            break;
        }
        case MPV_EVENT_SHUTDOWN:
            qCWarning(lcTargets) << "mpv shut down unexpectedly";
            emit errorOccurred(PlaybackError::Internal, QStringLiteral("mpv shut down"));
            return;
        default:
            break;
        }
    }
}

void MpvTarget::handleProperty(uint64_t id, const mpv_event_property* p)
{
    // Most properties become unavailable (format NONE) when nothing is loaded;
    // the helpers above return 0, "" or empty lists in that case.
    switch (static_cast<PropertyId>(id)) {
    case PropPause:
        m_paused = flagOf(p);
        updateState();
        break;
    case PropEof:
        m_eof = flagOf(p);
        updateState();
        break;
    case PropTimePos:
        emit positionChanged(doubleOf(p));
        break;
    case PropDuration:
        emit durationChanged(doubleOf(p));
        break;
    case PropVolume:
        if (p->format == MPV_FORMAT_DOUBLE) {
            m_volume = doubleOf(p);
            emit volumeChanged(m_volume, m_muted);
        }
        break;
    case PropMute:
        m_muted = flagOf(p);
        emit volumeChanged(m_volume, m_muted);
        break;
    case PropTitle: {
        // Without a title tag mpv falls back to the file name; the Session
        // already has a better name for that case, so only pass real titles.
        const QString title = stringOf(p);
        char* filename = mpv_get_property_string(m_mpv, "filename");
        const bool isFileName = filename && title == QString::fromUtf8(filename);
        mpv_free(filename);
        if (!isFileName)
            emit titleChanged(title);
        break;
    }
    case PropTracks:
        emit tracksChanged(p->format == MPV_FORMAT_NODE ? parseTracks(static_cast<mpv_node*>(p->data)) : QList<Track>{});
        break;
    case PropChapters:
        emit chaptersChanged(p->format == MPV_FORMAT_NODE ? parseChapters(static_cast<mpv_node*>(p->data)) : QList<Chapter>{});
        break;
    case PropPausedForCache:
        m_pausedForCache = flagOf(p);
        emit bufferingChanged(m_pausedForCache, m_cachePercent);
        break;
    case PropCacheState:
        m_cachePercent = int(intOf(p));
        if (m_pausedForCache)
            emit bufferingChanged(true, m_cachePercent);
        break;
    case PropHwdecCurrent:
        m_hwdecCurrent = stringOf(p);
        if (!m_hwdecCurrent.isEmpty() && m_hwdecCurrent != QLatin1String("no"))
            qCInfo(lcTargets).noquote() << "Active decoder:" << m_hwdecCurrent;
        emitStats();
        break;
    case PropVideoCodec:
        m_videoCodec = stringOf(p);
        emitStats();
        break;
    case PropWidth:
        m_videoWidth = int(intOf(p));
        applyCrop();
        emitStats();
        break;
    case PropHeight:
        m_videoHeight = int(intOf(p));
        applyCrop();
        emitStats();
        break;
    case PropFps:
        m_fps = doubleOf(p);
        emitStats();
        break;
    case PropDropped:
        m_dropped = intOf(p);
        emitStats();
        break;
    }
}

void MpvTarget::emitStats()
{
    const bool hw = !m_hwdecCurrent.isEmpty() && m_hwdecCurrent != QLatin1String("no");
    emit statsChanged({
        {QStringLiteral("Decoder"), hw ? m_hwdecCurrent + QStringLiteral(" (hardware)") : QStringLiteral("software")},
        {QStringLiteral("HW decoding"), m_hwdecEnabled ? QStringLiteral("allowed") : QStringLiteral("off")},
        {QStringLiteral("Codec"), m_videoCodec.isEmpty() ? QStringLiteral("-") : m_videoCodec},
        {QStringLiteral("Resolution"), m_videoWidth > 0 ? QStringLiteral("%1×%2").arg(m_videoWidth).arg(m_videoHeight) : QStringLiteral("-")},
        {QStringLiteral("FPS"), m_fps > 0 ? QString::number(m_fps, 'f', 2) : QStringLiteral("-")},
        {QStringLiteral("Dropped frames"), QString::number(m_dropped)},
    });
}

QList<Track> MpvTarget::parseTracks(const mpv_node* node)
{
    QList<Track> tracks;
    if (!node || node->format != MPV_FORMAT_NODE_ARRAY)
        return tracks;
    for (int i = 0; i < node->u.list->num; ++i) {
        const mpv_node& t = node->u.list->values[i];
        const QString type = nodeString(t, "type");
        Track track;
        if (type == QLatin1String("video"))      track.type = Track::Type::Video;
        else if (type == QLatin1String("audio")) track.type = Track::Type::Audio;
        else if (type == QLatin1String("sub"))   track.type = Track::Type::Subtitle;
        else continue;
        track.id = int(nodeInt(t, "id"));
        track.title = nodeString(t, "title");
        track.language = nodeString(t, "lang");
        track.codec = nodeString(t, "codec");
        track.selected = nodeFlag(t, "selected");
        track.external = nodeFlag(t, "external");
        track.isDefault = nodeFlag(t, "default");
        tracks.append(track);
    }
    return tracks;
}

QList<Chapter> MpvTarget::parseChapters(const mpv_node* node)
{
    QList<Chapter> chapters;
    if (!node || node->format != MPV_FORMAT_NODE_ARRAY)
        return chapters;
    for (int i = 0; i < node->u.list->num; ++i) {
        const mpv_node& c = node->u.list->values[i];
        chapters.append({nodeDouble(c, "time"), nodeString(c, "title")});
    }
    return chapters;
}

} // namespace cj
