import QtQuick
import QtQuick.Controls.Basic

// A round icon-only button with a tooltip, and the tooltip's words as its
// accessible name. On the player bar it never takes keyboard focus (Space
// must pause, not press it again): set `takesFocus` elsewhere.
ToolButton {
    id: control
    property string iconName
    property string tip
    property real iconSize: 20
    property bool takesFocus: false
    property bool active: false        // its menu is open, or it is switched on
    property color iconColor: Theme.text
    focusPolicy: takesFocus ? Qt.StrongFocus : Qt.NoFocus
    implicitWidth: 40
    implicitHeight: 40
    padding: 0
    Accessible.name: tip
    // Its own tooltip, in the app's colours (the attached one takes the
    // system's, which can come out brown on Windows).
    ToolTip {
        id: tipBox
        visible: control.hovered && control.tip !== ""
        delay: 500
        text: control.tip
        contentItem: Text {
            textFormat: Text.PlainText
            text: tipBox.text
            color: Theme.text
            font.pixelSize: 12
        }
        background: Rectangle {
            radius: 5
            color: Theme.panel
            border.color: Theme.line
        }
    }
    contentItem: Item {
        Icon {
            anchors.centerIn: parent
            name: control.iconName
            size: control.iconSize
            color: control.active ? Theme.accent : control.iconColor
            opacity: control.enabled ? 1 : 0.4
            scale: control.down ? 0.92 : 1
            Behavior on scale { NumberAnimation { duration: 90 } }
        }
    }
    background: Rectangle {
        radius: width / 2
        color: control.down ? Theme.pressed : control.hovered || control.active ? Theme.hover : "transparent"
        border.width: control.visualFocus ? 2 : 0
        border.color: Theme.accent
    }
}
