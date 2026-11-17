import QtQuick
import QtQuick.Controls.Basic

// A row of what the user started and did not finish, newest first: a click
// (or Enter) goes on from where they left off, ✕ (or Delete) takes it off
// the row. Binds to `library`.
ListView {
    id: row

    readonly property int tileWidth: 128
    signal resumed()

    function resumeAt(index) {
        const item = library.continueWatching[index]
        if (!item) return
        library.resume(item.key)
        row.resumed()
    }
    Keys.onReturnPressed: resumeAt(currentIndex)
    Keys.onEnterPressed: resumeAt(currentIndex)
    Keys.onDeletePressed: { const item = library.continueWatching[currentIndex]; if (item) library.forget(item.itemKey) }
    keyNavigationEnabled: true

    function remaining(item) {
        const left = Math.max(0, item.duration - item.position)
        return left >= 3600 ? Math.floor(left / 3600) + " h " + Math.round((left % 3600) / 60) + " min left"
                            : Math.max(1, Math.round(left / 60)) + " min left"
    }

    orientation: ListView.Horizontal
    implicitHeight: tileWidth * 1.5 + 44
    implicitWidth: count * (tileWidth + spacing) - spacing
    spacing: 12
    clip: true
    boundsBehavior: Flickable.StopAtBounds
    model: library.continueWatching
    ScrollBar.horizontal: ScrollBar { policy: row.contentWidth > row.width ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff }

    delegate: Item {
        id: tile
        required property var modelData
        required property int index
        width: row.tileWidth
        height: row.height

        Rectangle {
            id: art
            width: parent.width
            height: row.tileWidth * 1.5
            radius: Theme.radius
            color: Theme.card
            clip: true

            Text {
                textFormat: Text.PlainText
                anchors { fill: parent; margins: 10 }
                visible: poster.status !== Image.Ready
                text: tile.modelData.name
                color: Theme.text
                font.pixelSize: 14
                wrapMode: Text.Wrap
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
            }
            Image {
                id: poster
                anchors.fill: parent
                source: tile.modelData.poster
                fillMode: Image.PreserveAspectCrop
                sourceSize.width: row.tileWidth * 2
                asynchronous: true
            }
            // How far in
            Rectangle {
                anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                height: 4
                color: "#a0000000"
                Rectangle { width: parent.width * tile.modelData.progress; height: parent.height; color: Theme.accent }
            }
            Rectangle {
                anchors.centerIn: parent
                visible: hover.hovered
                width: 44; height: 44; radius: 22
                color: "#c0000000"
                Text { textFormat: Text.PlainText; anchors.centerIn: parent; anchors.horizontalCenterOffset: 2; text: "▶"; color: Theme.text; font.pixelSize: 18 }
            }
            HoverHandler { id: hover }
            TapHandler { onTapped: row.resumeAt(tile.index) }
            BarButton {
                anchors { top: parent.top; right: parent.right; margins: 4 }
                visible: hover.hovered
                text: "✕"
                tip: "Remove from Continue Watching"
                background: Rectangle { radius: 6; color: parent.hovered ? "#e0000000" : "#a0000000" }
                onClicked: library.forget(tile.modelData.itemKey)
            }
            // Hover and keyboard focus, drawn over the poster.
            Rectangle {
                anchors.fill: parent
                radius: parent.radius
                color: "transparent"
                readonly property bool current: tile.ListView.isCurrentItem && row.activeFocus
                border.width: current ? 3 : hover.hovered ? 2 : 0
                border.color: current ? Theme.accent : "#a0ffffff"
            }
        }
        Text {
            textFormat: Text.PlainText
            id: name
            anchors { top: art.bottom; topMargin: 6 }
            width: parent.width
            text: tile.modelData.name
            color: Theme.text
            font.pixelSize: 13
            elide: Text.ElideRight
        }
        Text {
            textFormat: Text.PlainText
            anchors { top: name.bottom; topMargin: 1 }
            width: parent.width
            text: tile.modelData.subtitle !== "" ? tile.modelData.subtitle : row.remaining(tile.modelData)
            color: Theme.muted
            font.pixelSize: 11
            elide: Text.ElideRight
        }
    }
}
