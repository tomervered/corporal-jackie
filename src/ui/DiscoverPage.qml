import QtQuick
import QtQuick.Controls.Basic

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

    Column {
        id: discoverBar
        width: parent.width
        spacing: 10
        PageTitle { text: "Discover" }
        Row {
            id: discoverType
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
            ComboBox {
                id: catalogBox
                width: Math.min(320, parent.width * 0.4)
                model: discoverPage.typeCatalogs.map(c => c[0])
                currentIndex: discoverPage.typeCatalogs.findIndex(c => c[1] === browser.catalogIndex)
                displayText: currentIndex >= 0 ? currentText : "Pick a list"
                onActivated: index => browser.openCatalog(discoverPage.typeCatalogs[index][1], "", "")
            }
            ComboBox {
                id: genreBox
                visible: browser.genres.length > 0
                width: 180
                model: (browser.catalogIndex >= 0 && browser.catalogs[browser.catalogIndex].genreRequired ? [] : ["All genres"]).concat(browser.genres)
                currentIndex: Math.max(0, model.indexOf(browser.genre === "" ? "All genres" : browser.genre))
                onActivated: index => browser.openCatalog(browser.catalogIndex, "", model[index] === "All genres" ? "" : model[index])
            }
            TextField {
                id: listSearch
                width: parent.width - catalogBox.width - (genreBox.visible ? genreBox.width + 8 : 0) - 8
                enabled: browser.catalogIndex >= 0 && browser.catalogs[browser.catalogIndex].searchable
                placeholderText: enabled ? "Search this list" : "This list can't be searched"
                onAccepted: browser.openCatalog(browser.catalogIndex, text, browser.genre)
            }
        }
        Muted {
            width: parent.width
            visible: text !== ""
            text: browser.itemsLoading && browser.items.length === 0 ? "Loading…"
                : browser.itemsError !== "" ? "This list could not be loaded: " + browser.itemsError
                : browser.catalogs.length === 0 ? "Nothing to browse yet. Install an add-on, or add one of your folders."
                : browser.catalogIndex >= 0 && browser.catalogs[browser.catalogIndex].searchOnly && browser.search === "" ? "Type something to search."
                : !browser.itemsLoading && browser.items.length === 0 && browser.catalogIndex >= 0 ? "Nothing here."
                : ""
        }
    }
    GridView {
        id: grid
        anchors { left: parent.left; right: parent.right; top: discoverBar.bottom; bottom: parent.bottom; topMargin: 14 }
        cellWidth: Theme.posterWidth + 16
        cellHeight: Theme.posterWidth * 1.5 + 56
        clip: true
        activeFocusOnTab: true
        keyNavigationEnabled: true
        model: browser.items
        ScrollBar.vertical: ScrollBar {}
        delegate: PosterTile {
            required property var modelData
            required property int index
            item: modelData
            current: GridView.isCurrentItem && grid.activeFocus
            onActivated: { grid.currentIndex = index; nav.openItem(() => browser.openItem(index)) }
        }
        Keys.onReturnPressed: nav.openItem(() => browser.openItem(currentIndex))
        Keys.onEnterPressed: nav.openItem(() => browser.openItem(currentIndex))
        onAtYEndChanged: if (atYEnd && count > 0) browser.loadMore()
        footer: Muted { width: grid.width; height: browser.itemsLoading && browser.items.length > 0 ? 28 : 0; text: height > 0 ? "Loading more…" : "" }
    }
}
