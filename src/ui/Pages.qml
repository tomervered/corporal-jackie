import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs

// The app's pages: Home, Discover, search results, an item's details, the
// ways to play it, Library, Add-ons and Your folders. They fill the window
// when nothing plays, and cover the video when opened with B. They bind to
// `browser` and `library`; picking a way to play hands it to the session.
//
// Keyboard: Tab moves between controls and rows, arrow keys move inside a
// row or grid, Enter opens, Escape goes back.
Rectangle {
    id: pages

    property color textColor: "#f2f2f5"
    property color mutedText: "#9a9aa6"
    property color accent: "#e8b04b"
    property string page: "home"   // home, discover, search, detail, streams, library, addons, folders
    property var history: []
    signal closeRequested()        // back to the video
    signal settingsRequested()
    signal openFileRequested()
    signal openLinkRequested()
    signal message(string text, bool isError)

    color: "#101014"

    function go(next) {
        if (next === page) return
        history = history.concat([page])
        page = next
    }
    // Top-level pages from the side bar start a fresh history.
    function goTop(next) {
        history = []
        page = next
    }
    function back() {
        if (history.length > 0) {
            page = history[history.length - 1]
            history = history.slice(0, -1)
        } else if (page !== "home") {
            page = "home"
        } else if (session.hasMedia) {
            closeRequested()
        }
    }
    // Called when the pages appear.
    function show() {
        if (browser.homeRows.length === 0) browser.loadHome()
        homeSearch.forceActiveFocus()
    }
    function openItem(open) {   // open: a function that sets browser.detail
        open()
        go("detail")
    }
    // Scrolls a page so an item in it shows whole.
    function reveal(flick, item) {
        const top = item.mapToItem(flick.contentItem, 0, 0).y
        if (top < flick.contentY) flick.contentY = Math.max(0, top - 12)
        else if (top + item.height > flick.contentY + flick.height)
            flick.contentY = Math.min(flick.contentHeight - flick.height, top + item.height - flick.height + 12)
    }
    function typeWord(type) {
        return type === "movie" ? "Movies" : type === "series" ? "Series" : type === "tv" ? "TV"
             : type === "channel" ? "Channels" : type === "" ? "All" : type.charAt(0).toUpperCase() + type.slice(1)
    }

    Connections {
        target: browser
        function onNotice(text, isError) { pages.message(text, isError) }
        function onPlayStarted(title) { pages.closeRequested() }
        // Details arrive after the page opens: then it is known whether
        // there are episodes to move through.
        function onDetailChanged() { if (pages.page === "detail" && !browser.detailLoading) Qt.callLater(pages.focusPage) }
    }
    Connections {
        target: library
        function onOpenRequested() { pages.go("detail") }
    }

    // Clicks and scrolls stay here, away from the video below.
    MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons; onWheel: wheel => wheel.accepted = true }
    Keys.onEscapePressed: pages.back()
    Keys.onBackPressed: pages.back()
    // Shortcuts that work on every page, unless a text field takes the key.
    Keys.onPressed: event => {
        switch (event.key) {
        case Qt.Key_P: pages.settingsRequested(); break
        case Qt.Key_O: pages.openFileRequested(); break
        case Qt.Key_U: pages.openLinkRequested(); break
        case Qt.Key_B: if (session.hasMedia) pages.closeRequested(); break
        case Qt.Key_Slash: pages.goTop("home"); homeSearch.forceActiveFocus(); break
        // Enter presses the focused button, as Space does.
        case Qt.Key_Return: case Qt.Key_Enter: {
            const focused = pages.Window.activeFocusItem
            if (!focused || typeof focused.click !== "function") return
            focused.click()
            break
        }
        default: return
        }
        event.accepted = true
    }
    onVisibleChanged: if (visible) show()
    // Each page starts with the keyboard on its main list, so arrows and
    // Enter work at once.
    onPageChanged: Qt.callLater(focusPage)
    function focusPage() {
        switch (page) {
        case "home": homeSearch.forceActiveFocus(); break
        case "search": searchField.forceActiveFocus(); break
        case "discover": grid.forceActiveFocus(); break
        case "detail": (browser.detail.isSeries ? episodes : playButton).forceActiveFocus(); break
        case "streams": streamList.forceActiveFocus(); break
        case "library": libraryGrid.forceActiveFocus(); break
        case "addons": addonField.forceActiveFocus(); break
        case "folders": folderList.forceActiveFocus(); break
        }
    }
    Component.onCompleted: if (visible) show()

    // --- Pieces --------------------------------------------------------------------

    component Muted: Text {
        color: pages.mutedText
        font.pixelSize: 13
        wrapMode: Text.WordWrap
    }
    component Heading: Text {
        color: pages.textColor
        font.pixelSize: 18
        font.weight: Font.DemiBold
        elide: Text.ElideRight
    }
    component PageTitle: Text {
        color: pages.textColor
        font.pixelSize: 26
        font.weight: Font.DemiBold
        elide: Text.ElideRight
    }
    component RowItem: ItemDelegate {
        id: rowItem
        property string primary
        property string secondary
        property real progress: -1     // 0-1 shows a bar; 1 shows "Watched"
        width: ListView.view ? ListView.view.width : implicitWidth
        highlighted: ListView.isCurrentItem && ListView.view.activeFocus
        contentItem: Column {
            spacing: 2
            Text {
                width: parent.width
                text: rowItem.primary + (rowItem.progress >= 1 ? "   ✓ Watched" : "")
                color: pages.textColor
                font.pixelSize: 14
                elide: Text.ElideRight
            }
            Text {
                width: parent.width
                visible: text !== ""
                text: rowItem.secondary
                color: pages.mutedText
                font.pixelSize: 12
                wrapMode: Text.WordWrap
                maximumLineCount: 3
                elide: Text.ElideRight
            }
            Rectangle {
                visible: rowItem.progress > 0 && rowItem.progress < 1
                width: Math.min(200, parent.width); height: 3; radius: 1
                color: "#30ffffff"
                Rectangle { width: parent.width * Math.max(0, rowItem.progress); height: parent.height; color: pages.accent }
            }
        }
        background: Rectangle {
            radius: 6
            color: rowItem.down ? "#40ffffff" : rowItem.hovered || rowItem.highlighted ? "#1fffffff" : "transparent"
            border.color: rowItem.highlighted ? pages.accent : "transparent"
        }
    }
    // A poster with its name under it. Shows the name on a plain tile when
    // there is no poster.
    component PosterTile: Item {
        id: tile
        property var item: ({})
        property bool current: false
        signal activated()
        width: 132
        height: 132 * 1.5 + 40
        Rectangle {
            id: art
            width: parent.width
            height: parent.width * 1.5
            radius: 6
            color: "#1fffffff"
            border.width: 2
            border.color: tile.current || tileHover.hovered ? pages.accent : "transparent"
            clip: true
            Text {
                anchors { fill: parent; margins: 10 }
                visible: poster.status !== Image.Ready
                text: tile.item.name || ""
                color: pages.textColor
                font.pixelSize: 14
                wrapMode: Text.Wrap
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
            }
            Image {
                id: poster
                anchors { fill: parent; margins: 2 }
                source: tile.item.poster || ""
                fillMode: Image.PreserveAspectCrop
                sourceSize.width: 264
                asynchronous: true
            }
            HoverHandler { id: tileHover; cursorShape: Qt.PointingHandCursor }
            TapHandler { onTapped: tile.activated() }
        }
        Text {
            id: tileName
            anchors { top: art.bottom; topMargin: 5 }
            width: parent.width
            text: tile.item.name || ""
            color: pages.textColor
            font.pixelSize: 13
            elide: Text.ElideRight
        }
        Text {
            anchors { top: tileName.bottom; topMargin: 1 }
            width: parent.width
            text: tile.item.releaseInfo || ""
            color: pages.mutedText
            font.pixelSize: 11
            elide: Text.ElideRight
        }
    }
    // A titled, scrolling row of posters. Arrow keys move, Enter opens.
    component PosterRow: Column {
        id: posterRow
        property string title
        property var items: []
        property bool loading: false
        property string error: ""
        property bool canSeeAll: false
        signal opened(int index)
        signal seeAll()
        signal focusedIn()   // the keyboard moved into this row: scroll it into view
        width: parent ? parent.width : 400
        spacing: 8
        Row {
            spacing: 12
            Heading { text: posterRow.title; anchors.verticalCenter: parent.verticalCenter }
            ToolButton {
                visible: posterRow.canSeeAll && posterRow.items.length > 0
                anchors.verticalCenter: parent.verticalCenter
                contentItem: Text { text: "See all ›"; color: pages.accent; font.pixelSize: 13 }
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
            height: 132 * 1.5 + 44
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
    component NavButton: ItemDelegate {
        id: nav
        property string target
        property bool selected: false
        width: parent ? parent.width : 160
        font.pixelSize: 15
        contentItem: Text {
            text: nav.text
            color: nav.selected ? pages.accent : pages.textColor
            font: nav.font
            elide: Text.ElideRight
        }
        background: Rectangle {
            radius: 8
            color: nav.selected ? "#1fe8b04b" : nav.down ? "#40ffffff" : nav.hovered ? "#14ffffff" : "transparent"
            border.color: nav.activeFocus ? pages.accent : "transparent"
        }
    }
    component Chip: Button {
        id: chip
        property bool selected: false
        checkable: true
        checked: selected
        focusPolicy: Qt.StrongFocus
    }

    // --- Side bar ----------------------------------------------------------------

    Column {
        id: sideBar
        anchors { left: parent.left; top: parent.top; bottom: parent.bottom; margins: 16 }
        width: 180
        spacing: 4
        Text {
            text: "Corporal Jackie"
            color: pages.textColor
            font.pixelSize: 18
            font.weight: Font.DemiBold
            bottomPadding: 14
            leftPadding: 8
        }
        NavButton { text: "Home"; selected: pages.page === "home"; onClicked: pages.goTop("home") }
        NavButton {
            text: "Discover"
            selected: pages.page === "discover"
            onClicked: {
                if (browser.catalogIndex < 0 && browser.catalogs.length > 0) browser.openCatalog(0)
                pages.goTop("discover")
            }
        }
        NavButton { text: "Library"; selected: pages.page === "library"; onClicked: pages.goTop("library") }
        NavButton { text: "Add-ons"; selected: pages.page === "addons"; onClicked: pages.goTop("addons") }
        NavButton { text: "Your folders"; selected: pages.page === "folders"; onClicked: pages.goTop("folders") }
        NavButton { text: "Settings"; onClicked: pages.settingsRequested() }
        Item { width: 1; height: 16 }
        NavButton {
            visible: session.hasMedia || session.loading
            text: "▶  Back to the video"
            onClicked: pages.closeRequested()
        }
    }
    Text {
        anchors { left: sideBar.left; bottom: parent.bottom; margins: 0; leftMargin: 8; bottomMargin: 16 }
        text: "Build " + appVersion + " · ? for shortcuts"
        color: "#5c5c66"
        font.pixelSize: 11
    }

    Item {
        id: content
        anchors { left: sideBar.right; right: parent.right; top: parent.top; bottom: parent.bottom; margins: 16; leftMargin: 24 }

        // Back, on every page reached from another one
        Button {
            id: backButton
            visible: pages.history.length > 0
            anchors { left: parent.left; top: parent.top }
            text: "‹ Back"
            onClicked: pages.back()
        }

        // --- Home ------------------------------------------------------------------
        Flickable {
            id: homePage
            anchors.fill: parent
            visible: pages.page === "home"
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
                            searchType.type = ""
                            pages.go("search")
                        }
                    }
                    Row {
                        id: openButtons
                        spacing: 8
                        Button { text: "Open a video…"; onClicked: pages.openFileRequested() }
                        Button { text: "Open a link…"; onClicked: pages.openLinkRequested() }
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
                        textColor: pages.textColor
                        mutedText: pages.mutedText
                        accent: pages.accent
                        onResumed: pages.closeRequested()
                    }
                }

                Muted {
                    width: parent.width
                    visible: browser.catalogs.length === 0
                    text: "Nothing to browse yet. Install an add-on under Add-ons, or add a folder with videos under Your folders."
                }
                Repeater {
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
                        onOpened: i => pages.openItem(() => browser.openRowItem(false, index, i))
                        onSeeAll: { browser.openCatalog(modelData.catalog); discoverType.type = ""; pages.go("discover") }
                        onFocusedIn: pages.reveal(homePage, homeRow)
                    }
                }
            }
        }

        // --- Search results ------------------------------------------------------------
        Flickable {
            id: searchPage
            anchors { left: parent.left; right: parent.right; top: backButton.bottom; bottom: parent.bottom; topMargin: 12 }
            visible: pages.page === "search"
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
                        onAccepted: browser.searchAll(text, searchType.type)
                    }
                }
                Row {
                    id: searchType
                    property string type: ""
                    spacing: 6
                    Repeater {
                        model: ["", "movie", "series"]
                        Chip {
                            required property string modelData
                            text: pages.typeWord(modelData)
                            selected: searchType.type === modelData
                            onClicked: { searchType.type = modelData; browser.searchAll(searchField.text, modelData) }
                        }
                    }
                }
                Muted {
                    width: parent.width
                    visible: text !== ""
                    text: browser.searchRows.length === 0 ? "None of your add-ons can search" + (searchType.type ? " " + pages.typeWord(searchType.type).toLowerCase() : "") + "."
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
                        onOpened: i => pages.openItem(() => browser.openRowItem(true, index, i))
                        onFocusedIn: pages.reveal(searchPage, searchRow)
                    }
                }
            }
        }

        // --- Discover: one list, by type and genre ----------------------------------------
        Item {
            id: discoverPage
            anchors { left: parent.left; right: parent.right; top: pages.history.length > 0 ? backButton.bottom : parent.top; bottom: parent.bottom; topMargin: pages.history.length > 0 ? 12 : 0 }
            visible: pages.page === "discover"

            // The lists of the picked type, as [name, index in browser.catalogs].
            readonly property var typeCatalogs: {
                const out = []
                browser.catalogs.forEach((c, i) => { if (discoverType.type === "" || c.type === discoverType.type) out.push([c.name, i]) })
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
                    property string type: ""
                    spacing: 6
                    Repeater {
                        model: discoverPage.types
                        Chip {
                            required property string modelData
                            text: pages.typeWord(modelData)
                            selected: discoverType.type === modelData
                            onClicked: {
                                discoverType.type = modelData
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
                cellWidth: 132 + 16
                cellHeight: 132 * 1.5 + 56
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
                    onActivated: { grid.currentIndex = index; pages.openItem(() => browser.openItem(index)) }
                }
                Keys.onReturnPressed: pages.openItem(() => browser.openItem(currentIndex))
                Keys.onEnterPressed: pages.openItem(() => browser.openItem(currentIndex))
                onAtYEndChanged: if (atYEnd && count > 0) browser.loadMore()
                footer: Muted { width: grid.width; height: browser.itemsLoading && browser.items.length > 0 ? 28 : 0; text: height > 0 ? "Loading more…" : "" }
            }
        }

        // --- Details and episodes -----------------------------------------------------------
        Item {
            id: detailPage
            anchors { left: parent.left; right: parent.right; top: backButton.bottom; bottom: parent.bottom; topMargin: 12 }
            visible: pages.page === "detail"

            // The item's backdrop, faded into the page
            Image {
                id: backdrop
                anchors { left: parent.left; right: parent.right; top: parent.top }
                height: Math.min(parent.height * 0.5, width * 9 / 16)
                source: browser.detail.background || ""
                fillMode: Image.PreserveAspectCrop
                asynchronous: true
                opacity: 0.35
                sourceSize.width: 1280
            }
            Rectangle {
                anchors.fill: backdrop
                visible: backdrop.status === Image.Ready
                gradient: Gradient {
                    GradientStop { position: 0.0; color: "transparent" }
                    GradientStop { position: 1.0; color: pages.color }
                }
            }

            Row {
                id: detailHeader
                width: parent.width
                spacing: 18
                Image {
                    id: detailPoster
                    width: 150; height: 225
                    source: browser.detail.poster || ""
                    visible: status === Image.Ready
                    fillMode: Image.PreserveAspectCrop
                    sourceSize.width: 300
                    asynchronous: true
                }
                Column {
                    width: parent.width - (detailPoster.visible ? detailPoster.width + 18 : 0)
                    spacing: 8
                    PageTitle { width: parent.width; text: browser.detail.name || "" }
                    Muted {
                        width: parent.width
                        visible: text !== ""
                        text: [browser.detail.releaseInfo, browser.detail.runtime, browser.detail.genres].filter(s => s).join("  ·  ")
                    }
                    Muted {
                        width: parent.width
                        visible: text !== ""
                        text: browser.detail.description || ""
                        color: pages.textColor
                        maximumLineCount: 6
                        elide: Text.ElideRight
                    }
                    Row {
                        spacing: 8
                        Button {
                            id: playButton
                            visible: !browser.detail.isSeries && !browser.detailLoading
                            text: "▶  Find ways to play"
                            highlighted: true
                            onClicked: { browser.openVideo(-1); pages.go("streams") }
                        }
                        Button {
                            // Reads library.items so it updates when the library changes.
                            readonly property bool saved: library.items.length >= 0 && library.contains(browser.detail.key || "")
                            visible: (browser.detail.id || "") !== ""
                            text: saved ? "✓ In your library" : "+ Add to library"
                            onClicked: saved ? library.remove(browser.detail.key) : library.add(browser.detail)
                        }
                    }
                    Muted {
                        width: parent.width
                        visible: text !== ""
                        text: browser.detailLoading ? "Loading details…" : browser.detailError !== "" ? "Details could not be loaded: " + browser.detailError : ""
                    }
                }
            }
            ListView {
                id: episodes
                anchors { left: parent.left; right: parent.right; top: detailHeader.bottom; bottom: parent.bottom; topMargin: 16 }
                visible: browser.detail.isSeries === true
                clip: true
                activeFocusOnTab: true
                keyNavigationEnabled: true
                model: browser.videos
                ScrollBar.vertical: ScrollBar {}
                section.property: "season"
                section.delegate: Text {
                    required property string section
                    text: section
                    color: pages.accent
                    font.pixelSize: 15
                    font.weight: Font.DemiBold
                    topPadding: 12
                    bottomPadding: 4
                }
                delegate: RowItem {
                    required property var modelData
                    required property int index
                    primary: modelData.label
                    secondary: modelData.released
                    progress: library.historyCount >= 0 ? library.progress(browser.detail.adapter + ":" + browser.detail.id + ":" + modelData.id) : -1
                    onClicked: { episodes.currentIndex = index; browser.openVideo(index); pages.go("streams") }
                }
                Keys.onReturnPressed: { browser.openVideo(currentIndex); pages.go("streams") }
                Keys.onEnterPressed: { browser.openVideo(currentIndex); pages.go("streams") }
            }
        }

        // --- Ways to play, best first, grouped by quality ------------------------------------
        Column {
            anchors { left: parent.left; right: parent.right; top: backButton.bottom; bottom: parent.bottom; topMargin: 12 }
            visible: pages.page === "streams"
            spacing: 8
            PageTitle { width: parent.width; text: browser.streamsTitle }
            Muted {
                width: parent.width
                text: browser.streamsLoading ? "Asking your add-ons… (" + browser.streams.length + " found so far)"
                    : browser.streams.length === 0 ? "No way to play this was found. Add-ons that find streams (for example torrent add-ons) have to be installed first."
                    : browser.streams.length === 1 ? "1 way to play." : browser.streams.length + " ways to play. The best is first."
            }
            Muted {
                width: parent.width
                visible: browser.streamProblems.length > 0
                color: "#e09080"
                text: "Didn't answer: " + browser.streamProblems.join("; ")
            }
            ListView {
                id: streamList
                width: parent.width
                height: parent.height - y
                clip: true
                activeFocusOnTab: true
                keyNavigationEnabled: true
                model: browser.streams
                ScrollBar.vertical: ScrollBar {}
                section.property: "group"
                section.delegate: Text {
                    required property string section
                    text: section
                    color: pages.accent
                    font.pixelSize: 15
                    font.weight: Font.DemiBold
                    topPadding: 12
                    bottomPadding: 4
                }
                delegate: RowItem {
                    required property var modelData
                    required property int index
                    primary: modelData.provider + " · " + modelData.name
                    secondary: (modelData.description ? modelData.description.replace(/\n/g, "  ") + "\n" : "") + modelData.facts
                    onClicked: browser.play(index)
                }
                Keys.onReturnPressed: browser.play(currentIndex)
                Keys.onEnterPressed: browser.play(currentIndex)
            }
        }

        // --- Library ----------------------------------------------------------------------
        Item {
            anchors.fill: parent
            visible: pages.page === "library"
            Column {
                id: libraryTop
                width: parent.width
                spacing: 12
                PageTitle { text: "Library" }
                Heading { visible: libraryContinue.count > 0; text: "Continue watching" }
                ContinueWatching {
                    id: libraryContinue
                    visible: count > 0
                    width: parent.width
                    height: implicitHeight
                    activeFocusOnTab: true
                    textColor: pages.textColor
                    mutedText: pages.mutedText
                    accent: pages.accent
                    onResumed: pages.closeRequested()
                }
                Heading { text: "Saved" }
                Muted {
                    width: parent.width
                    visible: library.items.length === 0
                    text: "Nothing saved yet. Open something and press Add to library."
                }
            }
            GridView {
                id: libraryGrid
                anchors { left: parent.left; right: parent.right; top: libraryTop.bottom; bottom: parent.bottom; topMargin: 10 }
                cellWidth: 132 + 16
                cellHeight: 132 * 1.5 + 56
                clip: true
                activeFocusOnTab: true
                keyNavigationEnabled: true
                model: library.items
                ScrollBar.vertical: ScrollBar {}
                delegate: PosterTile {
                    required property var modelData
                    required property int index
                    item: modelData
                    current: GridView.isCurrentItem && libraryGrid.activeFocus
                    onActivated: { libraryGrid.currentIndex = index; library.open(modelData.key) }
                }
                Keys.onReturnPressed: library.open(library.items[currentIndex].key)
                Keys.onEnterPressed: library.open(library.items[currentIndex].key)
            }
        }

        // --- Add-ons --------------------------------------------------------------------------
        Column {
            anchors.fill: parent
            visible: pages.page === "addons"
            spacing: 10
            PageTitle { text: "Add-ons" }
            Muted {
                width: parent.width
                text: "Add-ons give the lists, details and streams. Paste an add-on's install link (it ends in manifest.json). Cinemeta comes installed: it has lists and details, not streams. Add-ons higher up are asked first, and their lists come first on Home."
            }
            Row {
                width: parent.width
                spacing: 8
                TextField {
                    id: addonField
                    width: parent.width - installButton.width - 8
                    placeholderText: "https://…/manifest.json"
                    onAccepted: installButton.clicked()
                }
                Button {
                    id: installButton
                    text: browser.installing ? "Installing…" : "Install"
                    enabled: !browser.installing && addonField.text.trim() !== ""
                    onClicked: { browser.installAddon(addonField.text); addonField.text = "" }
                }
            }
            ListView {
                width: parent.width
                height: parent.height - y
                clip: true
                model: browser.addons
                spacing: 4
                ScrollBar.vertical: ScrollBar {}
                delegate: Rectangle {
                    required property var modelData
                    required property int index
                    width: ListView.view.width
                    height: addonText.implicitHeight + 20
                    radius: 8
                    color: "#14ffffff"
                    Column {
                        id: addonText
                        anchors { left: parent.left; right: addonButtons.left; verticalCenter: parent.verticalCenter; margins: 10 }
                        spacing: 2
                        Text { text: modelData.name + "  " + modelData.version; color: pages.textColor; font.pixelSize: 14; font.weight: Font.DemiBold }
                        Muted { width: parent.width; text: modelData.description; visible: text !== ""; maximumLineCount: 2; elide: Text.ElideRight }
                        Muted { width: parent.width; text: "Offers: " + modelData.offers }
                        Muted { width: parent.width; visible: modelData.error !== ""; color: "#e09080"; text: "Last request failed: " + modelData.error }
                    }
                    Row {
                        id: addonButtons
                        anchors { right: parent.right; verticalCenter: parent.verticalCenter; margins: 10 }
                        spacing: 4
                        Button { text: "↑"; enabled: index > 0; onClicked: browser.moveAddon(modelData.id, -1); ToolTip.visible: hovered; ToolTip.text: "Move up" }
                        Button { text: "↓"; enabled: index < browser.addons.length - 1; onClicked: browser.moveAddon(modelData.id, 1); ToolTip.visible: hovered; ToolTip.text: "Move down" }
                        Button { text: "Remove"; onClicked: browser.removeAddon(modelData.id) }
                    }
                }
            }
        }

        // --- Your folders ------------------------------------------------------------------
        Column {
            anchors.fill: parent
            visible: pages.page === "folders"
            spacing: 10
            PageTitle { text: "Your folders" }
            Muted {
                width: parent.width
                text: "Videos in these folders show up as lists on Home and in Discover. Subtitle files next to a video, or in a Subs folder beside it, load with it."
            }
            Row {
                spacing: 8
                Button { text: "Add a folder…"; onClicked: folderDialog.open() }
                Button { text: "Scan again"; enabled: browser.folders.length > 0 && !browser.scanning; onClicked: browser.rescanFolders() }
                Muted {
                    anchors.verticalCenter: parent.verticalCenter
                    text: browser.scanning ? "Scanning…" : browser.localVideoCount + " videos found"
                }
            }
            ListView {
                id: folderList
                width: parent.width
                height: parent.height - y
                clip: true
                activeFocusOnTab: true
                keyNavigationEnabled: true
                model: browser.folders
                delegate: RowItem {
                    required property var modelData
                    primary: modelData
                    secondary: "Click to remove"
                    onClicked: browser.removeFolder(modelData)
                }
                Keys.onDeletePressed: if (currentIndex >= 0) browser.removeFolder(browser.folders[currentIndex])
            }
        }
    }

    FolderDialog {
        id: folderDialog
        title: "Add a folder with videos"
        onAccepted: browser.addFolder(selectedFolder)
    }
}
