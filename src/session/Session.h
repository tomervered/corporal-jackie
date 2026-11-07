#pragma once
#include "PlaybackTarget.h"

#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

#include <functional>
#include <optional>

namespace cj {

// The single source of truth for playback. The UI binds only to this object;
// it never sees the target behind it (rule 1). The Session owns the current
// item and every playback setting, sends commands to the current target, and
// turns the target's events into the properties below.
class Session : public QObject
{
    Q_OBJECT
    Q_PROPERTY(cj::PlaybackState state READ state NOTIFY stateChanged)
    Q_PROPERTY(bool hasMedia READ hasMedia NOTIFY stateChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY stateChanged)
    Q_PROPERTY(bool paused READ paused NOTIFY stateChanged)
    Q_PROPERTY(QString targetName READ targetName CONSTANT)
    Q_PROPERTY(QUrl source READ source NOTIFY sourceChanged)
    Q_PROPERTY(QString title READ title NOTIFY titleChanged)
    Q_PROPERTY(double position READ position NOTIFY positionChanged)
    Q_PROPERTY(double duration READ duration NOTIFY durationChanged)
    Q_PROPERTY(bool buffering READ buffering NOTIFY bufferingChanged)
    Q_PROPERTY(int bufferPercent READ bufferPercent NOTIFY bufferingChanged)
    Q_PROPERTY(double volume READ volume WRITE setVolume NOTIFY volumeChanged)
    Q_PROPERTY(bool muted READ muted WRITE setMuted NOTIFY volumeChanged)
    Q_PROPERTY(double speed READ speed WRITE setSpeed NOTIFY speedChanged)
    Q_PROPERTY(QVariantList audioTracks READ audioTracks NOTIFY tracksChanged)
    Q_PROPERTY(QVariantList subtitleTracks READ subtitleTracks NOTIFY tracksChanged)
    Q_PROPERTY(int audioTrack READ audioTrack NOTIFY tracksChanged)
    Q_PROPERTY(int subtitleTrack READ subtitleTrack NOTIFY tracksChanged)
    Q_PROPERTY(QVariantList chapters READ chapters NOTIFY chaptersChanged)
    Q_PROPERTY(int currentChapter READ currentChapter NOTIFY positionChanged)
    Q_PROPERTY(double subtitleDelay READ subtitleDelay WRITE setSubtitleDelay NOTIFY subtitleDelayChanged)
    Q_PROPERTY(double subtitleScale READ subtitleScale WRITE setSubtitleScale NOTIFY subtitleScaleChanged)
    Q_PROPERTY(double audioDelay READ audioDelay WRITE setAudioDelay NOTIFY audioDelayChanged)
    Q_PROPERTY(double abLoopA READ abLoopA NOTIFY abLoopChanged)
    Q_PROPERTY(double abLoopB READ abLoopB NOTIFY abLoopChanged)
    Q_PROPERTY(QString aspectRatio READ aspectRatio NOTIFY aspectRatioChanged)
    Q_PROPERTY(QString crop READ crop NOTIFY cropChanged)
    Q_PROPERTY(int brightness READ brightness NOTIFY equalizerChanged)
    Q_PROPERTY(int contrast READ contrast NOTIFY equalizerChanged)
    Q_PROPERTY(int saturation READ saturation NOTIFY equalizerChanged)
    Q_PROPERTY(int gamma READ gamma NOTIFY equalizerChanged)
    Q_PROPERTY(int hue READ hue NOTIFY equalizerChanged)
    Q_PROPERTY(bool hardwareDecoding READ hardwareDecoding WRITE setHardwareDecoding NOTIFY hardwareDecodingChanged)
    Q_PROPERTY(QVariantMap stats READ stats NOTIFY statsChanged)
    Q_PROPERTY(cj::StreamPhase streamPhase READ streamPhase NOTIFY streamStatusChanged)
    Q_PROPERTY(int streamPercent READ streamPercent NOTIFY streamStatusChanged)
    Q_PROPERTY(int streamPeers READ streamPeers NOTIFY streamStatusChanged)
    Q_PROPERTY(double streamRate READ streamRate NOTIFY streamStatusChanged)

public:
    // The Session does not own the target; it must outlive the Session.
    explicit Session(PlaybackTarget* target, QObject* parent = nullptr);

    PlaybackTarget* target() const { return m_target; }

    PlaybackState state() const { return m_state; }
    bool hasMedia() const;
    bool loading() const { return m_state == PlaybackState::Loading; }
    bool paused() const { return m_state == PlaybackState::Paused; }
    QString targetName() const { return m_target->name(); }
    QUrl source() const { return m_source.url; }
    QString title() const { return m_title; }
    double position() const { return m_position; }
    double duration() const { return m_duration; }
    bool buffering() const { return m_buffering; }
    int bufferPercent() const { return m_bufferPercent; }
    double volume() const { return m_volume; }
    bool muted() const { return m_muted; }
    double speed() const { return m_speed; }
    QVariantList audioTracks() const { return tracksOf(Track::Type::Audio); }
    QVariantList subtitleTracks() const { return tracksOf(Track::Type::Subtitle); }
    int audioTrack() const { return selectedOf(Track::Type::Audio); }
    int subtitleTrack() const { return selectedOf(Track::Type::Subtitle); }
    QList<Track> tracks() const { return m_tracks; }
    QVariantList chapters() const;
    int currentChapter() const;
    double subtitleDelay() const { return m_subtitleDelay; }
    double subtitleScale() const { return m_subtitleScale; }
    double audioDelay() const { return m_audioDelay; }
    double abLoopA() const { return m_abA; }
    double abLoopB() const { return m_abB; }
    QString aspectRatio() const { return m_aspect; }
    QString crop() const { return m_crop; }
    int brightness() const { return m_eq.brightness; }
    int contrast() const { return m_eq.contrast; }
    int saturation() const { return m_eq.saturation; }
    int gamma() const { return m_eq.gamma; }
    int hue() const { return m_eq.hue; }
    bool hardwareDecoding() const { return m_hardwareDecoding; }
    StreamPhase streamPhase() const { return m_stream.phase; }
    int streamPercent() const { return m_stream.percent; }
    int streamPeers() const { return m_stream.peers; }
    double streamRate() const { return double(m_stream.downloadRate); }

    // Turns what the user typed, pasted or dropped (a magnet link, say) into
    // something playable, or returns nothing if it is not theirs. Tried in
    // order before the input is treated as a plain path or URL. Source
    // adapters register here.
    using SourceResolver = std::function<std::optional<PlayableSource>(const QString& input)>;
    void addSourceResolver(SourceResolver resolver) { m_resolvers.push_back(std::move(resolver)); }

    // Set by whoever delivers the current item's bytes (the torrent transport).
    // Cleared to None whenever a new item loads.
    void setStreamStatus(const StreamStatus& status);
    QVariantMap stats() const { return m_stats; }

    // How often a resume point is emitted while playing. Default 5 s.
    void setResumeInterval(int ms) { m_resumeTimer.setInterval(ms); }

    // Capability check by name, for QML, e.g. supports("FrameStep").
    Q_INVOKABLE bool supports(const QString& capability) const;

    // --- Commands -----------------------------------------------------------
    Q_INVOKABLE void open(const QUrl& url, double startSeconds = 0);
    Q_INVOKABLE void openPath(const QString& pathOrUrl, double startSeconds = 0);
    void load(const PlayableSource& source, double startSeconds = 0);
    Q_INVOKABLE void play();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void togglePause();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void seek(double seconds);
    Q_INVOKABLE void seekRelative(double seconds);
    Q_INVOKABLE void setAudioTrack(int id);
    Q_INVOKABLE void setSubtitleTrack(int id);
    Q_INVOKABLE void addExternalSubtitle(const QUrl& url);
    void setVolume(double volume);
    void setMuted(bool muted);
    void setSpeed(double factor);
    void setSubtitleDelay(double seconds);
    void setSubtitleScale(double factor);
    void setAudioDelay(double seconds);
    Q_INVOKABLE void nextChapter();
    Q_INVOKABLE void previousChapter();
    Q_INVOKABLE void frameStep(bool forward);
    Q_INVOKABLE void screenshot();
    // First call marks A at the current position, second marks B, third clears.
    Q_INVOKABLE void toggleAbLoop();
    Q_INVOKABLE void cycleAspectRatio();
    Q_INVOKABLE void cycleCrop();
    // name: brightness, contrast, saturation, gamma or hue; value -100…100.
    Q_INVOKABLE void setEqualizer(const QString& name, int value);
    Q_INVOKABLE void resetEqualizer();
    void setHardwareDecoding(bool enabled);

signals:
    void stateChanged();
    void sourceChanged();
    void titleChanged();
    void positionChanged();
    void durationChanged();
    void bufferingChanged();
    void volumeChanged();
    void speedChanged();
    void tracksChanged();
    void chaptersChanged();
    void subtitleDelayChanged();
    void subtitleScaleChanged();
    void audioDelayChanged();
    void abLoopChanged();
    void aspectRatioChanged();
    void cropChanged();
    void equalizerChanged();
    void hardwareDecodingChanged();
    void statsChanged();
    void streamStatusChanged();
    void ended();
    void errorOccurred(cj::PlaybackError error, const QString& message);
    void screenshotSaved(const QString& filePath);
    // Where to resume this item later: every 5 s while playing, on pause,
    // on stop, and before another item replaces it. Saved in Step 6.
    void resumePoint(const cj::PlayableSource& source, double position, double duration);

private:
    bool require(Capability c, const char* what) const;
    void emitResumePoint();
    void setState(PlaybackState s);
    void applyItemSettings();
    QVariantList tracksOf(Track::Type type) const;
    int selectedOf(Track::Type type) const;
    std::optional<PlayableSource> resolve(const QString& input) const;

    QPointer<PlaybackTarget> m_target;
    PlaybackState m_state = PlaybackState::Idle;
    PlayableSource m_source;
    QString m_title;
    double m_position = 0;
    double m_duration = 0;
    bool m_buffering = false;
    int m_bufferPercent = 0;
    double m_volume = 100;
    bool m_muted = false;
    double m_speed = 1.0;
    QList<Track> m_tracks;
    QList<Chapter> m_chapters;
    double m_subtitleDelay = 0;
    double m_subtitleScale = 1.0;
    double m_audioDelay = 0;
    double m_abA = -1;
    double m_abB = -1;
    QString m_aspect;
    QString m_crop;
    VideoEqualizer m_eq;
    bool m_hardwareDecoding = true;
    QVariantMap m_stats;
    StreamStatus m_stream;
    std::vector<SourceResolver> m_resolvers;
    // Where the last seek is going. Until the target gets there, relative
    // seeks (a held arrow key) add to this, not to the old position.
    double m_seekTarget = -1;
    QElapsedTimer m_seekClock;
    QTimer m_resumeTimer;
};

} // namespace cj
