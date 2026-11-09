#pragma once
#include "PlaybackTarget.h"

#include <QByteArray>
#include <QHash>
#include <QUrl>
#include <cstdint>
#include <memory>

struct mpv_handle;
struct mpv_event_property;
struct mpv_node;

namespace cj {

class ScopedHttp;

// The playback target for this computer, built on mpv. Owns the app's single
// mpv instance (rule 3): create it once and reuse it for every item.
//
// Implements docs/TARGET_CONTRACT.md. Must be used from the thread that
// created it; mpv's events are hopped onto that thread before being emitted.
class MpvTarget : public PlaybackTarget
{
    Q_OBJECT

public:
    enum class Mode {
        Video,     // renders through the VideoSurface item (vo=libmpv)
        Headless,  // no video or audio output; for tests
    };

    explicit MpvTarget(Mode mode = Mode::Video, QObject* parent = nullptr);
    ~MpvTarget() override;

    // The raw handle, only for the render bridge in this layer.
    mpv_handle* handle() const { return m_mpv; }

    // Called by the video surface once its render context exists. In Video
    // mode mpv cannot open a file before that, so earlier loads wait for it.
    Q_INVOKABLE void markRenderReady();

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

private:
    Q_INVOKABLE void processEvents();
    void handleProperty(uint64_t id, const ::mpv_event_property* prop);
    void command(std::initializer_list<const char*> args, uint64_t replyId = 0);
    void setProperty(const char* name, const QByteArray& value);
    void startLoad(const QByteArray& target, const QString& shown, double startSeconds);
    void addSubtitleNow(const SubtitleSource& subtitle, const char* flag);
    void addSubtitleFile(const QByteArray& path, const char* flag, const SubtitleSource& subtitle);
    void setState(PlaybackState s);
    void updateState();
    void applyCrop();
    void emitStats();
    static QList<Track> parseTracks(const ::mpv_node* node);
    static QList<Chapter> parseChapters(const ::mpv_node* node);

    mpv_handle* m_mpv = nullptr;
    Mode m_mode;
    bool m_renderReady = false;
    QByteArray m_pendingTarget;
    int m_loadsInFlight = 0;      // loadfile sent, START_FILE not yet seen
    double m_pendingStart = 0;
    QString m_pendingShown;       // the pending link, as logs may show it
    std::unique_ptr<ScopedHttp> m_http;   // links with credentials, remote subtitles
    static constexpr qint64 kMaxSubtitleBytes = 20 * 1024 * 1024;  // a subtitle is small
    QList<SubtitleSource> m_pendingSubtitles;  // added once the file is loaded

    PlaybackState m_state = PlaybackState::Idle;
    bool m_fileLoaded = false;
    bool m_paused = false;
    bool m_eof = false;
    double m_volume = 100;
    bool m_muted = false;
    bool m_hwdecEnabled = true;
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

    uint64_t m_nextReplyId = 1000;
    QHash<uint64_t, QString> m_pendingScreenshots;
};

} // namespace cj
