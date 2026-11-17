import QtQuick
import QtQuick.Controls.Basic

// A grid of posters that fills the width evenly: the columns share what is
// left over instead of leaving a gap on the right. Arrow keys move, Enter
// opens. `opened(index)` comes from a click and from Enter.
GridView {
    id: grid
    signal opened(int index)
    readonly property int columns: Math.max(1, Math.floor(width / (Theme.posterWidth + 18)))
    cellWidth: Math.floor(width / columns)
    cellHeight: Theme.posterWidth * 1.5 + 60
    clip: true
    activeFocusOnTab: true
    keyNavigationEnabled: true
    pixelAligned: true
    cacheBuffer: cellHeight * 2
    ScrollBar.vertical: ScrollBar {}
    // Tiles line up on the left with the page's headings; the space left
    // over goes between them.
    delegate: Item {
        id: cell
        required property var entry
        required property int index
        width: grid.cellWidth
        height: grid.cellHeight
        PosterTile {
            item: cell.entry
            current: cell.GridView.isCurrentItem && grid.activeFocus
            onActivated: { grid.currentIndex = cell.index; grid.opened(cell.index) }
        }
    }
    Keys.onReturnPressed: if (currentIndex >= 0) opened(currentIndex)
    Keys.onEnterPressed: if (currentIndex >= 0) opened(currentIndex)
}
