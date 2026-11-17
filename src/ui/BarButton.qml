import QtQuick
import QtQuick.Controls.Basic

// A button on the control bar or a panel header: a drawn icon (iconName,
// see Icon.qml) or a word. Never takes keyboard focus: Space must pause,
// not press the button again. `tip` shows on hover.
ToolButton {
    id: barButton
    property string iconName: ""
    property string tip: ""
    focusPolicy: Qt.NoFocus
    font.pixelSize: 13
    implicitWidth: Math.max(36, implicitContentWidth + 16)
    implicitHeight: 36
    ToolTip.visible: tip !== "" && hovered
    ToolTip.delay: 500
    ToolTip.text: tip
    contentItem: Item {
        implicitWidth: barButton.iconName !== "" ? 18 : label.implicitWidth
        implicitHeight: 18
        opacity: barButton.enabled ? 1 : 0.35
        Icon {
            anchors.centerIn: parent
            visible: barButton.iconName !== ""
            width: 18; height: 18
            name: barButton.iconName
        }
        Text {
            id: label
            textFormat: Text.PlainText
            anchors.centerIn: parent
            visible: barButton.iconName === ""
            text: barButton.text
            font: barButton.font
            color: Theme.text
        }
    }
    background: Rectangle {
        radius: 6
        color: barButton.down ? "#40ffffff" : barButton.hovered ? "#26ffffff" : "transparent"
    }
}
