import QtQuick
import QtQuick.Controls.Basic
import CorporalJackie.Player   // KeyedList

// Discover: one list at a time, the whole of it (more loads as it
// scrolls). Filters on one row: the type, the add-on, its list, the genre,
// and a filter of this list when the list can be searched. Searching every
// add-on is the box at the top of the page.
Item {
    id: discoverPage
    required property Item nav   // the Pages: go(), back(), openItem() and the picked types
    function takeFocus() { grid.forceActiveFocus() }

    // An add-on's lists are named "Add-on · List type".
    function addonOf(c) { const i = c.name.indexOf(" · "); return i >= 0 ? c.name.slice(0, i) : c.name }
    function listOf(c) { const i = c.name.indexOf(" · "); return i >= 0 ? c.name.slice(i + 3) : c.name }
    function sourceKey(c) { return c.adapter + "/" + c.provider }

    // "" = every add-on; else adapter/provider.
    property string addonKey: ""
    readonly property var open: browser.catalogIndex >= 0 ? browser.catalogs[browser.catalogIndex] : null
    onOpenChanged: if (open && addonKey !== "" && sourceKey(open) !== addonKey) addonKey = sourceKey(open)

    // The add-ons with a list of the picked type: [{key, name}].
    readonly property var addons: {
        const out = [], seen = {}
        browser.catalogs.forEach(c => {
            if (nav.discoverType !== "" && c.type !== nav.discoverType) return
            const k = sourceKey(c)
            if (seen[k]) return
            seen[k] = true
            out.push({ key: k, name: addonOf(c) })
        })
        return out
    }
    // The lists of the picked type and add-on, as [name, index in browser.catalogs].
    readonly property var typeCatalogs: {
        const out = []
        browser.catalogs.forEach((c, i) => {
            if (nav.discoverType !== "" && c.type !== nav.discoverType) return
            if (addonKey !== "" && sourceKey(c) !== addonKey) return
            out.push([addonKey !== "" ? listOf(c) : c.name, i])
        })
        return out
    }
    readonly property var types: {
        const seen = [""]
        browser.catalogs.forEach(c => { if (seen.indexOf(c.type) < 0) seen.push(c.type) })
        return seen
    }
    // A type or add-on was picked: open its first list unless the open one fits.
    function keepAListOpen() {
        const fits = open && (nav.discoverType === "" || open.type === nav.discoverType)
                     && (addonKey === "" || sourceKey(open) === addonKey)
        if (!fits && typeCatalogs.length > 0) browser.openCatalog(typeCatalogs[0][1])
    }

    Column {
        id: discoverBar
        width: parent.width
        spacing: 12
        Flow {
            width: parent.width
            spacing: 6
            Repeater {
                model: discoverPage.types
                Chip {
                    required property string modelData
                    text: nav.typeWord(modelData)
                    selected: nav.discoverType === modelData
                    onClicked: { nav.discoverType = modelData; discoverPage.keepAListOpen() }
                }
            }
        }
        Flow {
            width: parent.width
            spacing: 8
            AppCombo {
                id: addonBox
                objectName: "addonFilter"
                visible: discoverPage.addons.length > 1
                width: 190
                Accessible.name: "Add-on"
                model: ["All add-ons"].concat(discoverPage.addons.map(a => a.name))
                currentIndex: discoverPage.addonKey === "" ? 0 : 1 + discoverPage.addons.findIndex(a => a.key === discoverPage.addonKey)
                onActivated: index => {
                    discoverPage.addonKey = index === 0 ? "" : discoverPage.addons[index - 1].key
                    discoverPage.keepAListOpen()
                }
            }
            AppCombo {
                id: catalogBox
                objectName: "catalogPicker"
                width: Math.min(340, Math.max(220, discoverPage.width * 0.32))
                Accessible.name: "List"
                model: discoverPage.typeCatalogs.map(c => c[0])
                currentIndex: discoverPage.typeCatalogs.findIndex(c => c[1] === browser.catalogIndex)
                displayText: currentIndex >= 0 ? currentText : "Pick a list"
                onActivated: index => browser.openCatalog(discoverPage.typeCatalogs[index][1], "", "")
            }
            AppCombo {
                id: genreBox
                objectName: "genrePicker"
                visible: browser.genres.length > 0
                width: 180
                Accessible.name: "Genre"
                model: (discoverPage.open && discoverPage.open.genreRequired ? [] : ["All genres"]).concat(browser.genres)
                currentIndex: Math.max(0, model.indexOf(browser.genre === "" ? "All genres" : browser.genre))
                onActivated: index => browser.openCatalog(browser.catalogIndex, "", model[index] === "All genres" ? "" : model[index])
            }
            // Only for a list that can be searched: the others have no box
            // that does nothing. The search at the top asks every add-on.
            AppField {
                id: listSearch
                objectName: "listFilter"
                visible: discoverPage.open !== null && discoverPage.open.searchable
                width: 240
                search: true
                text: browser.search
                placeholderText: "Filter this list"
                onAccepted: browser.openCatalog(browser.catalogIndex, text, browser.genre)
            }
        }
        Muted {
            width: parent.width
            visible: text !== ""
            text: browser.itemsLoading && browser.items.length === 0 ? "Loading…"
                : browser.itemsError !== "" ? "This list could not be loaded: " + browser.itemsError
                : browser.catalogs.length === 0 ? "Nothing to browse yet. Install an add-on, or add one of your folders under Local."
                : discoverPage.open && discoverPage.open.searchOnly && browser.search === "" ? "This list only shows what you search for. Type in Filter this list."
                : !browser.itemsLoading && browser.items.length === 0 && browser.catalogIndex >= 0 ? "Nothing here."
                : ""
        }
    }
    // Updated in place: a new page adds posters without reloading the others.
    KeyedList { id: gridModel; values: browser.items }
    GridView {
        id: grid
        anchors { left: parent.left; right: parent.right; top: discoverBar.bottom; bottom: parent.bottom; topMargin: 18 }
        readonly property int columns: Math.max(2, Math.floor((width - 14 + Theme.posterGap) / (140 + Theme.posterGap)))
        cellWidth: Math.floor((width - 26) / columns)
        cellHeight: (cellWidth - Theme.posterGap) * 1.5 + 58 + 20
        clip: true
        activeFocusOnTab: true
        keyNavigationEnabled: true
        boundsBehavior: Flickable.StopAtBounds
        Component.onCompleted: if ("reuseItems" in grid) grid.reuseItems = true
        leftMargin: Theme.posterGlowGutter
        model: gridModel
        ScrollBar.vertical: ScrollBar {}
        delegate: PosterTile {
            required property var entry
            required property int index
            item: entry
            posterWidth: grid.cellWidth - Theme.posterGap
            current: GridView.isCurrentItem && grid.activeFocus
            onActivated: { grid.currentIndex = index; nav.openItem(() => browser.openItem(index)) }
        }
        Keys.onReturnPressed: nav.openItem(() => browser.openItem(currentIndex))
        Keys.onEnterPressed: nav.openItem(() => browser.openItem(currentIndex))
        onAtYEndChanged: if (atYEnd && count > 0) browser.loadMore()
        footer: Muted { width: grid.width; height: browser.itemsLoading && browser.items.length > 0 ? 28 : 0; text: height > 0 ? "Loading more…" : "" }
    }
}
