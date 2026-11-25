import QtQuick
import QtQuick.Controls.Basic
import CorporalJackie.Player   // KeyedList

// Library: what to go on watching, then what the user saved.
Flickable {
    id: libraryPage
    required property Item nav   // the Pages: go(), back(), openItem() and the picked types
    function takeFocus() { if (libraryContinue.count > 0) libraryContinue.forceActiveFocus(); else libraryGrid.forceActiveFocus() }

    readonly property int columns: Math.max(2, Math.floor((width - 14 + Theme.posterGap) / (140 + Theme.posterGap)))
    readonly property real cell: Math.floor((width - 14) / columns)
    contentWidth: width
    contentHeight: libraryGrid.y + libraryGrid.height + 24
    clip: true
    boundsBehavior: Flickable.StopAtBounds
    ScrollBar.vertical: ScrollBar {}

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
    // One row, like the other shelves: the wheel and the arrow keys move it
    // sideways.
    ListView {
        id: libraryGrid
        objectName: "savedRow"
        anchors { left: parent.left; right: parent.right; top: libraryTop.bottom; topMargin: 12 }
        height: (cell - Theme.posterGap) * 1.5 + 68 + Theme.posterGlowGutter
        orientation: ListView.Horizontal
        spacing: Theme.posterGap
        clip: true
        activeFocusOnTab: true
        keyNavigationEnabled: true
        boundsBehavior: Flickable.StopAtBounds
        reuseItems: true
        leftMargin: Theme.posterGlowGutter
        rightMargin: Theme.posterGlowGutter
        model: KeyedList { values: library.items }
        ScrollBar.horizontal: ScrollBar { policy: libraryGrid.contentWidth > libraryGrid.width ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff }
        // A plain wheel (up and down) scrolls the row sideways.
        WheelHandler {
            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
            enabled: libraryGrid.contentWidth > libraryGrid.width
            onWheel: event => {
                const delta = event.angleDelta.x !== 0 ? event.angleDelta.x : event.angleDelta.y
                const most = Math.max(0, libraryGrid.contentWidth - libraryGrid.width + libraryGrid.leftMargin + libraryGrid.rightMargin)
                libraryGrid.contentX = Math.max(-libraryGrid.leftMargin, Math.min(most - libraryGrid.leftMargin, libraryGrid.contentX - delta))
                event.accepted = true
            }
        }
        delegate: PosterTile {
            required property var entry
            required property int index
            item: entry
            posterWidth: cell - Theme.posterGap
            current: ListView.isCurrentItem && libraryGrid.activeFocus
            onActivated: { libraryGrid.currentIndex = index; library.open(entry.key) }
        }
        function openCurrent() {
            const item = library.items[currentIndex]
            if (item) library.open(item.key)
        }
        Keys.onReturnPressed: openCurrent()
        Keys.onEnterPressed: openCurrent()
    }
}
