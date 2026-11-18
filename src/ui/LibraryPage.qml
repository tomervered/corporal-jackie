import QtQuick
import QtQuick.Controls.Basic

// Library: what to go on watching, then what the user saved.
Item {
    required property Item nav   // the Pages: go(), back(), openItem() and the picked types
    function takeFocus() { if (libraryContinue.count > 0) libraryContinue.forceActiveFocus(); else libraryGrid.forceActiveFocus() }

    readonly property int columns: Math.max(2, Math.floor((width - 14 + Theme.posterGap) / (140 + Theme.posterGap)))
    readonly property real cell: Math.floor((width - 14) / columns)

    Column {
        id: libraryTop
        width: parent.width
        spacing: 12
        Heading { visible: libraryContinue.count > 0; text: "Continue watching" }
        ContinueWatching {
            id: libraryContinue
            visible: count > 0
            width: parent.width
            tileWidth: cell - Theme.posterGap
            height: implicitHeight
            activeFocusOnTab: true
            onResumed: nav.closeRequested()
        }
        Item { width: 1; height: libraryContinue.count > 0 ? 8 : 0 }
        Heading { text: "Saved" }
        Muted {
            width: parent.width
            visible: library.items.length === 0
            text: "Nothing saved yet. Open a film or show and press Add to library."
        }
    }
    GridView {
        id: libraryGrid
        anchors { left: parent.left; right: parent.right; top: libraryTop.bottom; bottom: parent.bottom; topMargin: 12 }
        cellWidth: cell
        cellHeight: (cell - Theme.posterGap) * 1.5 + 58 + 20
        clip: true
        activeFocusOnTab: true
        keyNavigationEnabled: true
        boundsBehavior: Flickable.StopAtBounds
        topMargin: 3
        leftMargin: 3
        model: library.items
        ScrollBar.vertical: ScrollBar {}
        delegate: PosterTile {
            required property var modelData
            required property int index
            item: modelData
            posterWidth: cell - Theme.posterGap
            current: GridView.isCurrentItem && libraryGrid.activeFocus
            onActivated: { libraryGrid.currentIndex = index; library.open(modelData.key) }
        }
        function openCurrent() {
            const item = library.items[currentIndex]
            if (item) library.open(item.key)
        }
        Keys.onReturnPressed: openCurrent()
        Keys.onEnterPressed: openCurrent()
    }
}
