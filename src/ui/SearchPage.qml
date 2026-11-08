import QtQuick
import QtQuick.Controls.Basic

// What a search of all the add-ons found, a row per list.
Flickable {
    id: searchPage
    required property Item nav   // the Pages: go(), back(), openItem() and the picked types
    function takeFocus() { searchField.forceActiveFocus() }

    contentHeight: searchColumn.implicitHeight + 24
    clip: true
    ScrollBar.vertical: ScrollBar {}
    Column {
        id: searchColumn
        width: searchPage.width - 14
        spacing: 18
        Row {
            width: parent.width
            spacing: 8
            TextField {
                id: searchField
                width: Math.min(460, parent.width)
                text: browser.searchAllText
                placeholderText: "Search all your add-ons"
                onAccepted: browser.searchAll(text, nav.searchType)
            }
        }
        Row {
            id: searchType
            spacing: 6
            Repeater {
                model: ["", "movie", "series"]
                Chip {
                    required property string modelData
                    text: nav.typeWord(modelData)
                    selected: nav.searchType === modelData
                    onClicked: { nav.searchType = modelData; browser.searchAll(searchField.text, modelData) }
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
        Repeater {
            model: browser.searchRows
            PosterRow {
                id: searchRow
                required property var modelData
                required property int index
                // Lists with nothing found are left out once they answer.
                visible: modelData.loading || modelData.items.length > 0
                title: modelData.name
                items: modelData.items
                loading: modelData.loading
                error: modelData.error
                onOpened: i => nav.openItem(() => browser.openRowItem(true, index, i))
                onFocusedIn: nav.reveal(searchPage, searchRow)
            }
        }
    }
}
