import QtQuick
import QtQuick.Controls.Basic
import CorporalJackie.Player   // KeyedList

// A titled, scrolling row of posters, sized so a whole number of them fill
// the width. Arrow keys move, Enter opens. "See all" opens the whole list.
Column {
    id: posterRow
    property string title
    property var items: []
    property bool loading: false
    property string error: ""
    property bool canSeeAll: false
    // False while the row is scrolled far out of view: its tiles and their
    // posters are let go, and come back when it scrolls near again.
    property bool near: true
    signal opened(int index)
    signal seeAll()
    function takeFocus() { rowList.forceActiveFocus() }
    signal focusedIn()   // the keyboard moved into this row: scroll it into view
    width: parent ? parent.width : 400
    spacing: 12

    readonly property int columns: Math.max(2, Math.floor((width + Theme.posterGap) / (140 + Theme.posterGap)))
    readonly property real tileWidth: Math.floor((width - (columns - 1) * Theme.posterGap) / columns)

    Item {
        width: parent.width
        height: Math.max(rowTitle.implicitHeight, seeAllButton.implicitHeight)
        Heading {
            id: rowTitle
            anchors { left: parent.left; right: seeAllButton.left; rightMargin: 12; verticalCenter: parent.verticalCenter }
            text: posterRow.title
        }
        ToolButton {
            id: seeAllButton
            visible: posterRow.canSeeAll && posterRow.items.length > 0
            anchors { right: parent.right; verticalCenter: parent.verticalCenter }
            Accessible.name: "See all of " + posterRow.title
            contentItem: Row {
                spacing: 2
                Text { textFormat: Text.PlainText; anchors.verticalCenter: parent.verticalCenter; text: "See all"; color: Theme.link; font.pixelSize: Theme.smallSize + 1; font.weight: Font.DemiBold }
                Icon { anchors.verticalCenter: parent.verticalCenter; name: "chevronRight"; size: 14; color: Theme.link }
            }
            background: Rectangle { radius: Theme.radius; color: seeAllButton.hovered ? Theme.hover : "transparent" }
            onClicked: posterRow.seeAll()
        }
    }
    Muted {
        visible: text !== ""
        text: posterRow.loading ? "Loading…" : posterRow.error !== "" ? "Could not be loaded: " + posterRow.error
            : posterRow.items.length === 0 ? "Nothing here." : ""
    }
    // Updated in place: a row that loads keeps the posters it has.
    KeyedList { id: rowModel; values: posterRow.items }
    ListView {
        id: rowList
        visible: posterRow.items.length > 0
        width: parent.width
        height: posterRow.tileWidth * 1.5 + 68 + Theme.posterGlowGutter
        orientation: ListView.Horizontal
        spacing: Theme.posterGap
        clip: true
        activeFocusOnTab: true
        keyNavigationEnabled: true
        boundsBehavior: Flickable.StopAtBounds
        model: posterRow.near ? rowModel : null
        highlightFollowsCurrentItem: true
        highlightMoveDuration: 120
        reuseItems: true
        leftMargin: Theme.posterGlowGutter
        rightMargin: Theme.posterGlowGutter
        ScrollBar.horizontal: ScrollBar { policy: rowList.contentWidth > rowList.width ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff }
        delegate: PosterTile {
            required property var entry
            required property int index
            item: entry
            posterWidth: posterRow.tileWidth - 2
            current: ListView.isCurrentItem && rowList.activeFocus
            onActivated: { rowList.currentIndex = index; posterRow.opened(index) }
        }
        Keys.onReturnPressed: posterRow.opened(currentIndex)
        Keys.onEnterPressed: posterRow.opened(currentIndex)
        onActiveFocusChanged: if (activeFocus) posterRow.focusedIn()
    }
}
