import QtQuick
import QtQuick.Controls.Basic

// A button on the control bar. Never takes keyboard focus: Space must pause,
// not press the button again.
ToolButton {
    id: barButton
    focusPolicy: Qt.NoFocus
    palette.buttonText: Theme.text
    font.pixelSize: 13
    implicitWidth: Math.max(36, implicitContentWidth + 16)
    implicitHeight: 36
    background: Rectangle {
        radius: 6
        color: barButton.down ? "#40ffffff" : barButton.hovered ? "#26ffffff" : "transparent"
    }
}
