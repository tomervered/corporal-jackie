import QtQuick
import QtQuick.Controls.Basic

// An entry in the side bar.
ItemDelegate {
    id: nav
    property string target
    property bool selected: false
    width: parent ? parent.width : 160
    font.pixelSize: 15
    contentItem: Text {
        text: nav.text
        color: nav.selected ? Theme.accent : Theme.text
        font: nav.font
        elide: Text.ElideRight
    }
    background: Rectangle {
        radius: 8
        color: nav.selected ? "#1fe8b04b" : nav.down ? "#40ffffff" : nav.hovered ? "#14ffffff" : "transparent"
        border.color: nav.activeFocus ? Theme.accent : "transparent"
    }
}
