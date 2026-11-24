import QtQuick
import QtQuick.Controls.Basic
import CorporalJackie.Player   // KeyedList

// What a search of all the add-ons found, a row per list.
Flickable {
    id: searchPage
    required property Item nav   // the Pages: go(), back(), openItem() and the picked types
    // The search box is the one at the top of every page.
    // WikiMaze's own search (src/sources/WikiMazeSearch.h), when it is installed
    // and new enough: its row comes first, with filters under the type chips.
    readonly property var wm: browser.wikimaze
    readonly property bool wmOn: wm.available && !wm.unsupported
    property string genreKey: ""
    property string yearOption: ""
    property string listId: ""
    readonly property string query: browser.searchAllText
    readonly property bool filtersOn: genreKey !== "" || yearOption !== "" || listId !== ""
    readonly property bool wmShown: wmOn && (query !== "" || filtersOn)
    function askWikimaze(live) { wm.search(query, nav.searchType, genreKey, yearOption, listId, live === true) }
    onQueryChanged: askWikimaze(false)
    Connections {
        target: nav
        function onSearchTypeChanged() { searchPage.listId = ""; searchPage.askWikimaze(false) }
    }
    Component.onCompleted: wm.loadFilters()
    // A genre chip for each genre of the type, with how many of the answer have it.
    function genreChips() {
        const counts = wm.genreCounts
        const haveCounts = Object.keys(counts).length > 0
        return wm.genres
            .filter(g => (nav.searchType === "" || g[nav.searchType]) && (!haveCounts || counts[g.key] > 0 || g.key === genreKey))
            .map(g => ({ key: g.key, label: g.label + (counts[g.key] ? "  " + counts[g.key] : "") }))
    }
    function yearChips() {
        const now = new Date().getFullYear()
        return [String(now), String(now - 1), "2020s", "2010s", "2000s", "1990s", "1980s", "Older"]
    }
    function listChips() { return wm.lists.filter(l => l.type === nav.searchType) }
    function takeFocus() { if (wmShown && wmRow.visible) wmRow.takeFocus(); else if (searchRows.count > 0 && searchRows.itemAt(0).visible) searchRows.itemAt(0).takeFocus(); else searchType.children[0].forceActiveFocus() }

    contentHeight: searchColumn.implicitHeight + 24
    clip: true
    ScrollBar.vertical: ScrollBar {}
    Column {
        id: searchColumn
        width: searchPage.width - 14
        spacing: 22
        Muted {
            width: parent.width
            visible: browser.searchAllText !== ""
            text: "Results for “" + browser.searchAllText + "” from every add-on that can search."
        }
        Flow {
            id: searchType
            width: parent.width
            spacing: 6
            Repeater {
                model: ["", "movie", "series"]
                Chip {
                    required property string modelData
                    text: nav.typeWord(modelData)
                    selected: nav.searchType === modelData
                    onClicked: { nav.searchType = modelData; browser.searchAll(browser.searchAllText, modelData) }
                }
            }
        }
        // Filters for WikiMaze's search; all off until one is touched.
        Column {
            width: parent.width
            spacing: 8
            visible: searchPage.wmOn
            Flow {
                width: parent.width
                spacing: 6
                Muted { text: "Genre"; height: 34; verticalAlignment: Text.AlignVCenter }
                Repeater {
                    model: searchPage.genreChips()
                    Chip {
                        required property var modelData
                        text: modelData.label
                        selected: searchPage.genreKey === modelData.key
                        onClicked: { searchPage.genreKey = selected ? "" : modelData.key; searchPage.askWikimaze(false) }
                    }
                }
            }
            Flow {
                width: parent.width
                spacing: 6
                Muted { text: "Year"; height: 34; verticalAlignment: Text.AlignVCenter }
                Repeater {
                    model: searchPage.yearChips()
                    Chip {
                        required property string modelData
                        text: modelData
                        selected: searchPage.yearOption === modelData
                        onClicked: { searchPage.yearOption = selected ? "" : modelData; searchPage.askWikimaze(false) }
                    }
                }
            }
            Flow {
                width: parent.width
                spacing: 6
                visible: nav.searchType !== "" && searchPage.listChips().length > 0
                Muted { text: "Search in"; height: 34; verticalAlignment: Text.AlignVCenter }
                Chip {
                    text: "Everything"
                    selected: searchPage.listId === ""
                    onClicked: { searchPage.listId = ""; searchPage.askWikimaze(false) }
                }
                Repeater {
                    model: searchPage.listChips()
                    Chip {
                        required property var modelData
                        text: modelData.name
                        selected: searchPage.listId === modelData.id
                        onClicked: { searchPage.listId = selected ? "" : modelData.id; searchPage.askWikimaze(false) }
                    }
                }
            }
        }
        PosterRow {
            id: wmRow
            objectName: "wikimazeRow"
            visible: searchPage.wmShown
            title: "WikiMaze" + (searchPage.wm.total > searchPage.wm.results.length ? "  ·  " + searchPage.wm.total + " found" : "")
            items: searchPage.wm.results
            loading: searchPage.wm.loading
            error: searchPage.wm.error
            near: y + height >= searchPage.contentY - searchPage.height / 2
                  && y <= searchPage.contentY + searchPage.height * 1.5
            onOpened: i => nav.openItem(() => browser.openWikimazeItem(i))
            onFocusedIn: nav.reveal(searchPage, wmRow)
        }
        AppButton {
            objectName: "wikimazeLiveButton"
            visible: searchPage.wmShown && !searchPage.wm.loading
            text: "Search Wikidata and TVmaze"
            onClicked: searchPage.askWikimaze(true)
        }
        Muted {
            width: parent.width
            visible: text !== ""
            text: browser.searchAllText === "" ? ""
                : browser.searchRows.length === 0 && searchPage.wmOn ? ""
                : browser.searchRows.length === 0 ? "None of your add-ons can search" + (nav.searchType ? " " + nav.typeWord(nav.searchType).toLowerCase() : "") + "."
                : browser.searchRows.every(r => !r.loading && r.items.length === 0)
                  && !(searchPage.wmOn && (searchPage.wm.loading || searchPage.wm.results.length > 0)) ? "Nothing found for “" + browser.searchAllText + "”."
                : ""
        }
        KeyedList { id: searchRowsModel; keyRole: "catalog"; values: browser.searchRows }
        Repeater {
            id: searchRows
            model: searchRowsModel
            PosterRow {
                id: searchRow
                required property var entry
                required property int index
                // Lists with nothing found are left out once they answer.
                visible: entry.loading || entry.items.length > 0
                title: entry.name
                items: entry.items
                loading: entry.loading
                error: entry.error
                near: y + height >= searchPage.contentY - searchPage.height / 2
                      && y <= searchPage.contentY + searchPage.height * 1.5
                onOpened: i => nav.openItem(() => browser.openRowItem(true, index, i))
                onFocusedIn: nav.reveal(searchPage, searchRow)
            }
        }
    }
}
