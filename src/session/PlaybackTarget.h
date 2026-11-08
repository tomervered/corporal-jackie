#pragma once
// The contract every playback target implements. Read docs/TARGET_CONTRACT.md
// first: it explains each command and event in words.

#include "PlaybackTypes.h"

#include <QObject>

namespace cj {

class PlaybackTarget : public QObject
{
    Q_OBJECT

public:
    using QObject::QObject;
    ~PlaybackTarget() override = default;

    // A short name for logs and the UI, e.g. "This computer".
    virtual QString name() const = 0;
    virtual Capabilities capabilities() const = 0;
    bool supports(Capability c) const { return capabilities().testFlag(c); }

    // --- Commands (never block; results come back as events) ---------------
    virtual void load(const PlayableSource& source, double startSeconds) = 0;
    virtual void play() = 0;
    virtual void pause() = 0;
    virtual void stop() = 0;
    virtual void seek(double seconds) = 0;
    virtual void setAudioTrack(int id) = 0;
    virtual void setSubtitleTrack(int id) = 0;   // -1 = none
    virtual void addExternalSubtitle(const QUrl& url) = 0;  // added and shown
    // Added as a track the user can pick. May be called while the item is
    // still loading; the target keeps it until it can add it. Added during
    // loading, the target's usual rules may show it (as players show a
    // subtitle file found next to a video); added later, it is never
    // switched on by itself.
    virtual void addSubtitleTrack(const SubtitleSource& subtitle) = 0;
    virtual void setSpeed(double factor) = 0;
    virtual void setVolume(double volume) = 0;    // 0–100
    virtual void setMuted(bool muted) = 0;

    // --- Optional commands; only called when the capability is listed ------
    virtual void setSubtitleDelay(double) {}
    virtual void setSubtitleScale(double) {}
    virtual void setAudioDelay(double) {}
    virtual void seekChapter(int) {}
    virtual void frameStep(bool /*forward*/) {}
    virtual void screenshot(const QString& /*filePath*/) {}
    virtual void setAbLoop(double /*a*/, double /*b*/) {}
    virtual void setAspectRatio(const QString&) {}
    virtual void setCrop(const QString&) {}
    virtual void setVideoEqualizer(const VideoEqualizer&) {}
    virtual void setHardwareDecoding(bool) {}
    // VideoOutput: a sharper (slower) upscaler; timing frames to the display's
    // refresh; blending frames so a 24 fps film moves evenly on a 60 Hz screen.
    virtual void setHighQualityScaling(bool) {}
    virtual void setDisplaySync(bool) {}
    virtual void setSmoothMotion(bool) {}

signals:
    void stateChanged(cj::PlaybackState state);
    void positionChanged(double seconds);
    void durationChanged(double seconds);
    void titleChanged(const QString& title);
    void tracksChanged(const QList<cj::Track>& tracks);
    void chaptersChanged(const QList<cj::Chapter>& chapters);
    void bufferingChanged(bool buffering, int percent);
    void volumeChanged(double volume, bool muted);
    void errorOccurred(cj::PlaybackError error, const QString& message);
    void statsChanged(const QMap<QString, QString>& stats);
    void screenshotSaved(const QString& filePath);
};

} // namespace cj
