import QtQuick
import QtQuick.Controls.Basic

// A row in a player panel (chapters, tracks, playback settings): words on
// the left, a value or a key on the right. The picked row has a filled
// background and a tick, so it never looks like the row under the pointer.
AbstractButton {
    id: row
    property string label
    property string detail         // a second, smaller line
    property string value          // on the right, muted
    property bool selected: false
    // Keeps the tick's room on every row of a list, so the values line up.
    property bool tickSpace: false
    property bool expandable: false
    property bool expanded: false
    property real fontSize: 14

    width: parent ? parent.width : 300
    implicitHeight: detail !== "" ? 48 : 38
    leftPadding: 10
    rightPadding: 10
    focusPolicy: Qt.NoFocus
    hoverEnabled: true
    Accessible.name: label + (value !== "" ? ", " + value : "") + (selected ? ", chosen" : "")
    Accessible.role: expandable ? Accessible.ComboBox : Accessible.MenuItem

    contentItem: Item {
        Column {
            anchors { left: parent.left; right: endRow.left; rightMargin: 10; verticalCenter: parent.verticalCenter }
            spacing: 1
            Text {
                textFormat: Text.PlainText
                width: parent.width
                text: row.label
                color: Theme.text
                font.pixelSize: row.fontSize
                elide: Text.ElideRight
                opacity: row.enabled ? 1 : 0.45
            }
            Text {
                textFormat: Text.PlainText
                visible: row.detail !== ""
                width: parent.width
                text: row.detail
                color: Theme.muted
                font.pixelSize: 11
                elide: Text.ElideRight
            }
        }
        Row {
            id: endRow
            anchors { right: parent.right; verticalCenter: parent.verticalCenter }
            spacing: 6
            Text {
                textFormat: Text.PlainText
                anchors.verticalCenter: parent.verticalCenter
                visible: text !== ""
                text: row.value
                color: Theme.muted
                font.pixelSize: 12
                opacity: row.enabled ? 1 : 0.45
            }
            Icon {
                anchors.verticalCenter: parent.verticalCenter
                visible: row.expandable
                name: "chevronDown"
                size: 13
                color: Theme.muted
                rotation: row.expanded ? 180 : 0
            }
            Icon {
                anchors.verticalCenter: parent.verticalCenter
                visible: row.selected || row.tickSpace
                opacity: row.selected ? 1 : 0
                name: "check"
                size: 15
                color: Theme.accent
            }
        }
    }
    background: Rectangle {
        radius: 7
        color: row.selected ? Theme.pressed : row.down ? Theme.pressed : row.hovered && row.enabled ? Theme.hover : "transparent"
    }
}
