import QtQuick
import QtQuick.Controls.Basic

// The app's on/off switch: a small pill that fills with the accent when on.
Switch {
    id: control
    implicitWidth: 40
    implicitHeight: 24
    padding: 0
    indicator: Rectangle {
        implicitWidth: 38
        implicitHeight: 22
        x: control.leftPadding
        y: (control.height - height) / 2
        radius: height / 2
        color: control.checked ? Theme.accent : Theme.canvas
        border.width: control.visualFocus ? 2 : 1
        border.color: control.visualFocus ? Theme.text : control.checked ? Theme.accent : Theme.line
        opacity: control.enabled ? 1 : 0.5
        Rectangle {
            x: control.checked ? parent.width - width - 4 : 4
            anchors.verticalCenter: parent.verticalCenter
            width: 14; height: 14; radius: 7
            color: control.checked ? Theme.accentText : Theme.muted
            Behavior on x { NumberAnimation { duration: 120 } }
        }
    }
    contentItem: Item {}
}
