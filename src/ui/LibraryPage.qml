import QtQuick
import QtQuick.Controls.Basic

// Library: what to go on watching and what the user saved.
Item {
    required property Item nav   // the Pages: go(), back(), openItem() and the picked types
    function takeFocus() { libraryGrid.forceActiveFocus() }

    Column {
        id: libraryTop
        width: parent.width
        spacing: 12
        PageTitle { text: "Library" }
        Heading { visible: libraryContinue.count > 0; text: "Continue watching" }
        ContinueWatching {
            id: libraryContinue
            visible: count > 0
            width: parent.width
            height: implicitHeight
            activeFocusOnTab: true
            onResumed: nav.closeRequested()
        }
        Heading { text: "Saved" }
        Muted {
            width: parent.width
            visible: library.items.length === 0
            text: "Nothing saved yet. Open something and press Add to library."
        }
    }
    GridView {
        id: libraryGrid
        anchors { left: parent.left; right: parent.right; top: libraryTop.bottom; bottom: parent.bottom; topMargin: 10 }
        cellWidth: Theme.posterWidth + 16
        cellHeight: Theme.posterWidth * 1.5 + 56
        clip: true
        activeFocusOnTab: true
        keyNavigationEnabled: true
        model: library.items
        ScrollBar.vertical: ScrollBar {}
        delegate: PosterTile {
            required property var modelData
            required property int index
            item: modelData
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
