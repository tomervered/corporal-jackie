#include "MpvPlayer.h"

#include "Logging.h"

#include <QFileInfo>
#include <QMetaObject>
#include <QRegularExpression>
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
    PropHwdecCurrent,
    PropVideoCodec,
    PropWidth,
    PropHeight,
    PropFps,
    PropDropped,
    PropTitle,
};

void wakeup(void* ctx)
{
    // Called by mpv on one of its own threads: hop to the owner's thread.
    QMetaObject::invokeMethod(static_cast<MpvPlayer*>(ctx), "processEvents", Qt::QueuedConnection);
}

QString stringOf(const mpv_event_property* p)
{
    if (p->format == MPV_FORMAT_STRING && p->data)
        return QString::fromUtf8(*static_cast<char**>(p->data));
    return {};
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

void setOption(mpv_handle* mpv, const char* name, const char* value)
{
    if (mpv_set_option_string(mpv, name, value) < 0)
        qCWarning(lcTargets) << "mpv rejected option" << name << "=" << value;
}

} // namespace

MpvPlayer::MpvPlayer(Mode mode, QObject* parent)
    : QObject(parent)
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

    mpv_observe_property(m_mpv, PropPause,        "pause",                MPV_FORMAT_FLAG);
    mpv_observe_property(m_mpv, PropTimePos,      "time-pos",             MPV_FORMAT_DOUBLE);
    mpv_observe_property(m_mpv, PropDuration,     "duration",             MPV_FORMAT_DOUBLE);
    mpv_observe_property(m_mpv, PropVolume,       "volume",               MPV_FORMAT_DOUBLE);
    mpv_observe_property(m_mpv, PropMute,         "mute",                 MPV_FORMAT_FLAG);
    mpv_observe_property(m_mpv, PropHwdecCurrent, "hwdec-current",        MPV_FORMAT_STRING);
    mpv_observe_property(m_mpv, PropVideoCodec,   "video-codec",          MPV_FORMAT_STRING);
    mpv_observe_property(m_mpv, PropWidth,        "width",                MPV_FORMAT_INT64);
    mpv_observe_property(m_mpv, PropHeight,       "height",               MPV_FORMAT_INT64);
    mpv_observe_property(m_mpv, PropFps,          "estimated-vf-fps",     MPV_FORMAT_DOUBLE);
    mpv_observe_property(m_mpv, PropDropped,      "frame-drop-count",     MPV_FORMAT_INT64);
    mpv_observe_property(m_mpv, PropTitle,        "media-title",          MPV_FORMAT_STRING);

    mpv_set_wakeup_callback(m_mpv, wakeup, this);
}

MpvPlayer::~MpvPlayer()
{
    if (m_mpv) {
        mpv_set_wakeup_callback(m_mpv, nullptr, nullptr);
        mpv_terminate_destroy(m_mpv);
    }
}

void MpvPlayer::command(std::initializer_list<const char*> args)
{
    std::vector<const char*> argv(args);
    argv.push_back(nullptr);
    if (const int err = mpv_command_async(m_mpv, 0, argv.data()); err < 0)
        qCWarning(lcTargets) << "mpv command" << argv.front() << "failed:" << mpv_error_string(err);
}

void MpvPlayer::open(const QUrl& url)
{
    if (url.isEmpty() || !url.isValid())
        return;
    load(url.isLocalFile() ? QFile::encodeName(url.toLocalFile())
                           : url.toString(QUrl::FullyEncoded).toUtf8(),
         url);
}

void MpvPlayer::openPath(const QString& pathOrUrl)
{
    // "scheme://..." goes to mpv as is (QUrl cannot parse every mpv URL, e.g.
    // av://lavfi:...). Anything else, including C:\..., is a local path.
    static const QRegularExpression scheme(QStringLiteral("^[A-Za-z][A-Za-z0-9+.-]+://"));
    if (scheme.match(pathOrUrl).hasMatch()) {
        load(pathOrUrl.toUtf8(), QUrl(pathOrUrl));
        return;
    }
    const QString absolute = QFileInfo(pathOrUrl).absoluteFilePath();
    load(QFile::encodeName(absolute), QUrl::fromLocalFile(absolute));
}

void MpvPlayer::load(const QByteArray& target, const QUrl& source)
{
    m_source = source;
    emit sourceChanged();
    if (m_mode == Mode::Video && !m_renderReady) {
        qCInfo(lcTargets).noquote() << "Will open" << QString::fromUtf8(target) << "once video output is ready";
        m_pendingTarget = target;
        return;
    }
    qCInfo(lcTargets).noquote() << "Opening" << QString::fromUtf8(target);
    command({"loadfile", target.constData(), "replace"});
    setPaused(false);
}

void MpvPlayer::markRenderReady()
{
    if (m_renderReady)
        return;
    m_renderReady = true;
    if (!m_pendingTarget.isEmpty()) {
        const QByteArray target = std::exchange(m_pendingTarget, {});
        load(target, m_source);
    }
}

void MpvPlayer::togglePause() { setPaused(!m_paused); }

void MpvPlayer::setPaused(bool paused)
{
    int flag = paused ? 1 : 0;
    mpv_set_property_async(m_mpv, 0, "pause", MPV_FORMAT_FLAG, &flag);
}

void MpvPlayer::setVolume(double volume)
{
    double v = qBound(0.0, volume, 100.0);
    mpv_set_property_async(m_mpv, 0, "volume", MPV_FORMAT_DOUBLE, &v);
}

void MpvPlayer::setMuted(bool muted)
{
    int flag = muted ? 1 : 0;
    mpv_set_property_async(m_mpv, 0, "mute", MPV_FORMAT_FLAG, &flag);
}

void MpvPlayer::setHwdecEnabled(bool enabled)
{
    if (enabled == m_hwdecEnabled)
        return;
    m_hwdecEnabled = enabled;
    mpv_set_property_string(m_mpv, "hwdec", enabled ? "auto-safe" : "no");
    qCInfo(lcTargets) << "Hardware decoding" << (enabled ? "on" : "off");
    emit hwdecEnabledChanged();
}

void MpvPlayer::seek(double seconds)
{
    const QByteArray pos = QByteArray::number(qMax(0.0, seconds), 'f', 3);
    command({"seek", pos.constData(), "absolute"});
}

void MpvPlayer::seekRelative(double seconds)
{
    const QByteArray delta = QByteArray::number(seconds, 'f', 3);
    command({"seek", delta.constData(), "relative"});
}

void MpvPlayer::stop()
{
    command({"stop"});
}

void MpvPlayer::setFileLoaded(bool loaded)
{
    if (loaded == m_fileLoaded)
        return;
    m_fileLoaded = loaded;
    emit fileLoadedChanged();
}

void MpvPlayer::processEvents()
{
    while (m_mpv) {
        mpv_event* event = mpv_wait_event(m_mpv, 0);
        switch (event->event_id) {
        case MPV_EVENT_NONE:
            return;
        case MPV_EVENT_PROPERTY_CHANGE:
            handlePropertyChange(event->reply_userdata, static_cast<mpv_event_property*>(event->data));
            break;
        case MPV_EVENT_FILE_LOADED:
            qCInfo(lcTargets) << "File loaded";
            setFileLoaded(true);
            break;
        case MPV_EVENT_START_FILE:
            setFileLoaded(false);
            break;
        case MPV_EVENT_END_FILE: {
            const auto* end = static_cast<mpv_event_end_file*>(event->data);
            if (end->reason == MPV_END_FILE_REASON_ERROR) {
                setFileLoaded(false);
                const QString message = QString::fromUtf8(mpv_error_string(end->error));
                const Error kind = end->error == MPV_ERROR_UNKNOWN_FORMAT ? Error::UnsupportedFile
                                 : end->error == MPV_ERROR_LOADING_FAILED ? Error::LoadFailed
                                                                          : Error::Internal;
                qCWarning(lcTargets).noquote() << "Playback failed:" << message;
                emit errorOccurred(kind, message);
            } else if (end->reason == MPV_END_FILE_REASON_EOF) {
                emit endOfFile();
            } else if (end->reason == MPV_END_FILE_REASON_STOP) {
                setFileLoaded(false);
            }
            break;
        }
        case MPV_EVENT_LOG_MESSAGE: {
            const auto* msg = static_cast<mpv_event_log_message*>(event->data);
            qCWarning(lcTargets).noquote() << "mpv" << msg->prefix << ":"
                                           << QString::fromUtf8(msg->text).trimmed();
            break;
        }
        case MPV_EVENT_SHUTDOWN:
            qCWarning(lcTargets) << "mpv shut down unexpectedly";
            emit errorOccurred(Error::Internal, QStringLiteral("mpv shut down"));
            return;
        default:
            break;
        }
    }
}

void MpvPlayer::handlePropertyChange(uint64_t id, const mpv_event_property* p)
{
    // Most properties become unavailable (format NONE) when nothing is loaded;
    // the helpers above return 0 or "" in that case.
    switch (static_cast<PropertyId>(id)) {
    case PropPause:
        if (const bool v = flagOf(p); v != m_paused) { m_paused = v; emit pausedChanged(); }
        break;
    case PropTimePos:
        m_position = doubleOf(p);
        emit positionChanged();
        break;
    case PropDuration:
        m_duration = doubleOf(p);
        emit durationChanged();
        break;
    case PropVolume:
        if (p->format == MPV_FORMAT_DOUBLE) { m_volume = doubleOf(p); emit volumeChanged(); }
        break;
    case PropMute:
        if (const bool v = flagOf(p); v != m_muted) { m_muted = v; emit mutedChanged(); }
        break;
    case PropHwdecCurrent:
        m_hwdecCurrent = stringOf(p);
        if (!m_hwdecCurrent.isEmpty())
            qCInfo(lcTargets).noquote() << "Active decoder:" << m_hwdecCurrent;
        emit statsChanged();
        break;
    case PropVideoCodec:
        m_videoCodec = stringOf(p);
        emit statsChanged();
        break;
    case PropWidth:
        m_videoWidth = int(intOf(p));
        emit statsChanged();
        break;
    case PropHeight:
        m_videoHeight = int(intOf(p));
        emit statsChanged();
        break;
    case PropFps:
        m_fps = doubleOf(p);
        emit statsChanged();
        break;
    case PropDropped:
        m_droppedFrames = int(intOf(p));
        emit statsChanged();
        break;
    case PropTitle:
        m_title = stringOf(p);
        emit titleChanged();
        break;
    }
}

} // namespace cj
