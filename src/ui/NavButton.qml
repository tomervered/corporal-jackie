import QtQuick
import QtQuick.Controls.Basic

// An entry in the side bar: an icon and a name. The page that shows has a
// raised background and a short accent bar inside its left edge (straight,
// not following the rounded corner).
ItemDelegate {
    id: nav
    property string iconName
    property bool selected: false
    width: parent ? parent.width : 160
    height: 44
    leftPadding: 14
    font.pixelSize: Theme.bodySize
    Accessible.name: text
    contentItem: Row {
        spacing: 12
        Icon {
            anchors.verticalCenter: parent.verticalCenter
            name: nav.iconName
            size: 19
            color: nav.selected || nav.hovered ? Theme.text : Theme.muted
        }
        Text {
            textFormat: Text.PlainText
            anchors.verticalCenter: parent.verticalCenter
            width: nav.availableWidth - 31
            text: nav.text
            color: nav.selected || nav.hovered ? Theme.text : Theme.muted
            font: nav.font
            elide: Text.ElideRight
        }
    }
    background: Rectangle {
        radius: Theme.radius
        color: nav.selected ? Theme.raised : nav.down ? Theme.pressed : nav.hovered ? Theme.hover : "transparent"
        border.width: nav.visualFocus ? 2 : 0
        border.color: Theme.accent
        Rectangle {
            visible: nav.selected
            x: 5
            anchors.verticalCenter: parent.verticalCenter
            width: 3
            height: 18
            radius: 1.5
            color: Theme.accent
        }
    }
}
