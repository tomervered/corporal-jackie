#pragma once
#include "Session.h"
#include <QPointer>
#include <QQuickFramebufferObject>

namespace cj {

class MpvTarget;

// The video surface the UI places in its window (QML name: VideoSurface).
// It shows the video of the Session's target when that target is this
// computer's mpv, and nothing otherwise (e.g. while casting). It renders through
// mpv's OpenGL render API into a framebuffer that Qt Quick composites like any
// other item, so QML controls can be drawn on top without flicker.
//
// Needs Qt Quick to run on OpenGL: call useOpenGL() before the first window.
class MpvVideoItem : public QQuickFramebufferObject
{
    Q_OBJECT
    Q_PROPERTY(cj::Session* playbackSession READ session WRITE setSession NOTIFY sessionChanged)

public:
    explicit MpvVideoItem(QQuickItem* parent = nullptr);

    Session* session() const { return m_session; }
    void setSession(Session* session);
    MpvTarget* target() const;

    Renderer* createRenderer() const override;

    // Selects OpenGL for Qt Quick (and a core profile on macOS). Call after
    // QGuiApplication exists and before any QML window is created.
    static void useOpenGL();

signals:
    void sessionChanged();

private:
    void reportDisplayFps();

    QPointer<Session> m_session;
    QMetaObject::Connection m_screenConnection;
    QMetaObject::Connection m_rateConnection;
};

} // namespace cj
