import QtQuick
import QtQuick.Controls.Basic

// A row of what the user started and did not finish, newest first: a click
// (or Enter) goes on from where they left off, ✕ (or Delete) takes it off
// the row. `skip` leaves out the first ones (Home shows the newest on its
// own, larger). Binds to `library`.
ListView {
    id: row

    property int skip: 0
    property real tileWidth: 150
    signal resumed()

    readonly property var entries: library.continueWatching.slice(skip)
    function resumeAt(index) {
        const item = entries[index]
        if (!item) return
        library.resume(item.key)
        row.resumed()
    }
    Keys.onReturnPressed: resumeAt(currentIndex)
    Keys.onEnterPressed: resumeAt(currentIndex)
    Keys.onDeletePressed: { const item = entries[currentIndex]; if (item) library.forget(item.itemKey) }
    keyNavigationEnabled: true

    function remaining(item) {
        const left = Math.max(0, item.duration - item.position)
        return left >= 3600 ? Math.floor(left / 3600) + " h " + Math.round((left % 3600) / 60) + " min left"
                            : Math.max(1, Math.round(left / 60)) + " min left"
    }

    orientation: ListView.Horizontal
    implicitHeight: tileWidth * 1.5 + 70
    implicitWidth: count * (tileWidth + spacing) - spacing
    spacing: Theme.posterGap
    clip: true
    topMargin: 4
    leftMargin: 4
    rightMargin: 3
    boundsBehavior: Flickable.StopAtBounds
    model: entries
    ScrollBar.horizontal: ScrollBar { policy: row.contentWidth > row.width ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff }

    delegate: Item {
        id: tile
        required property var modelData
        required property int index
        width: row.tileWidth
        height: poster.height
        HoverHandler { id: hover }
        PosterTile {
            id: poster
            posterWidth: row.tileWidth
            item: tile.modelData
            detail: tile.modelData.subtitle !== "" ? tile.modelData.subtitle : row.remaining(tile.modelData)
            progress: tile.modelData.progress
            current: tile.ListView.isCurrentItem && row.activeFocus
            onActivated: row.resumeAt(tile.index)
        }
        // Play, over the poster, while the pointer is on it.
        Rectangle {
            x: (row.tileWidth - width) / 2
            y: (row.tileWidth * 1.5 - height) / 2
            visible: hover.hovered
            width: 48; height: 48; radius: 24
            color: "#c0000000"
            Icon { anchors.centerIn: parent; anchors.horizontalCenterOffset: 2; name: "play"; size: 20; color: "white" }
        }
        IconButton {
            x: row.tileWidth - width - 4
            y: 4
            visible: hover.hovered
            width: 32; height: 32
            iconName: "close"
            iconSize: 15
            iconColor: "white"
            tip: "Remove from Continue Watching"
            background: Rectangle { radius: 16; color: parent.hovered ? "#e0000000" : "#a0000000" }
            onClicked: library.forget(tile.modelData.itemKey)
        }
    }
}
