#pragma once
// A playback target that plays nothing. It writes every command it receives
// as one line of text in `commands`, and lets a test send any event the
// contract allows. Session tests run against this, so they need no mpv.

#include "PlaybackTarget.h"

#include <QStringList>
#include <QVariant>

namespace cj {

class FakeTarget : public PlaybackTarget
{
    Q_OBJECT

public:
    explicit FakeTarget(Capabilities caps = allCapabilities(), QObject* parent = nullptr)
        : PlaybackTarget(parent), m_caps(caps) {}

    static Capabilities allCapabilities()
    {
        Capabilities c;
        for (Capability x : {Capability::SubtitleDelay, Capability::SubtitleStyle, Capability::AudioDelay,
                             Capability::Chapters, Capability::FrameStep, Capability::Screenshot,
                             Capability::AbLoop, Capability::AspectRatio, Capability::Crop,
                             Capability::VideoEqualizer, Capability::HardwareDecoding})
            c |= x;
        return c;
    }

    QString name() const override { return QStringLiteral("Fake"); }
    Capabilities capabilities() const override { return m_caps; }

    QStringList commands;
    QString last() const { return commands.isEmpty() ? QString() : commands.last(); }
    bool sent(const QString& prefix) const
    {
        for (const QString& c : commands)
            if (c.startsWith(prefix))
                return true;
        return false;
    }

    void load(const PlayableSource& s, double start) override { log("load", s.url.toString(), start); }
    void play() override { log("play"); }
    void pause() override { log("pause"); }
    void stop() override { log("stop"); }
    void seek(double s) override { log("seek", s); }
    void setAudioTrack(int id) override { log("audioTrack", id); }
    void setSubtitleTrack(int id) override { log("subtitleTrack", id); }
    void addExternalSubtitle(const QUrl& u) override { log("addSubtitle", u.toString()); }
    void addSubtitleTrack(const SubtitleSource& s) override { log("subtitleTrack+", s.url.toString(), s.language); }
    void setSpeed(double f) override { log("speed", f); }
    void setVolume(double v) override { log("volume", v); }
    void setMuted(bool m) override { log("muted", m); }
    void setSubtitleDelay(double s) override { log("subtitleDelay", s); }
    void setSubtitleScale(double f) override { log("subtitleScale", f); }
    void setSubtitleStyle(const SubtitleStyle& s) override
    {
        log("subtitleStyle", s.font, s.color, s.outline, s.background, s.restyleAll);
    }
    void setAudioDelay(double s) override { log("audioDelay", s); }
    void seekChapter(int i) override { log("chapter", i); }
    void frameStep(bool f) override { log("frameStep", f); }
    void screenshot(const QString& p) override { log("screenshot", p); }
    void setAbLoop(double a, double b) override { log("abLoop", a, b); }
    void setAspectRatio(const QString& r) override { log("aspect", r); }
    void setCrop(const QString& c) override { log("crop", c); }
    void setVideoEqualizer(const VideoEqualizer& e) override
    {
        log("equalizer", e.brightness, e.contrast, e.saturation, e.gamma, e.hue);
    }
    void setHardwareDecoding(bool on) override { log("hwdec", on); }

    // --- Events, as a real target would send them ---------------------------
    void startPlaying(double duration = 100)
    {
        emit durationChanged(duration);
        emit stateChanged(PlaybackState::Playing);
    }
    void movePosition(double s) { emit positionChanged(s); }
    void setState(PlaybackState s) { emit stateChanged(s); }

private:
    template<typename... Args>
    void log(const char* name, Args... args)
    {
        QStringList parts{QString::fromLatin1(name)};
        (parts.append(QVariant::fromValue(args).toString()), ...);
        commands.append(parts.join(QLatin1Char(' ')));
    }

    Capabilities m_caps;
};

} // namespace cj
