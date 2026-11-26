#include "MpvPlayer.h"

#include "GraphicsCards.h"
#include "Logging.h"
#include "ScopedHttp.h"

#include "PlaybackSession.h"

#include <QFile>
#include <QRegularExpression>
#include <QDir>
#include <QFileInfo>
#include <QMetaObject>
#include <QTemporaryFile>
#include <QTimer>
#include <mpv/client.h>

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <utility>
#include <vector>

#ifdef Q_OS_MACOS
#include "MetalFxUpscaler.h"
#endif

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
    PropOutHeight,
    PropFps,
    PropDropped,
    PropDecoderDropped,
    PropDelayed,
    PropMistimed,
    // Settings mpv's own keys change; passed on as settingChanged.
    PropSpeed,
    PropSubDelay,
    PropAudioDelay,
    PropSubScale,
    PropAbLoopA,
    PropAbLoopB,
    PropAspect,
    PropBrightness,
    PropContrast,
    PropSaturation,
    PropGamma,
    PropHue,
    PropHwdec,
    PropFullscreen,
};

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

// The protocols FFmpeg may open for links inside a file (HLS segments, DASH
// fragments, MP4 references). No file, no pipe, no network-share access.
constexpr const char* kLavfProtocolWhitelist = "protocol_whitelist=[http,https,tls,tcp,crypto,data]";

// The id of the on_load hook (see MpvPlayer::onLoadHook).
constexpr uint64_t kOnLoadHook = 1;
constexpr uint64_t kOnPreloadedHook = 2;

// "16:9" or "2.39:1" or "1.85" -> width/height, or 0 if not a ratio.
double parseRatio(const QString& text)
{
    const QStringList parts = text.split(QLatin1Char(':'));
    bool ok1 = false, ok2 = true;
    const double a = parts.value(0).toDouble(&ok1);
    const double b = parts.size() > 1 ? parts.at(1).toDouble(&ok2) : 1.0;
    return ok1 && ok2 && a > 0 && b > 0 ? a / b : 0.0;
}

// Hardware decoding when it is allowed. On Windows only copy-back decoders:
// our mpv also has a zero-copy Direct3D 11 bridge (mpv-patches), which the
// app uses only while NVIDIA or Intel VSR is picked, because it converts
// every frame to 8-bit RGB.
#ifdef Q_OS_WIN
constexpr const char* kHwdecOn = "auto-copy-safe";
#else
constexpr const char* kHwdecOn = "auto-safe";
#endif

bool isVsr(Upscaler u) { return u == Upscaler::NvidiaVsr || u == Upscaler::IntelVsr; }
// The upscalers that are mpv shader files (any GPU).
bool isShader(Upscaler u)
{
    return u == Upscaler::Fsr || u == Upscaler::Sgsr || u == Upscaler::Cas || u == Upscaler::Anime4k;
}


} // namespace

Capabilities MpvPlayer::vsrCapabilities(unsigned vendorId, const QString& adapterName)
{
    // NVIDIA: RTX 20 series and later (a GTX has no VSR).
    if (vendorId == 0x10DE && adapterName.contains(QLatin1String("RTX"), Qt::CaseInsensitive))
        return Capability::UpscaleNvidiaVsr;
    // Intel: the Xe generation (11th gen Core and later): Arc, Iris Xe, UHD
    // Graphics 7xx, and the plain "UHD Graphics" of 12th to 14th gen laptop
    // chips. Older ones have none: HD and UHD Graphics 5xx/6xx (a 10th gen
    // i3, say) and Iris Plus. A chip this lets through that cannot run it
    // fails on first use, and the target turns VSR off and says so.
    if (vendorId == 0x8086) {
        static const QRegularExpression xe(
            QStringLiteral("\\bArc\\b|Iris(\\(R\\))? Xe|UHD Graphics( 7\\d\\d)?$"),
            QRegularExpression::CaseInsensitiveOption);
        if (xe.match(adapterName.trimmed()).hasMatch())
            return Capability::UpscaleIntelVsr;
    }
    return {};
}

QString MpvPlayer::vsrFilter(Upscaler upscaler, int videoHeight, int targetHeight, int shownHeight,
                             bool intelCopyback)
{
    const int target = targetHeight > 0 ? targetHeight : shownHeight;
    if (!isVsr(upscaler) || videoHeight <= 0 || target <= videoHeight)
        return {};  // nothing to make bigger (yet)
    // In steps of 0.05, so a window resized by a few pixels keeps its filter.
    const double factor = qBound(1.05, qRound(20.0 * target / videoHeight) / 20.0, 4.0);
    QString filter = QStringLiteral("d3d11vpp=scaling-mode=%1:scale=%2")
        .arg(upscaler == Upscaler::NvidiaVsr ? QStringLiteral("nvidia") : QStringLiteral("intel"))
        .arg(factor, 0, 'f', 2);
    // Only Intel VSR has the compatibility mode; NVIDIA keeps its own path.
    if (upscaler == Upscaler::IntelVsr && intelCopyback)
        filter += QStringLiteral(":intel-copyback=yes");
    return filter;
}

namespace {
QString upscalerName(Upscaler u)
{
    switch (u) {
    case Upscaler::Fsr:       return QStringLiteral("AMD FSR 1");
    case Upscaler::Sgsr:      return QStringLiteral("Snapdragon GSR");
    case Upscaler::Cas:       return QStringLiteral("AMD CAS");
    case Upscaler::Anime4k:   return QStringLiteral("Anime4K");
    case Upscaler::NvidiaVsr: return QStringLiteral("NVIDIA VSR");
    case Upscaler::IntelVsr:  return QStringLiteral("Intel VSR");
    default:                  return {};
    }
}
} // namespace

QString MpvPlayer::upscalerStatus(Upscaler upscaler, int videoHeight, int target, int outHeight, bool filterSet)
{
    const QString name = upscalerName(upscaler);
    if (name.isEmpty() || videoHeight <= 0)
        return {};
    if (target > 0 && target <= videoHeight)
        return QStringLiteral("%1 idle · the video is already %2p").arg(name).arg(videoHeight);
    if (isShader(upscaler))
        return target > 0 ? QStringLiteral("%1 on · %2p → %3p").arg(name).arg(videoHeight).arg(target)
                          : QStringLiteral("%1 on · %2p → screen size").arg(name).arg(videoHeight);
    // VSR: only the size of the frames the filter hands on shows that the
    // driver did the scaling.
    if (filterSet && outHeight > videoHeight)
        return QStringLiteral("%1 on · %2p → %3p").arg(name).arg(videoHeight).arg(outHeight);
    return QStringLiteral("%1 starting…").arg(name);
}

MpvPlayer::MpvPlayer(Mode mode, QObject* parent)
    : PlaybackTarget(parent)
    , m_mode(mode)
{
    m_mpv = mpv_create();
    if (!m_mpv)
        throw std::runtime_error("mpv_create failed");

    // The settings mpv reads once, before it starts.
    chooseOutput(mode);
    findUpscalers(mode);
    setPlayerBehaviour();
    setSafetyLimits();
    setScreenshotNaming();
    setOptionsFromEnvironment();

    if (mpv_initialize(m_mpv) < 0) {
        mpv_terminate_destroy(m_mpv);
        m_mpv = nullptr;
        throw std::runtime_error("mpv_initialize failed");
    }
    // Links with credentials and remote subtitles are fetched by this, never
    // by mpv's own HTTP code (see ScopedHttp.h). So mpv's http-header-fields
    // stays empty and its user agent stays its default.
    m_http = std::make_unique<ScopedHttp>();
    m_http->registerWith(m_mpv);
    // Every file mpv is about to open, playlist entries included, passes
    // through onLoadHook() first.
    mpv_hook_add(m_mpv, kOnLoadHook, "on_load", 0);
    if (mode == Mode::Video)
        mpv_hook_add(m_mpv, kOnPreloadedHook, "on_preloaded", 0);

    logLibraryVersions();
    startMpvLog();
    observeProperties();
    bindAppKeys();

    mpv_set_wakeup_callback(m_mpv, [](void* context) {
        static_cast<MpvPlayer*>(context)->queueEventDrain();
    }, this);
}

// Video mode draws into the app's window; the other mode (tests) plays
// nothing on screen or speakers.
void MpvPlayer::chooseOutput(Mode mode)
{
    if (mode == Mode::Video) {
        setOption(m_mpv, "vo", "libmpv");
        // mpv's defaults on purpose: its "high-quality" scaling and display
        // sync with frame blending ("smooth motion") were tried on
        // 2026-11-08 and each made playback stutter in this embedded
        // Qt Quick output, even on an RTX 4060.
    } else {
        setOption(m_mpv, "vo", "null");
        setOption(m_mpv, "ao", "null");
    }
    setOption(m_mpv, "hwdec", kHwdecOn);
}

// Asks which upscalers this computer can run, and sets up the timer that
// applies the vendor upscaler after a window resize.
void MpvPlayer::findUpscalers(Mode mode)
{
    if (mode == Mode::Video) {
        m_vsrCaps = GraphicsCards::activeVsr();
#ifdef Q_OS_MACOS
        if (MetalFxUpscaler::available())
            m_vsrCaps |= Capability::UpscaleMetalFx;
        qCInfo(lcTargets) << "MetalFX upscaling" << (MetalFxUpscaler::available() ? "available" : "not available");
#endif
    }
    m_vsrTimer = new QTimer(this);
    m_vsrTimer->setSingleShot(true);
    m_vsrTimer->setInterval(400);  // a window being resized settles first
    connect(m_vsrTimer, &QTimer::timeout, this, &MpvPlayer::applyVsrFilter);
}

// How mpv behaves inside the app: the app draws its own bars and buttons,
// so mpv's are turned off.
void MpvPlayer::setPlayerBehaviour()
{
    setOption(m_mpv, "volume-max", "130");      // PlaybackTarget::kMaxVolume
    // mpv's own seek and volume bars and messages stay off: the app's bar
    // shows both. (O still cycles mpv's OSD on for the position.)
    setOption(m_mpv, "osd-bar", "no");
    setOption(m_mpv, "osd-level", "0");
    setOption(m_mpv, "keep-open", "yes");      // stay on the last frame at the end
    setOption(m_mpv, "idle", "yes");
    setOption(m_mpv, "terminal", "no");
    // mpv's own key bindings (its input.conf defaults). The window passes
    // every key over the video to mpv; the app has no shortcuts of its own
    // there, so the two never disagree about a key.
    setOption(m_mpv, "input-default-bindings", "yes");
    setOption(m_mpv, "osc", "no");
    setOption(m_mpv, "config", "no");          // ignore any mpv.conf on the user's machine
}

// Links come from add-ons and pasted text, so mpv is told not to trust them
// (security review 2026-11-15, docs/SECURITY.md).
void MpvPlayer::setSafetyLimits()
{
    // Distro builds of libmpv (Homebrew, apt) include Lua: its ytdl hook would
    // hand a link to a yt-dlp found on the PATH, and a script from the user's
    // folders would run inside the player.
    setOption(m_mpv, "ytdl", "no");
    setOption(m_mpv, "load-scripts", "no");
    // mpv does not check HTTPS certificates unless told to, so anyone on the
    // network path could swap a stream for a crafted file. Links with
    // credentials go through ScopedHttp, which uses Qt's checks.
    setOption(m_mpv, "tls-verify", "yes");
    // FFmpeg's HLS and DASH readers, and MP4 external references, open the
    // links a file names by themselves. FFmpeg lets an HLS list from the web
    // name file: segments, which on Windows can be a network share (a
    // password-hash leak). Only network protocols may be opened that way.
    setOption(m_mpv, "demuxer-lavf-o", kLavfProtocolWhitelist);
}

void MpvPlayer::setScreenshotNaming()
{
    setOption(m_mpv, "screenshot-format", "png");
    // mpv's S key saves here too (mpv makes the folder), named like the
    // app's own screenshots: "<title> 01-02-03.png".
    setOption(m_mpv, "screenshot-dir", PlaybackSession::screenshotFolder().toUtf8().constData());  // mpv takes UTF-8
    setOption(m_mpv, "screenshot-template", "%{media-title} %wH-%wM-%wS");
}

// CJ_MPV_OPTS="name=value,name=value" sets extra mpv options, for troubleshooting.
void MpvPlayer::setOptionsFromEnvironment()
{
    for (const QByteArray& pair : qgetenv("CJ_MPV_OPTS").split(',')) {
        const qsizetype eq = pair.indexOf('=');
        if (eq <= 0)
            continue;
        setOption(m_mpv, pair.left(eq).constData(), pair.mid(eq + 1).constData());
        if (pair.startsWith("cache="))
            m_cacheModeFromEnv = true;
        if (pair.startsWith("demuxer-max-"))
            m_cacheSizeFromEnv = true;
    }
}

// The exact builds in use: the macOS package takes whatever Homebrew
// had, so the log is where a report shows which FFmpeg played a file.
void MpvPlayer::logLibraryVersions()
{
    for (const char* name : {"mpv-version", "ffmpeg-version"}) {
        if (char* v = mpv_get_property_string(m_mpv, name)) {
            qCInfo(lcTargets).noquote() << name << QString::fromUtf8(v);
            mpv_free(v);
        }
    }
}

// CJ_MPV_LOG=v (or debug) shows more of mpv's own log, for troubleshooting.
// Without it mpv is asked for info, and only warnings and the one line the
// Windows video bridge writes about how it waits for OpenGL are kept, so a
// report shows which mode ran.
void MpvPlayer::startMpvLog()
{
    const QByteArray mpvLog = qgetenv("CJ_MPV_LOG");
    mpv_request_log_messages(m_mpv, mpvLog.isEmpty() ? "info" : mpvLog.constData());
}

// Asks mpv to tell us whenever one of these properties changes; each change
// arrives in handleProperty().
void MpvPlayer::observeProperties()
{
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
        {PropOutHeight,      "video-out-params/h",     MPV_FORMAT_INT64},
        {PropFps,            "estimated-vf-fps",       MPV_FORMAT_DOUBLE},
        {PropDropped,        "frame-drop-count",       MPV_FORMAT_INT64},
        {PropDecoderDropped, "decoder-frame-drop-count", MPV_FORMAT_INT64},
        {PropDelayed,        "vo-delayed-frame-count", MPV_FORMAT_INT64},
        {PropMistimed,       "mistimed-frame-count",   MPV_FORMAT_INT64},
        {PropSpeed,          "speed",                  MPV_FORMAT_DOUBLE},
        {PropSubDelay,       "sub-delay",              MPV_FORMAT_DOUBLE},
        {PropAudioDelay,     "audio-delay",            MPV_FORMAT_DOUBLE},
        {PropSubScale,       "sub-scale",              MPV_FORMAT_DOUBLE},
        {PropAbLoopA,        "ab-loop-a",              MPV_FORMAT_DOUBLE},
        {PropAbLoopB,        "ab-loop-b",              MPV_FORMAT_DOUBLE},
        {PropAspect,         "video-aspect-override",  MPV_FORMAT_STRING},
        {PropBrightness,     "brightness",             MPV_FORMAT_INT64},
        {PropContrast,       "contrast",               MPV_FORMAT_INT64},
        {PropSaturation,     "saturation",             MPV_FORMAT_INT64},
        {PropGamma,          "gamma",                  MPV_FORMAT_INT64},
        {PropHue,            "hue",                    MPV_FORMAT_INT64},
        {PropHwdec,          "hwdec",                  MPV_FORMAT_STRING},
        {PropFullscreen,     "fullscreen",             MPV_FORMAT_FLAG},
    };
    for (const auto& o : observed)
        mpv_observe_property(m_mpv, o.id, o.name, o.format);
}

MpvPlayer::~MpvPlayer()
{
    if (m_http)
        m_http->abort();  // mpv waits for its open streams
    if (m_mpv) {
        mpv_set_wakeup_callback(m_mpv, nullptr, nullptr);
        mpv_terminate_destroy(m_mpv);
    }
}

QString MpvPlayer::name() const { return tr("This computer"); }

Capabilities MpvPlayer::capabilities() const
{
    return Capability::SubtitleDelay | Capability::SubtitleStyle | Capability::AudioDelay
         | Capability::Chapters | Capability::FrameStep | Capability::Screenshot
         | Capability::AbLoop | Capability::AspectRatio | Capability::Crop
         | Capability::VideoEqualizer | Capability::HardwareDecoding | Capability::NativeKeys
         | Capability::UpscaleFsr | Capability::UpscaleSgsr | Capability::UpscaleCas
         | Capability::UpscaleAnime4k | m_vsrCaps;
}

void MpvPlayer::setFullScreen(bool on) { setProperty("fullscreen", on ? "yes" : "no"); }

// --- Plumbing -----------------------------------------------------------------

void MpvPlayer::command(std::initializer_list<const char*> args, uint64_t replyId)
{
    std::vector<const char*> argv(args);
    argv.push_back(nullptr);
    if (const int err = mpv_command_async(m_mpv, replyId, argv.data()); err < 0)
        qCWarning(lcTargets) << "mpv command" << argv.front() << "failed:" << mpv_error_string(err);
}

void MpvPlayer::setProperty(const char* name, const QByteArray& value)
{
    // Strings are converted by mpv to the property's own type.
    const char* v = value.constData();
    mpv_set_property_async(m_mpv, 0, name, MPV_FORMAT_STRING, &v);
}

void MpvPlayer::setState(PlaybackState s)
{
    if (s == m_state)
        return;
    m_state = s;
    ++m_presentationEpoch;
    emit stateChanged(s);
}

void MpvPlayer::updateState()
{
    if (!m_fileLoaded)
        return;
    setState(m_eof ? PlaybackState::Ended : m_paused ? PlaybackState::Paused : PlaybackState::Playing);
}

// --- Commands -------------------------------------------------------------------

void MpvPlayer::load(const PlayableSource& source, double startSeconds)
{
    const QUrl& url = source.url;
    m_http->reset();
    const QUrl routed = m_http->routeVideo(url, source.headers);
    const QByteArray target = url.isLocalFile() ? QFile::encodeName(url.toLocalFile())
                                                : routed.toString(QUrl::FullyEncoded).toUtf8();
    m_fileLoaded = false;
    m_eof = false;
    m_pendingSubtitles = source.subtitles;
    setState(PlaybackState::Loading);
    const QString shown = logging::redactUrl(url);
    if (m_mode == Mode::Video) {
        m_pendingTarget = target;
        m_pendingShown = shown;
        m_pendingStart = startSeconds;
        ++m_renderLoadId;
        emit renderLoadRequested();
        return;
    }
    startLoad(target, shown, startSeconds);
}

void MpvPlayer::startLoad(const QByteArray& target, const QString& shown, double startSeconds)
{
    qCInfo(lcTargets).noquote() << "Opening" << shown;
    // "start" applies to the next file loaded; reset it every time.
    mpv_set_property_string(m_mpv, "start", startSeconds > 0 ? number(startSeconds).constData() : "none");
    mpv_set_property_string(m_mpv, "pause", "no");
    if (!m_cacheSizeFromEnv) {
        // mpv's buffer is memory on top of everything else. A web link has no
        // other buffer, so it gets a big one. A torrent already has the
        // broker's (minutes ahead, on disk or in RAM) and a local file needs
        // none: 256 MB there was a second copy of what the broker holds
        // (measured 2026-11-14 on a 1080p torrent in disk mode: the app held
        // 810 MB with 256 MB, 590 MB with 64 MB). Nor does a back buffer
        // help there: going back re-reads from the disk at once, and at 4K
        // 32 MB was under 10 s, so a step back missed it anyway (a 4K file
        // measured 2026-11-18 on Linux: 1005 MB with 64 + 32 MB, 946 with 32 + 0).
        const bool web = target.startsWith("http") || target.startsWith("cjhttp");
        mpv_set_property_string(m_mpv, "demuxer-max-bytes", web ? "256MiB" : "32MiB");
        mpv_set_property_string(m_mpv, "demuxer-max-back-bytes", web ? "64MiB" : "0");
    }
    applyLoadPolicy(target);
    ++m_loadsInFlight;
    command({"loadfile", target.constData(), "replace"});
}

// What a file may make mpv open by itself (security review 2026-11-15, see
// docs/SECURITY.md). Playlists, mpv EDL lists, CUE sheets and FFmpeg's HLS
// lists all name further files, and mpv opens them. mpv guards that only for
// links it opens itself: an http(s) list may name only other web links. Our
// torrent:// and cjhttp:// streams come through mpv's stream callbacks, which
// mpv trusts like a path the user typed, so a playlist inside a torrent could
// name C:\ files or \\server\share paths (a password-hash leak on Windows).
//  - Torrents and local videos: no references at all. Nothing a player of
//    single videos needs is lost; a local MKV that chains other files
//    (ordered chapters) plays its own part only.
//  - A local file the user picked whose name says it is a list (.m3u, .cue...):
//    references on, as in any player.
//  - Web links: references on, since HLS needs them; onLoadHook() keeps every
//    entry a web link, and ScopedHttp refuses EDL and CUE answers.
void MpvPlayer::applyLoadPolicy(const QByteArray& target)
{
    static const QList<QByteArray> kListSuffixes = {".m3u", ".m3u8", ".pls", ".cue", ".edl"};
    const QByteArray lower = target.toLower();
    m_webLoad = lower.startsWith("http://") || lower.startsWith("https://") || lower.startsWith("cjhttp://")
             || lower.startsWith("cjhttps://");
    const bool localList = !lower.contains("://")
        && std::any_of(kListSuffixes.begin(), kListSuffixes.end(), [&](const QByteArray& s) { return lower.endsWith(s); });
    mpv_set_property_string(m_mpv, "access-references", m_webLoad || localList ? "yes" : "no");
    // Plain files can read ahead directly from disk. Keep the memory cache
    // for streamed sources, including the app's custom torrent protocol,
    // and for playlists that can contain streams.
    if (!m_cacheModeFromEnv) {
        const bool stream = lower.contains("://");
        mpv_set_property_string(m_mpv, "cache", stream || localList ? "yes" : "no");
    }
}

// mpv's on_load hook: runs before each file opens, playlist entries
// included. While a web link plays, every entry must be a web link too.
// mpv waits until mpv_hook_continue(), so this answers at once.
void MpvPlayer::onLoadHook(uint64_t hookId)
{
    if (m_webLoad) {
        char* name = mpv_get_property_string(m_mpv, "stream-open-filename");
        const QByteArray entry = name ? QByteArray(name).toLower() : QByteArray();
        mpv_free(name);
        const bool web = entry.startsWith("http://") || entry.startsWith("https://")
                      || entry.startsWith("cjhttp://") || entry.startsWith("cjhttps://");
        if (!web) {
            qCWarning(lcTargets) << "Refusing a playlist entry that is not a web link";
            // A protocol nobody registered: mpv fails this entry and goes on.
            mpv_set_property_string(m_mpv, "stream-open-filename", "cj-refused://");
        }
    }
    mpv_hook_continue(m_mpv, hookId);
}

void MpvPlayer::updateViewportHeight(int height, uint64_t loadId)
{
    if (m_shownHeight != height) {
        m_shownHeight = height;
        if (m_fileLoaded && isVsr(m_upscaler) && m_upscaleHeight == 0)
            m_vsrTimer->start();
        else
            updateUpscalerStatus();
    }
    if (loadId != 0 && loadId == pendingRenderLoad())
        startLoad(std::exchange(m_pendingTarget, {}), m_pendingShown, m_pendingStart);
}

void MpvPlayer::prepareVsrFilter()
{
    m_vsrTimer->stop();
    if (!isVsr(m_upscaler))
        return;
    // This hook runs before mpv builds the video chain. With ambiguous or
    // unavailable demuxer dimensions, the decoded height still sets the filter.
    int height = 0;
    mpv_node tracks{};
    if (mpv_get_property(m_mpv, "track-list", MPV_FORMAT_NODE, &tracks) >= 0) {
        if (tracks.format == MPV_FORMAT_NODE_ARRAY) {
            int videoTracks = 0;
            for (int i = 0; i < tracks.u.list->num; ++i) {
                const auto& track = tracks.u.list->values[i];
                if (nodeString(track, "type") == QLatin1String("video") && !nodeFlag(track, "image")) {
                    ++videoTracks;
                    height = int(nodeInt(track, "demux-h"));
                }
            }
            if (videoTracks != 1)
                height = 0;
        }
        mpv_free_node_contents(&tracks);
    }
    const QString filter = vsrFilter(m_upscaler, height, m_upscaleHeight, m_shownHeight, m_intelCopyback);
    if (filter != m_vsrFilter && mpv_set_property_string(m_mpv, "vf", filter.toUtf8().constData()) >= 0) {
        m_vsrFilter = filter;
        if (!filter.isEmpty())
            qCInfo(lcTargets).noquote() << "Prepared video filter:" << filter;
    }
}

void MpvPlayer::play()  { setProperty("pause", "no"); }
void MpvPlayer::pause() { setProperty("pause", "yes"); }

void MpvPlayer::stop()
{
    m_pendingTarget.clear();
    // A loadfile mpv has not started yet never will: it gets no START_FILE,
    // so stop counting it, or the state would stay Loading for good.
    m_loadsInFlight = 0;
    command({"stop"});
    if (!m_fileLoaded)
        setState(PlaybackState::Idle);
}

void MpvPlayer::seek(double seconds)
{
    const QByteArray pos = number(qMax(0.0, seconds));
    command({"seek", pos.constData(), "absolute"});
}

void MpvPlayer::setAudioTrack(int id)    { setProperty("aid", id < 0 ? "no" : QByteArray::number(id)); }
void MpvPlayer::setSubtitleTrack(int id) { setProperty("sid", id < 0 ? "no" : QByteArray::number(id)); }

void MpvPlayer::addExternalSubtitle(const QUrl& url)
{
    addSubtitleNow(SubtitleSource{url, {}, {}}, "select");
}

// An audio file the user picked on this computer (a dub, a commentary).
// Only local files: a web link would bypass ScopedHttp.
void MpvPlayer::addExternalAudio(const QUrl& url)
{
    if (!url.isLocalFile()) {
        qCWarning(lcTargets).noquote() << "Skipping an audio track that is not a local file:" << logging::redactUrl(url);
        return;
    }
    const QByteArray path = QFile::encodeName(url.toLocalFile());
    command({"audio-add", path.constData(), "select"});
}

void MpvPlayer::addSubtitleTrack(const SubtitleSource& subtitle)
{
    if (m_fileLoaded)
        addSubtitleNow(subtitle, "auto");
    else
        m_pendingSubtitles.append(subtitle);
}

// A subtitle is a local file the user picked or a web link from an add-on.
// Web links go through ScopedHttp: the stream's credentials reach only the
// stream's own server, and the download is capped.
void MpvPlayer::addSubtitleNow(const SubtitleSource& subtitle, const char* flag)
{
    QByteArray path;
    if (subtitle.url.isLocalFile()) {
        path = QFile::encodeName(subtitle.url.toLocalFile());
    } else {
        const QUrl routed = m_http->routeSubtitle(subtitle.url, kMaxSubtitleBytes);
        if (routed == subtitle.url) {
            qCWarning(lcTargets).noquote() << "Skipping a subtitle that is neither a local file nor a web link:"
                                           << logging::redactUrl(subtitle.url);
            return;
        }
        path = routed.toString(QUrl::FullyEncoded).toUtf8();
    }
    addSubtitleFile(path, flag, subtitle);
}

void MpvPlayer::addSubtitleFile(const QByteArray& path, const char* flag, const SubtitleSource& subtitle)
{
    const QByteArray title = (subtitle.label.isEmpty() ? subtitle.language : subtitle.label).toUtf8();
    const QByteArray lang = subtitle.language.toUtf8();
    // "auto": add it as a choice; mpv's own language rules pick what shows.
    // "select": the user picked this file, so show it.
    if (title.isEmpty() && lang.isEmpty())
        command({"sub-add", path.constData(), flag});
    else
        command({"sub-add", path.constData(), flag, title.constData(), lang.constData()});
}

void MpvPlayer::setSpeed(double factor)  { setProperty("speed", number(factor)); }
void MpvPlayer::setVolume(double volume) { setProperty("volume", number(qBound(0.0, volume, kMaxVolume))); }
void MpvPlayer::setMuted(bool muted)     { setProperty("mute", muted ? "yes" : "no"); }

void MpvPlayer::setSubtitleDelay(double seconds) { setProperty("sub-delay", number(seconds)); }
void MpvPlayer::setSubtitleScale(double factor)  { setProperty("sub-scale", number(factor)); }

void MpvPlayer::setSubtitleStyle(const SubtitleStyle& style)
{
    setProperty("sub-font", style.font.isEmpty() ? QByteArray("sans-serif") : style.font.toUtf8());
    setProperty("sub-color", style.color.toLatin1());  // "#RRGGBB", checked by the PlaybackSession
    setProperty("sub-border-size", number(style.outline));
    // mpv colors with alpha are #AARRGGBB.
    const QByteArray box = "#" + QByteArray::number(qRound(style.background * 2.55), 16).rightJustified(2, '0') + "000000";
    // A box behind the text that keeps the outline. mpv 0.38 and later; the
    // app ships 0.39, older system builds just show no box.
    const char* border = style.background > 0 ? "background-box" : "outline-and-shadow";
    if (mpv_set_property_string(m_mpv, "sub-border-style", border) < 0 && style.background > 0)
        qCInfo(lcTargets) << "This mpv cannot draw a box behind subtitles";
    if (style.background > 0)
        setProperty("sub-back-color", box);
    // Styled subtitles (ASS, common for anime) keep their own look unless asked.
    setProperty("sub-ass-override", style.restyleAll ? "force" : "scale");
}

void MpvPlayer::setAudioDelay(double seconds)    { setProperty("audio-delay", number(seconds)); }
void MpvPlayer::seekChapter(int index)           { setProperty("chapter", QByteArray::number(index)); }

void MpvPlayer::frameStep(bool forward)
{
    command({forward ? "frame-step" : "frame-back-step"});
}

void MpvPlayer::screenshot(const QString& filePath)
{
    const uint64_t id = m_nextReplyId++;
    m_pendingScreenshots.insert(id, filePath);
    const QByteArray path = QFile::encodeName(filePath);
    command({"screenshot-to-file", path.constData(), "video"}, id);
}

void MpvPlayer::setAbLoop(double a, double b)
{
    setProperty("ab-loop-a", a < 0 ? "no" : number(a));
    setProperty("ab-loop-b", b < 0 ? "no" : number(b));
}

void MpvPlayer::setAspectRatio(const QString& ratio)
{
    setProperty("video-aspect-override", ratio.isEmpty() ? "no" : ratio.toUtf8());
}

void MpvPlayer::setCrop(const QString& ratio)
{
    m_cropRatio = ratio;
    applyCrop();
}

// Crop is given as an aspect ratio; mpv wants pixels, so it is recomputed
// whenever the video size is known or changes.
void MpvPlayer::applyCrop()
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

void MpvPlayer::setVideoEqualizer(const VideoEqualizer& eq)
{
    setProperty("brightness", QByteArray::number(eq.brightness));
    setProperty("contrast",   QByteArray::number(eq.contrast));
    setProperty("saturation", QByteArray::number(eq.saturation));
    setProperty("gamma",      QByteArray::number(eq.gamma));
    setProperty("hue",        QByteArray::number(eq.hue));
}

void MpvPlayer::setHardwareDecoding(bool enabled)
{
    m_hwdecEnabled = enabled;
    setProperty("hwdec", !enabled ? "no" : isVsr(m_upscaler) ? "d3d11va" : kHwdecOn);
    qCInfo(lcTargets) << "Hardware decoding" << (enabled ? "on" : "off");
    emitStats();
}

// An upscaler's shader (FSR, SGSR, CAS or Anime4K), sized for 'targetHeight' (0 = the size
// the video is shown at), as an mpv shader file. Null if it cannot be written.
std::unique_ptr<QTemporaryFile> MpvPlayer::writeShader(Upscaler upscaler, int targetHeight)
{
    const QString name = upscaler == Upscaler::Sgsr    ? QStringLiteral("sgsr1")
                       : upscaler == Upscaler::Cas     ? QStringLiteral("cas")
                       : upscaler == Upscaler::Anime4k ? QStringLiteral("anime4k_upscale_s")
                                                       : QStringLiteral("fsr1");
    QFile source(QStringLiteral(":/cj/shaders/%1.glsl").arg(name));
    if (!source.open(QIODevice::ReadOnly))
        return nullptr;
    // mpv reads "//!" lines up to '\n'; a '\r' from a Windows checkout would
    // end up inside the size expressions.
    QByteArray text = source.readAll().replace("\r\n", "\n");
    QByteArray when, width, height;
    if (targetHeight <= 0) {
        when = "OUTPUT.w LUMA.w / 1.0 > OUTPUT.h LUMA.h / 1.0 > *";
        width = "OUTPUT.w";
        height = "OUTPUT.h";
    } else {
        const QByteArray h = QByteArray::number(targetHeight);
        when = h + " LUMA.h / 1.0 >";
        width = "LUMA.w " + h + " * LUMA.h /";
        height = h;
    }
    if (upscaler == Upscaler::Anime4k) {
        // It hooks MAIN and doubles; it is worth running from 1.2x up.
        when = targetHeight <= 0 ? QByteArray("OUTPUT.w MAIN.w / 1.200 > OUTPUT.h MAIN.h / 1.200 > *")
                                 : QByteArray::number(targetHeight) + " MAIN.h / 1.200 >";
    }
    text.replace("@WHEN@", when).replace("@WIDTH@", width).replace("@HEIGHT@", height);

    auto file = std::make_unique<QTemporaryFile>(QDir::tempPath() + QStringLiteral("/cj-%1-XXXXXX.glsl").arg(name));
    if (!file->open() || file->write(text) != text.size() || !file->flush())
        return nullptr;
    file->close();  // the file stays until the object goes
    return file;
}

// The filter is set again at once when Intel VSR is the one running.
void MpvPlayer::setIntelVsrCopyback(bool on)
{
    if (on == m_intelCopyback)
        return;
    m_intelCopyback = on;
    if (m_upscaler == Upscaler::IntelVsr)
        applyVsrFilter();
}

// Off clears the shader list and the filter, so mpv frees what they used.
void MpvPlayer::setUpscaler(Upscaler upscaler, int targetHeight)
{
    if (upscaler == m_upscaler && targetHeight == m_upscaleHeight)
        return;
    const bool wasVsr = isVsr(m_upscaler);
    command({"change-list", "glsl-shaders", "clr", ""});
    m_upscaler = Upscaler::Off;
    m_upscaleHeight = targetHeight;
    if (isShader(upscaler)) {
        // Kept until mpv has dropped it: the new file replaces it below.
        std::unique_ptr<QTemporaryFile> file = writeShader(upscaler, targetHeight);
        if (file) {
            command({"change-list", "glsl-shaders", "append", file->fileName().toUtf8().constData()});
            m_shaderFile = std::move(file);
            m_upscaler = upscaler;
        } else {
            qCWarning(lcTargets) << "Could not write the upscaler's shader; upscaling stays off";
        }
    } else if (isVsr(upscaler) && m_vsrCaps.testFlag(upscaler == Upscaler::NvidiaVsr ? Capability::UpscaleNvidiaVsr
                                                                                      : Capability::UpscaleIntelVsr)) {
        m_upscaler = upscaler;
    } else if (upscaler == Upscaler::MetalFx && m_vsrCaps.testFlag(Capability::UpscaleMetalFx)) {
        m_upscaler = upscaler;  // the video surface does the work (MpvVideoItem)
    } else if (upscaler != Upscaler::Off) {
        qCWarning(lcTargets) << "Upscaler" << int(upscaler) << "is not available here";
    }
    if (!isShader(m_upscaler))
        m_shaderFile.reset();

    // VSR decodes into Direct3D 11 frames, so the filter can work on them.
    if (isVsr(m_upscaler) != wasVsr && m_hwdecEnabled)
        setProperty("hwdec", isVsr(m_upscaler) ? "d3d11va" : kHwdecOn);
    m_vsrTimer->stop();
    applyVsrFilter();

    qCInfo(lcTargets).noquote() << "Upscaler:" << upscalerLabel();
    updateUpscalerStatus();
    emitStats();
}

void MpvPlayer::applyVsrFilter()
{
    const QString filter = vsrFilter(m_upscaler, m_videoHeight, m_upscaleHeight, m_shownHeight, m_intelCopyback);
    if (filter == m_vsrFilter)
        return;
    m_vsrFilter = filter;
    setProperty("vf", filter.toUtf8());
    if (!filter.isEmpty())
        qCInfo(lcTargets).noquote() << "Video filter:" << filter;
    updateUpscalerStatus();
}

void MpvPlayer::renderUpscalerFailed()
{
    if (m_upscaler == Upscaler::MetalFx)
        turnUpscalerOff(QStringLiteral("MetalFX failed on this Mac"));
}

void MpvPlayer::turnUpscalerOff(const QString& why)
{
    const QString name = upscalerName(m_upscaler);
    qCWarning(lcTargets).noquote() << why << "; upscaling turned off";
    setUpscaler(Upscaler::Off, m_upscaleHeight);
    emit settingChanged(QStringLiteral("upscaler"), QStringLiteral("off"));
    m_upscalerStatus = QStringLiteral("%1 off · this graphics card or driver could not run it").arg(name);
    emit upscalerStatusChanged(m_upscalerStatus, true);
}

void MpvPlayer::updateUpscalerStatus()
{
    const int target = m_upscaleHeight > 0 ? m_upscaleHeight : m_shownHeight;
    const QString status = upscalerStatus(m_upscaler, m_videoHeight, target, m_outHeight, !m_vsrFilter.isEmpty());
    if (status == m_upscalerStatus)
        return;
    m_upscalerStatus = status;
    if (!status.isEmpty())
        qCInfo(lcTargets).noquote() << "Upscaler status:" << status;
    emit upscalerStatusChanged(status, false);
    emitStats();
}

// --- Events -----------------------------------------------------------------------

void MpvPlayer::queueEventDrain()
{
    if (!m_eventDrainQueued.exchange(true))
        QMetaObject::invokeMethod(this, &MpvPlayer::processEvents, Qt::QueuedConnection);
}

void MpvPlayer::processEvents()
{
    // Clear before draining so a wake during the drain can post a follow-up.
    m_eventDrainQueued.store(false);
    // At most 32 events per call, so input and drawing get their turn even
    // if mpv keeps posting. The rest wait for the next call, queued below.
    for (int handled = 0; m_mpv && handled < 32; ++handled) {
        mpv_event* event = mpv_wait_event(m_mpv, 0);
        switch (event->event_id) {
        case MPV_EVENT_NONE:
            return;  // nothing left to read
        case MPV_EVENT_PROPERTY_CHANGE:
            handleProperty(event->reply_userdata, static_cast<mpv_event_property*>(event->data));
            break;
        case MPV_EVENT_START_FILE:
            onFileStarting();
            break;
        case MPV_EVENT_SEEK:
            ++m_presentationEpoch;
            break;
        case MPV_EVENT_FILE_LOADED:
            onFileLoaded();
            break;
        case MPV_EVENT_END_FILE:
            onFileEnded(static_cast<mpv_event_end_file*>(event->data));
            break;
        case MPV_EVENT_COMMAND_REPLY:
            onCommandFinished(event);
            break;
        case MPV_EVENT_LOG_MESSAGE:
            onMpvLog(static_cast<mpv_event_log_message*>(event->data));
            break;
        case MPV_EVENT_HOOK: {
            // mpv pauses loading until the hook is answered with mpv_hook_continue.
            const auto* hook = static_cast<mpv_event_hook*>(event->data);
            if (event->reply_userdata == kOnLoadHook) {
                onLoadHook(hook->id);  // answers the hook itself
            } else {
                if (event->reply_userdata == kOnPreloadedHook)
                    prepareVsrFilter();
                mpv_hook_continue(m_mpv, hook->id);
            }
            break;
        }
        case MPV_EVENT_CLIENT_MESSAGE: {
            // A key binding in mpv asking the app to do something ("cj-app <action>").
            const auto* msg = static_cast<mpv_event_client_message*>(event->data);
            if (msg->num_args == 2 && qstrcmp(msg->args[0], "cj-app") == 0)
                emit appActionRequested(QString::fromUtf8(msg->args[1]));
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
    if (m_mpv)
        queueEventDrain();
}

void MpvPlayer::onFileStarting()
{
    m_loadsInFlight = qMax(0, m_loadsInFlight - 1);
    m_fileLoaded = false;
    ++m_presentationEpoch;
    m_presentationGaps = 0;
    m_longestPresentationGap = 0;
    setState(PlaybackState::Loading);
    emitStats();
}

void MpvPlayer::onFileLoaded()
{
    qCInfo(lcTargets) << "File loaded";
    m_fileLoaded = true;
    m_eof = false;
    // Subtitles chosen before the file was ready.
    for (const SubtitleSource& sub : std::exchange(m_pendingSubtitles, {}))
        addSubtitleNow(sub, "auto");
    applyCrop();
    updateState();
}

void MpvPlayer::onFileEnded(const mpv_event_end_file* end)
{
    const bool failed = end->reason == MPV_END_FILE_REASON_ERROR;
    const bool stopped = end->reason == MPV_END_FILE_REASON_STOP || end->reason == MPV_END_FILE_REASON_QUIT;
    if (!failed && !stopped)
        return;  // the end of the file or a redirect: nothing to do here
    m_fileLoaded = false;
    // STOP also fires when a new file replaces this one; stay in Loading
    // then instead of flashing Idle.
    if (m_loadsInFlight == 0 && m_pendingTarget.isEmpty())
        setState(PlaybackState::Idle);
    if (!failed)
        return;

    const QString message = QString::fromUtf8(mpv_error_string(end->error));
    PlaybackError kind = PlaybackError::Internal;
    if (end->error == MPV_ERROR_UNKNOWN_FORMAT)
        kind = PlaybackError::UnsupportedFile;
    else if (end->error == MPV_ERROR_LOADING_FAILED)
        kind = PlaybackError::LoadFailed;
    qCWarning(lcTargets).noquote() << "Playback failed:" << message;
    emit errorOccurred(kind, message);
}

// Only screenshots ask for a reply today.
void MpvPlayer::onCommandFinished(const mpv_event* event)
{
    const auto it = m_pendingScreenshots.find(event->reply_userdata);
    if (it == m_pendingScreenshots.end())
        return;
    const QString path = it.value();
    m_pendingScreenshots.erase(it);
    if (event->error >= 0) {
        qCInfo(lcTargets).noquote() << "Screenshot saved to" << path;
        emit screenshotSaved(path);
    } else {
        qCWarning(lcTargets) << "Screenshot failed:" << mpv_error_string(event->error);
    }
}

// mpv's own log. Warnings and errors go to our log (with private paths
// removed); the rest only with CJ_MPV_LOG set. A warning from the renderer
// about an upscaler turns that upscaler off.
void MpvPlayer::onMpvLog(const mpv_event_log_message* message)
{
    if (message->log_level > MPV_LOG_LEVEL_WARN && !qEnvironmentVariableIsSet("CJ_MPV_LOG")) {
        if (std::strstr(message->text, "d3d11-dxinterop"))
            qCInfo(lcTargets).noquote() << QString::fromUtf8(message->text).trimmed();
        return;
    }
    const QString text = QString::fromUtf8(message->text).trimmed();
    qCWarning(lcTargets).noquote() << "mpv" << message->prefix << ":" << logging::redactText(text);
    if (message->log_level > MPV_LOG_LEVEL_WARN)
        return;

    // A graphics driver that cannot build the upscaler's shader would
    // leave a broken picture: fall back to mpv's own scaling.
    // (The renderer logs as "vo/..." or, through the render API, "libmpv_render".)
    const bool fromRenderer = qstrncmp(message->prefix, "vo", 2) == 0
                              || qstrncmp(message->prefix, "libmpv_render", 13) == 0;
    const bool aboutShader = text.contains(QLatin1String("shader"), Qt::CaseInsensitive)
                             || text.contains(QLatin1String("error"), Qt::CaseInsensitive);
    if (isShader(m_upscaler) && fromRenderer && aboutShader)
        turnUpscalerOff(QStringLiteral("The upscaler's shader failed on this graphics driver"));

    // The same for VSR: the driver refused it, or the filter could
    // not start (no Direct3D 11 device: another GPU drives the window).
    const bool aboutVsr = text.contains(QLatin1String("Super Resolution")) || text.contains(QLatin1String("d3d11vpp"))
                          || QByteArray(message->prefix).contains("d3d11vpp");
    if (isVsr(m_upscaler) && aboutVsr)
        turnUpscalerOff(QStringLiteral("Video Super Resolution failed on this graphics card"));
}

void MpvPlayer::handleProperty(uint64_t id, const mpv_event_property* p)
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
        // Without a title tag mpv falls back to the file name; the PlaybackSession
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
        m_outHeight = 0;  // the next file's filtered size is not known yet
        applyCrop();
        applyVsrFilter();
        updateUpscalerStatus();
        emitStats();
        break;
    case PropOutHeight:
        m_outHeight = int(intOf(p));
        updateUpscalerStatus();
        break;
    case PropFps:
        m_fps = doubleOf(p);
        emitStats();
        break;
    case PropDropped:
        m_dropped = intOf(p);
        emitStats();
        break;
    case PropDecoderDropped:
        m_decoderDropped = intOf(p);
        emitStats();
        break;
    case PropDelayed:
        m_delayed = intOf(p);
        emitStats();
        break;
    case PropMistimed:
        m_mistimed = intOf(p);
        emitStats();
        break;
    case PropSpeed:
        if (p->format == MPV_FORMAT_DOUBLE) emit settingChanged(QStringLiteral("speed"), doubleOf(p));
        break;
    case PropSubDelay:
        if (p->format == MPV_FORMAT_DOUBLE) emit settingChanged(QStringLiteral("subtitleDelay"), doubleOf(p));
        break;
    case PropAudioDelay:
        if (p->format == MPV_FORMAT_DOUBLE) emit settingChanged(QStringLiteral("audioDelay"), doubleOf(p));
        break;
    case PropSubScale:
        if (p->format == MPV_FORMAT_DOUBLE) emit settingChanged(QStringLiteral("subtitleScale"), doubleOf(p));
        break;
    case PropAbLoopA:
    case PropAbLoopB: {
        // "no" (unset) has no number: format NONE.
        const double v = p->format == MPV_FORMAT_DOUBLE ? doubleOf(p) : -1.0;
        emit settingChanged(id == PropAbLoopA ? QStringLiteral("abLoopA") : QStringLiteral("abLoopB"), v < 0 ? -1.0 : v);
        break;
    }
    case PropAspect:
        // "16:9", "1.777778", or "-1"/"no" for the file's own.
        emit settingChanged(QStringLiteral("aspectRatio"), qMax(0.0, parseRatio(stringOf(p))));
        break;
    case PropBrightness: emit settingChanged(QStringLiteral("brightness"), int(intOf(p))); break;
    case PropContrast:   emit settingChanged(QStringLiteral("contrast"), int(intOf(p))); break;
    case PropSaturation: emit settingChanged(QStringLiteral("saturation"), int(intOf(p))); break;
    case PropGamma:      emit settingChanged(QStringLiteral("gamma"), int(intOf(p))); break;
    case PropHue:        emit settingChanged(QStringLiteral("hue"), int(intOf(p))); break;
    case PropHwdec: {
        const QString hwdec = stringOf(p);
        if (hwdec.isEmpty())
            break;
        const bool enabled = hwdec != QLatin1String("no");
        if (enabled != m_hwdecEnabled) {
            m_hwdecEnabled = enabled;
            emitStats();
        }
        emit settingChanged(QStringLiteral("hardwareDecoding"), enabled);
        break;
    }
    case PropFullscreen:
        emit settingChanged(QStringLiteral("fullScreen"), flagOf(p));
        break;
    }
}

QString MpvPlayer::upscalerLabel() const
{
    QString name;
    switch (m_upscaler) {
    case Upscaler::Off:       return QStringLiteral("off");
    case Upscaler::Fsr:       name = QStringLiteral("AMD FSR 1"); break;
    case Upscaler::Sgsr:      name = QStringLiteral("Snapdragon GSR"); break;
    case Upscaler::Cas:       name = QStringLiteral("AMD CAS"); break;
    case Upscaler::Anime4k:   name = QStringLiteral("Anime4K"); break;
    case Upscaler::NvidiaVsr: name = QStringLiteral("NVIDIA VSR"); break;
    case Upscaler::IntelVsr:  name = QStringLiteral("Intel VSR"); break;
    case Upscaler::MetalFx:   name = QStringLiteral("Apple MetalFX"); break;
    }
    return name + (m_upscaleHeight > 0 ? QStringLiteral(" to %1p").arg(m_upscaleHeight) : QStringLiteral(" to window size"));
}

void MpvPlayer::reportPresentationGap(uint64_t epoch, qint64 milliseconds)
{
    if (epoch != m_presentationEpoch)
        return;
    ++m_presentationGaps;
    m_longestPresentationGap = qMax(m_longestPresentationGap, milliseconds);
    qCDebug(lcTargets) << "Qt presentation gap:" << milliseconds << "ms";
    emitStats();
}

void MpvPlayer::emitStats()
{
    const bool hw = !m_hwdecCurrent.isEmpty() && m_hwdecCurrent != QLatin1String("no");
    emit statsChanged({
        {QStringLiteral("Decoder"), hw ? m_hwdecCurrent + QStringLiteral(" (hardware)") : QStringLiteral("software")},
        {QStringLiteral("HW decoding"), m_hwdecEnabled ? QStringLiteral("allowed") : QStringLiteral("off")},
        {QStringLiteral("Codec"), m_videoCodec.isEmpty() ? QStringLiteral("-") : m_videoCodec},
        {QStringLiteral("Resolution"), m_videoWidth > 0 ? QStringLiteral("%1×%2").arg(m_videoWidth).arg(m_videoHeight) : QStringLiteral("-")},
        {QStringLiteral("Upscaler"), upscalerLabel()},
        {QStringLiteral("Upscaler now"), m_upscalerStatus.isEmpty() ? QStringLiteral("-") : m_upscalerStatus},
        {QStringLiteral("FPS"), m_fps > 0 ? QString::number(m_fps, 'f', 2) : QStringLiteral("-")},
        // Dropped by the output (too slow to draw, or timing), dropped by
        // the decoder (too slow to decode), late, and shown at the wrong
        // refresh: which one grows says why playback stutters.
        {QStringLiteral("Dropped frames"), QStringLiteral("%1 drawing, %2 decoding").arg(m_dropped).arg(m_decoderDropped)},
        {QStringLiteral("Late frames"), QStringLiteral("%1 delayed, %2 mistimed").arg(m_delayed).arg(m_mistimed)},
        {QStringLiteral("Qt presentation gaps"), m_mode == Mode::Video
            ? QStringLiteral("%1 (%2 ms longest)").arg(m_presentationGaps).arg(m_longestPresentationGap)
            : QStringLiteral("—")},
    });
}

QList<Track> MpvPlayer::parseTracks(const mpv_node* node)
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

QList<Chapter> MpvPlayer::parseChapters(const mpv_node* node)
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
