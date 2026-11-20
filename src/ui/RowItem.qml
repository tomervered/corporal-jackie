import QtQuick
import QtQuick.Controls.Basic

// One line in a list: a name, details under it, and how far it was watched.
ItemDelegate {
    id: rowItem
    property bool onPlayer: false   // over the video: the player\'s colours
    property string primary
    property string secondary
    property real progress: -1     // 0-1 shows a bar; 1 shows "Watched"
    width: ListView.view ? ListView.view.width : implicitWidth
    highlighted: ListView.isCurrentItem && ListView.view.activeFocus
    contentItem: Column {
        spacing: 2
        Text {
            textFormat: Text.PlainText
            width: parent.width
            text: rowItem.primary + (rowItem.progress >= 1 ? "   ✓ Watched" : "")
            color: rowItem.onPlayer ? Theme.pText : Theme.text
            font.pixelSize: 14
            elide: Text.ElideRight
        }
        Text {
            textFormat: Text.PlainText
            width: parent.width
            visible: text !== ""
            text: rowItem.secondary
            color: rowItem.onPlayer ? Theme.pMuted : Theme.muted
            font.pixelSize: 12
            wrapMode: Text.WordWrap
            maximumLineCount: 3
            elide: Text.ElideRight
        }
        Rectangle {
            visible: rowItem.progress > 0 && rowItem.progress < 1
            width: Math.min(200, parent.width); height: 3; radius: 1
            color: rowItem.onPlayer ? Theme.pDivider : Theme.divider
            Rectangle { width: parent.width * Math.max(0, rowItem.progress); height: parent.height; color: Theme.accent }
        }
    }
    background: Rectangle {
        radius: 6
        color: rowItem.down ? (rowItem.onPlayer ? Theme.pPressed : Theme.pressed) : rowItem.highlighted ? (rowItem.onPlayer ? Theme.pRaised : Theme.raised)
             : rowItem.hovered ? (rowItem.onPlayer ? Theme.pHover : Theme.hover) : "transparent"
        border.color: rowItem.highlighted ? Theme.accent : "transparent"
    }
}
