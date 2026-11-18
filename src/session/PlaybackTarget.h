#pragma once
// The contract every playback target implements. Read docs/TARGET_CONTRACT.md
// first: it explains each command and event in words.

#include "PlaybackTypes.h"

#include <QObject>
#include <QVariant>

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
    virtual void setSubtitleStyle(const SubtitleStyle&) {}
    virtual void setAudioDelay(double) {}
    virtual void seekChapter(int) {}
    virtual void frameStep(bool /*forward*/) {}
    virtual void screenshot(const QString& /*filePath*/) {}
    virtual void setAbLoop(double /*a*/, double /*b*/) {}
    virtual void setAspectRatio(const QString&) {}
    virtual void setCrop(const QString&) {}
    virtual void setVideoEqualizer(const VideoEqualizer&) {}
    virtual void setHardwareDecoding(bool) {}
    // Upscale*: only called with an upscaler whose capability is listed, or
    // Off. Off must undo the others fully, with nothing left loaded.
    // targetHeight: the height to upscale to, in pixels; 0 = the size the
    // video is shown at.
    virtual void setUpscaler(Upscaler, int /*targetHeight*/) {}
    // NativeKeys: a key pressed or released over the video, as Qt names it.
    // The target runs its own binding for it. False if it has no name for
    // the key (a lone Shift, say).
    virtual bool keyEvent(int /*key*/, Qt::KeyboardModifiers, const QString& /*text*/, bool /*pressed*/) { return false; }
    // Full screen is the window's business; a target with its own key for it
    // (mpv's F) is told, so that key and the window agree.
    virtual void setFullScreen(bool) {}

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
    // The target changed one of its settings by itself (its own key bindings
    // did). Names and values: "speed", "subtitleDelay", "audioDelay",
    // "subtitleScale" (double); "abLoopA", "abLoopB" (seconds, -1 = unset);
    // "aspectRatio" (width/height, 0 = the file's own); "brightness",
    // "contrast", "saturation", "gamma", "hue" (int); "hardwareDecoding",
    // "fullScreen" (bool); "upscaler" (its Session name: "off" when the
    // target had to turn one off, a driver that cannot run it, say).
    void settingChanged(const QString& name, const QVariant& value);
    // Its own key bindings asked for something only the app can do:
    // "stop" (close the video) or "stats" (show or hide the stats).
    void appActionRequested(const QString& action);
};

} // namespace cj
