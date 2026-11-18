import QtQuick
import QtQuick.Controls.Basic
import CorporalJackie.Player   // KeyedList

// What a search of all the add-ons found, a row per list.
Flickable {
    id: searchPage
    required property Item nav   // the Pages: go(), back(), openItem() and the picked types
    // The search box is the one at the top of every page.
    function takeFocus() { if (searchRows.count > 0 && searchRows.itemAt(0).visible) searchRows.itemAt(0).takeFocus(); else searchType.children[0].forceActiveFocus() }

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
        Muted {
            width: parent.width
            visible: text !== ""
            text: browser.searchAllText === "" ? ""
                : browser.searchRows.length === 0 ? "None of your add-ons can search" + (nav.searchType ? " " + nav.typeWord(nav.searchType).toLowerCase() : "") + "."
                : browser.searchRows.every(r => !r.loading && r.items.length === 0) ? "Nothing found for “" + browser.searchAllText + "”."
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
                onOpened: i => nav.openItem(() => browser.openRowItem(true, index, i))
                onFocusedIn: nav.reveal(searchPage, searchRow)
            }
        }
    }
}
