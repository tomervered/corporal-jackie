import QtQuick
import QtQuick.Controls.Basic
import CorporalJackie.Player   // KeyedList

// Discover: one list at a time, by type and genre.
Item {
    id: discoverPage
    required property Item nav   // the Pages: go(), back(), openItem() and the picked types
    function takeFocus() { grid.forceActiveFocus() }


    // The lists of the picked type, as [name, index in browser.catalogs].
    readonly property var typeCatalogs: {
        const out = []
        browser.catalogs.forEach((c, i) => { if (nav.discoverType === "" || c.type === nav.discoverType) out.push([c.name, i]) })
        return out
    }
    readonly property var types: {
        const seen = [""]
        browser.catalogs.forEach(c => { if (seen.indexOf(c.type) < 0) seen.push(c.type) })
        return seen
    }

    // The search box shows the search of the list that is open.
    Connections {
        target: browser
        function onItemsChanged() { if (!listSearch.activeFocus) listSearch.text = browser.search }
    }

    Column {
        id: discoverBar
        width: parent.width
        spacing: 10
        PageTitle { text: "Discover" }
        Flow {
            id: discoverType
            width: parent.width
            spacing: 6
            Repeater {
                model: discoverPage.types
                Chip {
                    required property string modelData
                    text: nav.typeWord(modelData)
                    selected: nav.discoverType === modelData
                    onClicked: {
                        nav.discoverType = modelData
                        const first = discoverPage.typeCatalogs[0]
                        const open = browser.catalogs[browser.catalogIndex]
                        if (first && modelData !== "" && (!open || open.type !== modelData))
                            browser.openCatalog(first[1])
                    }
                }
            }
        }
        Row {
            width: parent.width
            spacing: 8
            AppComboBox {
                id: catalogBox
                width: Math.min(320, parent.width * 0.4)
                model: discoverPage.typeCatalogs.map(c => c[0])
                currentIndex: discoverPage.typeCatalogs.findIndex(c => c[1] === browser.catalogIndex)
                displayText: currentIndex >= 0 ? currentText : "Pick a list"
                onActivated: index => browser.openCatalog(discoverPage.typeCatalogs[index][1], "", "")
            }
            AppComboBox {
                id: genreBox
                visible: browser.genres.length > 0
                width: Math.min(180, parent.width * 0.25)
                model: (browser.catalogIndex >= 0 && browser.catalogs[browser.catalogIndex].genreRequired ? [] : ["All genres"]).concat(browser.genres)
                currentIndex: Math.max(0, model.indexOf(browser.genre === "" ? "All genres" : browser.genre))
                onActivated: index => browser.openCatalog(browser.catalogIndex, "", model[index] === "All genres" ? "" : model[index])
            }
            AppTextField {
                id: listSearch
                width: parent.width - catalogBox.width - (genreBox.visible ? genreBox.width + 8 : 0) - 8
                enabled: browser.catalogIndex >= 0 && browser.catalogs[browser.catalogIndex].searchable
                placeholderText: enabled ? "Search this list" : "This list can't be searched"
                onAccepted: browser.openCatalog(browser.catalogIndex, text, browser.genre)
            }
        }
        // Why the list is empty, and what to do about it.
        Row {
            spacing: 12
            visible: emptyText.text !== ""
            Muted {
                id: emptyText
                anchors.verticalCenter: parent.verticalCenter
                width: Math.min(implicitWidth, discoverBar.width - (emptyAction.visible ? emptyAction.width + 12 : 0))
                color: browser.itemsError !== "" ? Theme.error : Theme.muted
                text: browser.itemsLoading ? ""
                    : browser.itemsError !== "" ? "This list could not be loaded: " + browser.itemsError
                    : browser.catalogs.length === 0 ? "Nothing to browse yet. Install an add-on, or add one of your folders."
                    : browser.catalogIndex < 0 ? "Pick a list above."
                    : browser.catalogs[browser.catalogIndex].searchOnly && browser.search === "" ? "This list only shows what you search for. Type in the search box."
                    : browser.items.length === 0 ? "Nothing in this list" + (browser.genre !== "" ? " for " + browser.genre : "") + "."
                    : ""
            }
            AppButton {
                id: emptyAction
                anchors.verticalCenter: parent.verticalCenter
                visible: browser.itemsError !== "" || browser.catalogs.length === 0
                text: browser.catalogs.length === 0 ? "Go to Add-ons" : "Try again"
                onClicked: browser.catalogs.length === 0 ? nav.goTop("addons")
                         : browser.openCatalog(browser.catalogIndex, browser.search, browser.genre)
            }
        }
    }
    // Updated in place: a new page adds posters without reloading the others.
    KeyedList { id: gridModel; values: browser.items }
    PosterGrid {
        id: grid
        anchors { left: parent.left; right: parent.right; top: discoverBar.bottom; bottom: parent.bottom; topMargin: 14 }
        model: gridModel
        onOpened: index => nav.openItem(() => browser.openItem(index))
        onAtYEndChanged: if (atYEnd && count > 0) browser.loadMore()
        footer: Muted {
            width: grid.width; height: browser.itemsLoading && browser.items.length > 0 ? 32 : 0
            horizontalAlignment: Text.AlignHCenter
            text: height > 0 ? "Loading more…" : ""
        }
    }
    PlaceholderTiles {
        anchors { left: parent.left; right: parent.right; top: discoverBar.bottom; topMargin: 14 }
        visible: browser.itemsLoading && browser.items.length === 0
    }
}
