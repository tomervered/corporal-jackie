import QtQuick
import QtQuick.Controls.Basic

// A poster with its name under it. Shows the name on a plain tile when
// there is no poster.
Item {
    id: tile
    property var item: ({})
    property bool current: false
    signal activated()
    width: Theme.posterWidth
    height: Theme.posterWidth * 1.5 + 40
    Rectangle {
        id: art
        width: parent.width
        height: parent.width * 1.5
        radius: 6
        color: "#1fffffff"
        border.width: 2
        border.color: tile.current || tileHover.hovered ? Theme.accent : "transparent"
        clip: true
        Text {
            anchors { fill: parent; margins: 10 }
            visible: poster.status !== Image.Ready
            text: tile.item.name || ""
            color: Theme.text
            font.pixelSize: 14
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        Image {
            id: poster
            anchors { fill: parent; margins: 2 }
            source: tile.item.poster || ""
            fillMode: Image.PreserveAspectCrop
            sourceSize.width: 264
            asynchronous: true
        }
        HoverHandler { id: tileHover; cursorShape: Qt.PointingHandCursor }
        TapHandler { onTapped: tile.activated() }
    }
    Text {
        id: tileName
        anchors { top: art.bottom; topMargin: 5 }
        width: parent.width
        text: tile.item.name || ""
        color: Theme.text
        font.pixelSize: 13
        elide: Text.ElideRight
    }
    Text {
        anchors { top: tileName.bottom; topMargin: 1 }
        width: parent.width
        text: tile.item.releaseInfo || ""
        color: Theme.muted
        font.pixelSize: 11
        elide: Text.ElideRight
    }
}
