import QtQuick
import QtQuick.Controls.Basic

// The app's text field. `search` puts a magnifier at its start.
TextField {
    id: control
    property bool search: false
    font.pixelSize: Theme.bodySize
    implicitHeight: Theme.controlHeight + 2
    leftPadding: search ? 38 : 14
    rightPadding: 14
    color: Theme.text
    placeholderTextColor: Theme.muted
    selectionColor: Theme.accent
    selectedTextColor: Theme.accentText
    Accessible.name: placeholderText
    background: Rectangle {
        implicitWidth: 200
        radius: 7
        color: Theme.panel
        border.width: control.activeFocus ? 2 : 1
        border.color: control.activeFocus ? Theme.accent : Theme.line
        opacity: control.enabled ? 1 : 0.6
        Icon {
            visible: control.search
            anchors { left: parent.left; leftMargin: 12; verticalCenter: parent.verticalCenter }
            name: "search"
            size: 17
            color: Theme.muted
        }
    }
}
