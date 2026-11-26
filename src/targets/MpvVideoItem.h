#pragma once
#include "PlaybackSession.h"
#include <QPointer>
#include <QQuickFramebufferObject>

namespace cj {

class MpvPlayer;

// The video surface the UI places in its window (QML name: VideoSurface).
// It shows the video of the PlaybackSession's target when that target is this
// computer's mpv, and nothing otherwise (e.g. while casting). It renders through
// mpv's OpenGL render API into a framebuffer that Qt Quick composites like any
// other item, so QML controls can be drawn on top without flicker.
//
// Needs Qt Quick to run on OpenGL: call useOpenGL() before the first window.
class MpvVideoItem : public QQuickFramebufferObject
{
    Q_OBJECT
    Q_PROPERTY(cj::PlaybackSession* playbackSession READ session WRITE setSession NOTIFY sessionChanged)
    Q_PROPERTY(bool covered READ covered WRITE setCovered NOTIFY coveredChanged)

public:
    explicit MpvVideoItem(QQuickItem* parent = nullptr);

    PlaybackSession* session() const { return m_session; }
    void setSession(PlaybackSession* session);
    bool covered() const { return m_covered; }
    void setCovered(bool covered);
    MpvPlayer* target() const;

    Renderer* createRenderer() const override;

    // Selects OpenGL for Qt Quick (and a core profile on macOS). Call after
    // QGuiApplication exists and before any QML window is created.
    static void useOpenGL();

signals:
    void sessionChanged();
    void coveredChanged();

private:
    QPointer<PlaybackSession> m_session;
    QMetaObject::Connection m_loadRequested;
    QMetaObject::Connection m_stateChanged;
    bool m_covered = false;
};

} // namespace cj
