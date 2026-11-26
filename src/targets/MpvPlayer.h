#pragma once
#include "PlaybackTarget.h"

#include <QByteArray>
#include <QHash>
#include <QUrl>
#include <atomic>
#include <cstdint>
#include <memory>

class QTemporaryFile;
class QTimer;

struct mpv_handle;
struct mpv_event_property;
struct mpv_event_end_file;
struct mpv_event_log_message;
struct mpv_event;
struct mpv_node;

namespace cj {

class ScopedHttp;

// The playback target for this computer, built on mpv. Owns the app's single
// mpv instance (rule 3): create it once and reuse it for every item.
//
// Implements docs/TARGET_CONTRACT.md. Must be used from the thread that
// created it; mpv's events are hopped onto that thread before being emitted.
class MpvPlayer : public PlaybackTarget
{
    Q_OBJECT

public:
    enum class Mode {
        Video,     // renders through the VideoSurface item (vo=libmpv)
        Headless,  // no video or audio output; for tests
    };

    explicit MpvPlayer(Mode mode = Mode::Video, QObject* parent = nullptr);
    ~MpvPlayer() override;

    // The raw handle, only for the render bridge in this layer.
    mpv_handle* handle() const { return m_mpv; }

    // A load can start once the renderer confirms it has a framebuffer for it.
    uint64_t pendingRenderLoad() const { return m_pendingTarget.isEmpty() ? 0 : m_renderLoadId; }
    void updateViewportHeight(int height, uint64_t loadId);
    // For the video surface, which runs MetalFX itself: what to draw.
    Upscaler upscaler() const { return m_upscaler; }
    int upscaleHeight() const { return m_upscaleHeight; }
    int videoWidth() const { return m_videoWidth; }
    int videoHeight() const { return m_videoHeight; }
    double videoFps() const { return m_fps; }
    uint64_t presentationEpoch() const { return m_presentationEpoch; }
    void reportPresentationGap(uint64_t epoch, qint64 milliseconds);
    // The video surface could not run MetalFX: turn it off.
    Q_INVOKABLE void renderUpscalerFailed();

    QString name() const override;
    Capabilities capabilities() const override;

    void load(const PlayableSource& source, double startSeconds) override;
    void play() override;
    void pause() override;
    void stop() override;
    void seek(double seconds) override;
    void setAudioTrack(int id) override;
    void setSubtitleTrack(int id) override;
    void addExternalSubtitle(const QUrl& url) override;
    void addExternalAudio(const QUrl& url) override;
    void addSubtitleTrack(const SubtitleSource& subtitle) override;
    void setSpeed(double factor) override;
    void setVolume(double volume) override;
    void setMuted(bool muted) override;

    void setSubtitleDelay(double seconds) override;
    void setSubtitleScale(double factor) override;
    void setSubtitleStyle(const SubtitleStyle& style) override;
    void setAudioDelay(double seconds) override;
    void seekChapter(int index) override;
    void frameStep(bool forward) override;
    void screenshot(const QString& filePath) override;
    void setAbLoop(double a, double b) override;
    void setAspectRatio(const QString& ratio) override;
    void setCrop(const QString& ratio) override;
    void setVideoEqualizer(const VideoEqualizer& eq) override;
    void setHardwareDecoding(bool enabled) override;
    void setUpscaler(Upscaler upscaler, int targetHeight) override;
    void setIntelVsrCopyback(bool on) override;
    bool keyEvent(int key, Qt::KeyboardModifiers modifiers, const QString& text, bool pressed) override;
    void setFullScreen(bool on) override;

    // A Qt key as mpv's input.conf names it ("SPACE", "Ctrl+s", "Shift+LEFT",
    // "F"), or empty if mpv has no name for it.
    static QString keyName(int key, Qt::KeyboardModifiers modifiers, const QString& text);
    // Which Video Super Resolution a graphics card has, from its DXGI vendor
    // id and name. Public for tests.
    static Capabilities vsrCapabilities(unsigned vendorId, const QString& adapterName);
    // mpv's vf for VSR: scale the video (videoHeight) up to targetHeight, or
    // to shownHeight when that is 0. Empty when there is nothing to do.
    // intelCopyback: Intel VSR's compatibility mode, asked of the filter.
    static QString vsrFilter(Upscaler upscaler, int videoHeight, int targetHeight, int shownHeight,
                             bool intelCopyback = false);
    // The line upscalerStatusChanged sends. target: the height to reach (the
    // shown height for "screen size"); outHeight: the height the video
    // filters hand on (a VSR filter that works makes it bigger than
    // videoHeight); filterSet: a VSR filter is in place. Public for tests.
    static QString upscalerStatus(Upscaler upscaler, int videoHeight, int target, int outHeight, bool filterSet);

signals:
    void renderLoadRequested();

private:
    // The steps of the constructor, in order.
    void chooseOutput(Mode mode);
    void findUpscalers(Mode mode);
    void setPlayerBehaviour();
    void setSafetyLimits();
    void setScreenshotNaming();
    void setOptionsFromEnvironment();
    void logLibraryVersions();
    void startMpvLog();
    void observeProperties();

    void queueEventDrain();
    // Reads what mpv reported since the last call, a few events at a time.
    void processEvents();
    // One handler for each kind of event processEvents() cares about.
    void handleProperty(uint64_t id, const ::mpv_event_property* prop);
    void onFileStarting();
    void onFileLoaded();
    void onFileEnded(const ::mpv_event_end_file* end);
    void onCommandFinished(const ::mpv_event* event);
    void onMpvLog(const ::mpv_event_log_message* message);
    void command(std::initializer_list<const char*> args, uint64_t replyId = 0);
    void setProperty(const char* name, const QByteArray& value);
    void startLoad(const QByteArray& target, const QString& shown, double startSeconds);
    void applyLoadPolicy(const QByteArray& target);
    void onLoadHook(uint64_t hookId);
    void prepareVsrFilter();
    void addSubtitleNow(const SubtitleSource& subtitle, const char* flag);
    void addSubtitleFile(const QByteArray& path, const char* flag, const SubtitleSource& subtitle);
    void setState(PlaybackState s);
    void updateState();
    void applyCrop();
    void bindAppKeys();
    void emitStats();
    QString upscalerLabel() const;
    void applyVsrFilter();
    void turnUpscalerOff(const QString& why);
    void updateUpscalerStatus();
    static std::unique_ptr<QTemporaryFile> writeShader(Upscaler upscaler, int targetHeight);
    static QList<Track> parseTracks(const ::mpv_node* node);
    static QList<Chapter> parseChapters(const ::mpv_node* node);

    mpv_handle* m_mpv = nullptr;
    std::atomic_bool m_eventDrainQueued = false;
    Mode m_mode;
    uint64_t m_renderLoadId = 0;
    QByteArray m_pendingTarget;
    int m_loadsInFlight = 0;      // loadfile sent, START_FILE not yet seen
    double m_pendingStart = 0;
    QString m_pendingShown;       // the pending link, as logs may show it
    std::unique_ptr<ScopedHttp> m_http;   // links with credentials, remote subtitles
    bool m_webLoad = false;               // the file playing is a web link (see applyLoadPolicy)
    bool m_cacheModeFromEnv = false;      // CJ_MPV_OPTS set mpv's cache mode: leave it
    bool m_cacheSizeFromEnv = false;      // CJ_MPV_OPTS set mpv's buffer size: leave it
    static constexpr qint64 kMaxSubtitleBytes = 20 * 1024 * 1024;  // a subtitle is small
    QList<SubtitleSource> m_pendingSubtitles;  // added once the file is loaded

    PlaybackState m_state = PlaybackState::Idle;
    bool m_fileLoaded = false;
    bool m_paused = false;
    bool m_eof = false;
    double m_volume = 100;
    bool m_muted = false;
    bool m_hwdecEnabled = true;
    Upscaler m_upscaler = Upscaler::Off;
    int m_upscaleHeight = 0;
    bool m_intelCopyback = true;   // Intel VSR compatibility mode (the filter's intel-copyback); on until told otherwise
    Capabilities m_vsrCaps;       // VSR (Windows) or MetalFX (macOS), found once at start
    QString m_vsrFilter;          // the vf now set, empty for none
    int m_shownHeight = 0;        // physical height of the video framebuffer
    int m_outHeight = 0;          // height after the video filters (video-out-params/h)
    QString m_upscalerStatus;     // the line last sent by upscalerStatusChanged
    QTimer* m_vsrTimer = nullptr; // re-sizes the filter once a resize settles
    // The upscaler's shader (FSR, SGSR, CAS or Anime4K) with its sizes filled in, written
    // out when it is picked and removed when it is turned off: mpv loads shaders from files
    // (and caches them by name, so each size gets a new file).
    std::unique_ptr<QTemporaryFile> m_shaderFile;
    bool m_pausedForCache = false;
    int m_cachePercent = 100;
    QString m_cropRatio;
    int m_videoWidth = 0;
    int m_videoHeight = 0;
    QString m_hwdecCurrent;
    QString m_videoCodec;
    double m_fps = 0;
    int64_t m_dropped = 0;
    int64_t m_decoderDropped = 0;
    int64_t m_delayed = 0;
    int64_t m_mistimed = 0;
    uint64_t m_presentationEpoch = 0;
    int m_presentationGaps = 0;
    qint64 m_longestPresentationGap = 0;

    uint64_t m_nextReplyId = 1000;
    QHash<uint64_t, QString> m_pendingScreenshots;
    QHash<int, QByteArray> m_heldKeys;  // Qt key -> the mpv name it went down as
};

} // namespace cj
