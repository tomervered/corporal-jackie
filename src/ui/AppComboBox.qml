import QtQuick
import QtQuick.Controls.Basic

// A drop-down with rounded corners, as tall as the app's buttons. The list
// it opens keeps Basic style's look in the window's colours.
ComboBox {
    id: box
    implicitHeight: 36
    font.pixelSize: 13
    background: Rectangle {
        radius: Theme.radius
        color: box.down ? "#3c3c48" : box.hovered ? "#34343e" : "#2a2a33"
        border.width: box.visualFocus ? 2 : 0
        border.color: Theme.accent
    }
}
