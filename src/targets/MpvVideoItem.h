#pragma once
#include <QPointer>
#include <QQuickFramebufferObject>

namespace cj {

class MpvPlayer;

// A Qt Quick item that shows the video of an MpvPlayer. It renders through
// mpv's OpenGL render API into a framebuffer that Qt Quick composites like any
// other item, so QML controls can be drawn on top without flicker.
//
// Needs Qt Quick to run on OpenGL: call useOpenGL() before the first window.
class MpvVideoItem : public QQuickFramebufferObject
{
    Q_OBJECT
    Q_PROPERTY(cj::MpvPlayer* player READ player WRITE setPlayer NOTIFY playerChanged)

public:
    explicit MpvVideoItem(QQuickItem* parent = nullptr);

    MpvPlayer* player() const { return m_player; }
    void setPlayer(MpvPlayer* player);

    Renderer* createRenderer() const override;

    // Selects OpenGL for Qt Quick (and a core profile on macOS). Call after
    // QGuiApplication exists and before any QML window is created.
    static void useOpenGL();

signals:
    void playerChanged();

private:
    QPointer<MpvPlayer> m_player;
};

} // namespace cj
