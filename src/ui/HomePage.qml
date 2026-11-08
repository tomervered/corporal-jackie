import QtQuick
import QtQuick.Controls.Basic

// Home: search, what to go on watching, and a row from each list of every
// add-on and folder.
Flickable {
    id: homePage
    required property Item nav   // the Pages: go(), back(), openItem() and the picked types
    // With nothing playing, typing searches. Over a video the keyboard starts
    // on the first row instead, so B (and the other letters) still work.
    function focusSearch() { homeSearch.forceActiveFocus() }
    function takeFocus() {
        if (!session.hasMedia) homeSearch.forceActiveFocus()
        else if (homeContinue.count > 0) homeContinue.forceActiveFocus()
        else if (homeRows.count > 0) homeRows.itemAt(0).takeFocus()
        else homeSearch.forceActiveFocus()
    }

    contentHeight: homeColumn.implicitHeight + 24
    clip: true
    ScrollBar.vertical: ScrollBar {}
    Column {
        id: homeColumn
        width: homePage.width - 14
        spacing: 18

        Row {
            width: parent.width
            spacing: 8
            TextField {
                id: homeSearch
                width: Math.min(460, parent.width - openButtons.width - 8)
                placeholderText: "Search all your add-ons"
                onAccepted: {
                    if (text.trim() === "") return
                    browser.searchAll(text, "")
                    nav.searchType = ""
                    nav.go("search")
                }
            }
            Row {
                id: openButtons
                spacing: 8
                Button { text: "Open a video…"; onClicked: nav.openFileRequested() }
                Button { text: "Open a link…"; onClicked: nav.openLinkRequested() }
            }
        }
        Muted {
            width: parent.width
            text: "Drop a video, a subtitle file or a magnet link anywhere on the window to open it."
        }

        Column {
            width: parent.width
            visible: homeContinue.count > 0
            spacing: 8
            Heading { text: "Continue watching" }
            ContinueWatching {
                id: homeContinue
                width: parent.width
                height: implicitHeight
                activeFocusOnTab: true
                onResumed: nav.closeRequested()
            }
        }

        Muted {
            width: parent.width
            visible: browser.catalogs.length === 0
            text: "Nothing to browse yet. Install an add-on under Add-ons, or add a folder with videos under Your folders."
        }
        Repeater {
            id: homeRows
            model: browser.homeRows
            PosterRow {
                id: homeRow
                required property var modelData
                required property int index
                title: modelData.name
                items: modelData.items
                loading: modelData.loading
                error: modelData.error
                canSeeAll: true
                onOpened: i => nav.openItem(() => browser.openRowItem(false, index, i))
                onSeeAll: { browser.openCatalog(modelData.catalog); nav.discoverType = ""; nav.go("discover") }
                onFocusedIn: nav.reveal(homePage, homeRow)
            }
        }
    }
}
