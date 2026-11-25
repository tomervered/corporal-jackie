import QtQuick
import QtQuick.Controls.Basic

Rectangle {
    id: group
    property string title
    property bool expanded: false
    default property alias content: groupBody.data
    function focusHeader() { groupHeader.forceActiveFocus() }
    width: parent ? parent.width : 400
    height: groupHeader.height + (expanded ? groupBody.implicitHeight + 14 : 0)
    radius: 7
    color: Theme.panel
    border.color: Theme.divider
    clip: true
    ItemDelegate {
        id: groupHeader
        width: parent.width
        height: 50
        leftPadding: 16
        rightPadding: 16
        Accessible.name: group.title
        Accessible.role: Accessible.Button
        contentItem: Item {
            Text {
                textFormat: Text.PlainText
                anchors { left: parent.left; verticalCenter: parent.verticalCenter }
                text: group.title
                color: Theme.text
                font.pixelSize: Theme.bodySize
                font.weight: Font.DemiBold
            }
            Icon {
                anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                name: group.expanded ? "chevronDown" : "chevronRight"
                size: 17
                color: Theme.muted
            }
        }
        background: Rectangle {
            radius: 7
            color: groupHeader.down ? Theme.pressed : groupHeader.hovered ? Theme.hover : "transparent"
            border.width: groupHeader.visualFocus ? 2 : 0
            border.color: Theme.accent
        }
        onClicked: group.expanded = !group.expanded
    }
    Column {
        id: groupBody
        visible: group.expanded
        anchors { left: parent.left; right: parent.right; top: groupHeader.bottom; leftMargin: 16; rightMargin: 16 }
    }
}
