import QtQuick
import QtQuick.Controls.Basic

// The app's button: an outlined panel button, or with `primary` the one
// filled accent button of a screen (Resume, Install). `iconName` puts an
// Icon before the words.
Button {
    id: control
    property bool primary: false
    property string iconName: ""
    font.pixelSize: Theme.bodySize
    implicitHeight: Theme.controlHeight
    leftPadding: iconName !== "" && text !== "" ? 12 : 14
    rightPadding: 14
    Accessible.name: text
    contentItem: Row {
        spacing: 8
        Icon {
            visible: control.iconName !== ""
            anchors.verticalCenter: parent.verticalCenter
            name: control.iconName
            size: 16
            color: label.color
        }
        Text {
            id: label
            textFormat: Text.PlainText
            anchors.verticalCenter: parent.verticalCenter
            visible: text !== ""
            text: control.text
            font: control.font
            color: control.primary ? Theme.accentText : Theme.text
            opacity: control.enabled ? 1 : 0.45
            elide: Text.ElideRight
        }
    }
    background: Rectangle {
        implicitWidth: 40
        radius: Theme.radius
        color: control.primary ? (control.down ? Qt.darker(Theme.accent, 1.15) : control.hovered ? Qt.lighter(Theme.accent, 1.08) : Theme.accent)
             : control.down ? Theme.pressed : control.hovered || control.checked ? Theme.raised : Theme.panel
        border.width: control.visualFocus ? 2 : 1
        border.color: control.visualFocus ? Theme.accent : control.primary ? "transparent" : control.checked ? Theme.accent : Theme.line
        opacity: control.enabled ? 1 : 0.6
    }
}
