import QtQuick
import QtQuick.Controls.Basic

// An entry in the side bar. `accent` marks the one that leads back to the
// video.
ItemDelegate {
    id: nav
    property bool selected: false
    property bool accent: false
    width: parent ? parent.width : 160
    height: 38
    leftPadding: 12
    font.pixelSize: 15
    contentItem: Text {
        textFormat: Text.PlainText
        text: nav.text
        color: nav.selected || nav.accent ? Theme.accent : nav.hovered ? Theme.text : "#c8c8d0"
        font.pixelSize: nav.font.pixelSize
        font.weight: nav.selected ? Font.DemiBold : Font.Normal
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
    background: Rectangle {
        radius: Theme.radius
        color: nav.selected ? Theme.accentSoft : nav.down ? "#30ffffff" : nav.hovered ? "#12ffffff" : "transparent"
        border.color: nav.visualFocus ? Theme.accent : nav.accent ? "#60e8b04b" : "transparent"
        // A mark at the left of the page that is open.
        Rectangle {
            visible: nav.selected
            anchors { left: parent.left; verticalCenter: parent.verticalCenter }
            width: 3; height: parent.height - 16; radius: 1.5
            color: Theme.accent
        }
    }
}
