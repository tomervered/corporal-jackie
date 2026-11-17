import QtQuick
import QtQuick.Controls.Basic

// A text box with rounded corners, as tall as the app's buttons.
TextField {
    id: field
    implicitHeight: 36
    leftPadding: 12
    rightPadding: 12
    font.pixelSize: 14
    background: Rectangle {
        radius: Theme.radius
        color: field.enabled ? "#1c1c23" : "#16161b"
        border.width: field.activeFocus ? 2 : 1
        border.color: field.activeFocus ? Theme.accent : field.hovered ? "#4c4c58" : "#33333c"
    }
}
