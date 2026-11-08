import QtQuick
import QtQuick.Controls.Basic

// A titled, scrolling row of posters. Arrow keys move, Enter opens.
Column {
    id: posterRow
    property string title
    property var items: []
    property bool loading: false
    property string error: ""
    property bool canSeeAll: false
    signal opened(int index)
    signal seeAll()
    function takeFocus() { rowList.forceActiveFocus() }
    signal focusedIn()   // the keyboard moved into this row: scroll it into view
    width: parent ? parent.width : 400
    spacing: 8
    Row {
        spacing: 12
        Heading { text: posterRow.title; anchors.verticalCenter: parent.verticalCenter }
        ToolButton {
            visible: posterRow.canSeeAll && posterRow.items.length > 0
            anchors.verticalCenter: parent.verticalCenter
            contentItem: Text { text: "See all ›"; color: Theme.accent; font.pixelSize: 13 }
            onClicked: posterRow.seeAll()
        }
    }
    Muted {
        visible: text !== ""
        text: posterRow.loading ? "Loading…" : posterRow.error !== "" ? "Could not be loaded: " + posterRow.error
            : posterRow.items.length === 0 ? "Nothing here." : ""
    }
    ListView {
        id: rowList
        visible: posterRow.items.length > 0
        width: parent.width
        height: Theme.posterWidth * 1.5 + 44
        orientation: ListView.Horizontal
        spacing: 14
        clip: true
        activeFocusOnTab: true
        keyNavigationEnabled: true
        boundsBehavior: Flickable.StopAtBounds
        model: posterRow.items
        highlightFollowsCurrentItem: true
        highlightMoveDuration: 120
        ScrollBar.horizontal: ScrollBar { policy: rowList.contentWidth > rowList.width ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff }
        delegate: PosterTile {
            required property var modelData
            required property int index
            item: modelData
            current: ListView.isCurrentItem && rowList.activeFocus
            onActivated: { rowList.currentIndex = index; posterRow.opened(index) }
        }
        Keys.onReturnPressed: posterRow.opened(currentIndex)
        Keys.onEnterPressed: posterRow.opened(currentIndex)
        onActiveFocusChanged: if (activeFocus) posterRow.focusedIn()
    }
}
