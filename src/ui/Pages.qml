import QtQuick
import QtQuick.Controls.Basic
import "Format.js" as Format

// The app's pages: Home, Discover, search results, an item's details, the
// ways to play it, Library, Add-ons, Local (your folders) and Settings, with
// the side bar and a search box at the top of each. They fill the window
// when nothing plays, and cover the video when opened with B. They bind to
// `browser` and `library`; picking a way to play hands it to the session.
//
// Keyboard: Tab moves between controls and rows, arrow keys move inside a
// row or grid, Enter opens, Escape goes back.
Rectangle {
    id: pages

    property string page: "home"   // home, discover, search, detail, streams, library, addons, folders
    // Settings shows over whichever page is open, and closes back to it.
    property bool settingsOpen: false
    readonly property string shownPage: settingsOpen ? "settings" : page
    readonly property var titles: ({
        home: "Home", discover: "Discover", search: "Search", library: "Library",
        addons: "Add-ons", folders: "Local", settings: "Settings", detail: ""
    })
    property var history: []
    property string searchType: ""     // the type picked on the search page; "" = all
    property string discoverType: ""   // the type picked on Discover
    property string streamSort: "best" // how ways to play are ordered: best, seeders, largest, smallest
    property var hiddenProviders: []   // add-ons whose ways to play are left out
    signal closeRequested()        // back to the video
    signal settingsRequested()     // opens or closes Settings
    signal openFileRequested()
    signal openLinkRequested()
    signal message(string text, bool isError)

    color: Theme.background

    function go(next) {
        if (next === page) return
        history = history.concat([page])
        page = next
    }
    // Top-level pages from the side bar start a fresh history.
    function goTop(next) {
        if (settingsOpen) settingsRequested()
        history = []
        page = next
        Qt.callLater(focusPage)
    }
    function openDiscover() {
        if (browser.catalogIndex < 0 && browser.catalogs.length > 0) browser.openCatalog(0)
        goTop("discover")
    }
    // A search of every add-on, from the box at the top of every page.
    function searchAll(text) {
        if (text.trim() === "") return
        browser.searchAll(text, searchType)
        if (settingsOpen) settingsRequested()
        go("search")
    }
    function focusSearch() { globalSearch.forceActiveFocus(); globalSearch.selectAll() }
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
        focusPage()
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
    // Escape closes Settings, else goes back. Settings' own controls let it
    // through (a closed drop-down, a switch), so it works from any of them.
    Keys.onEscapePressed: settingsOpen ? settingsRequested() : pages.back()
    Keys.onBackPressed: settingsOpen ? settingsRequested() : pages.back()
    // Shortcuts that work on every page, unless a text field takes the key.
    Keys.onPressed: event => {
        switch (event.key) {
        case Qt.Key_P: pages.settingsRequested(); break
        case Qt.Key_O: pages.openFileRequested(); break
        case Qt.Key_U: pages.openLinkRequested(); break
        case Qt.Key_B: if (session.hasMedia) pages.closeRequested(); break
        case Qt.Key_Slash: pages.focusSearch(); break
        // Enter presses the focused button, as Space does.
        case Qt.Key_Return: case Qt.Key_Enter: {
            // click() is Qt 6.8 and later; before that, send the signal.
            const focused = pages.Window.activeFocusItem
            if (focused && typeof focused.click === "function") focused.click()
            else if (focused && typeof focused.clicked === "function") focused.clicked()
            else return
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
    onSettingsOpenChanged: Qt.callLater(focusPage)
    function focusPage() {
        // The first-run screen keeps the keyboard until it is answered.
        if (!preferences.privacyChosen) return
        if (settingsOpen) { settingsPage.takeFocus(); return }
        const current = pageItems[page]
        if (current) current.takeFocus()
    }
    Component.onCompleted: if (visible) show()
    readonly property var pageItems: ({
        home: homePage, search: searchPage, discover: discoverPage, detail: detailPage,
        streams: streamsPage, library: libraryPage, addons: addonsPage, folders: foldersPage
    })

    // --- Side bar ----------------------------------------------------------------

    Rectangle {
        id: sideBar
        objectName: "sideBar"
        anchors { left: parent.left; top: parent.top; bottom: parent.bottom }
        width: pages.width < 760 ? 132 : Theme.sideBarWidth
        color: Theme.panel
        Rectangle { anchors { right: parent.right; top: parent.top; bottom: parent.bottom } width: 1; color: Theme.divider }

        Column {
            anchors { left: parent.left; right: parent.right; top: parent.top; margins: 12; topMargin: 22 }
            spacing: 4
            Row {
                spacing: 10
                leftPadding: 2
                bottomPadding: 26
                Image {
                    anchors.verticalCenter: parent.verticalCenter
                    source: "images/logo.png"
                    width: 36; height: 36
                    sourceSize: Qt.size(72, 72)
                    smooth: true
                    mipmap: true
                }
                Text {
                    textFormat: Text.PlainText
                    anchors.verticalCenter: parent.verticalCenter
                    width: sideBar.width - 36 - 10 - 26
                    text: "Corporal Jackie"
                    color: Theme.text
                    font.pixelSize: 16
                    font.weight: Font.DemiBold
                    wrapMode: Text.WordWrap
                    lineHeight: 0.95
                }
            }
            NavButton { text: "Home"; iconName: "home"; selected: pages.shownPage === "home"; onClicked: pages.goTop("home") }
            NavButton {
                text: "Discover"
                iconName: "discover"
                selected: pages.shownPage === "discover"
                onClicked: pages.openDiscover()
            }
            NavButton { text: "Library"; iconName: "library"; selected: pages.shownPage === "library"; onClicked: pages.goTop("library") }
            NavButton { text: "Add-ons"; iconName: "addons"; selected: pages.shownPage === "addons"; onClicked: pages.goTop("addons") }
            NavButton { text: "Local"; iconName: "local"; selected: pages.shownPage === "folders"; onClicked: pages.goTop("folders") }
        }

        Column {
            anchors { left: parent.left; right: parent.right; bottom: parent.bottom; margins: 12; bottomMargin: 16 }
            spacing: 4
            // What plays behind the pages, and the way back to it.
            Rectangle {
                objectName: "nowPlaying"
                visible: session.hasMedia || session.loading
                width: parent.width
                height: nowColumn.implicitHeight + 20
                radius: 8
                color: Theme.raised
                border.color: Theme.line
                Column {
                    id: nowColumn
                    anchors { left: parent.left; right: parent.right; top: parent.top; margins: 10 }
                    spacing: 8
                    Text {
                        textFormat: Text.PlainText
                        width: parent.width
                        text: session.loading && !session.hasMedia ? "Opening…" : session.paused ? "Paused" : "Playing"
                        color: Theme.muted
                        font.pixelSize: 12
                    }
                    Text {
                        textFormat: Text.PlainText
                        width: parent.width
                        text: session.title
                        visible: text !== ""
                        color: Theme.text
                        font.pixelSize: 14
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                        maximumLineCount: 2
                        wrapMode: Text.Wrap
                    }
                    // How far in, and how much is left.
                    Column {
                        width: parent.width
                        spacing: 4
                        visible: session.duration > 0
                        Rectangle {
                            objectName: "nowPlayingProgress"
                            width: parent.width
                            height: 4
                            radius: 2
                            color: Theme.line
                            Rectangle {
                                width: parent.width * Math.min(1, session.position / Math.max(1, session.duration))
                                height: parent.height
                                radius: 2
                                color: Theme.accent
                            }
                        }
                        Text {
                            textFormat: Text.PlainText
                            width: parent.width
                            text: Format.time(session.position) + " / " + Format.time(session.duration)
                            color: Theme.muted
                            font.pixelSize: 11
                            elide: Text.ElideRight
                        }
                    }
                    AppButton {
                        objectName: "backToVideo"
                        width: parent.width
                        font.pixelSize: Theme.smallSize + 1
                        primary: true
                        iconName: "play"
                        text: "Back to video"
                        onClicked: pages.closeRequested()
                    }
                }
            }
            NavButton {
                text: "Settings"
                iconName: "settings"
                selected: pages.settingsOpen
                onClicked: pages.settingsRequested()
            }
            Text {
                textFormat: Text.PlainText
                leftPadding: 14
                text: "Version " + appVersion
                color: Theme.faint
                font.pixelSize: 11
            }
        }
    }

    // --- The page ------------------------------------------------------------------

    Item {
        id: content
        anchors { left: sideBar.right; right: parent.right; top: parent.top; bottom: parent.bottom
                  leftMargin: pages.width < 760 ? 14 : 32; rightMargin: pages.width < 760 ? 14 : 32; topMargin: 22 }

        // The title, Back on a page reached from another one, and search,
        // which is the same on every page: it asks every add-on.
        Item {
            id: header
            objectName: "pageHeader"
            anchors { left: parent.left; right: parent.right; top: parent.top }
            height: 44
            Row {
                anchors { left: parent.left; verticalCenter: parent.verticalCenter }
                width: Math.max(0, headerTools.x - 16)
                spacing: 6
                IconButton {
                    id: backButton
                    objectName: "backButton"
                    visible: pages.history.length > 0 && !pages.settingsOpen
                    anchors.verticalCenter: parent.verticalCenter
                    iconName: "back"
                    tip: "Back (Esc)"
                    takesFocus: true
                    onClicked: pages.back()
                }
                PageTitle {
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width - (backButton.visible ? backButton.width + 6 : 0)
                    text: pages.titles[pages.shownPage] !== undefined ? pages.titles[pages.shownPage]
                        : pages.shownPage === "streams" ? browser.streamsTitle : ""
                }
            }
            Row {
                id: headerTools
                anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                spacing: 4
                AppField {
                    id: globalSearch
                    objectName: "globalSearch"
                    search: true
                    width: Math.min(290, content.width * 0.4)
                    anchors.verticalCenter: parent.verticalCenter
                    placeholderText: "Search titles"
                    Accessible.name: "Search all your add-ons"
                    onAccepted: pages.searchAll(text)
                    Keys.onEscapePressed: event => {
                        if (text !== "") { text = ""; event.accepted = true } else event.accepted = false
                    }
                }
                IconButton {
                    anchors.verticalCenter: parent.verticalCenter
                    iconName: "link"
                    tip: "Open a link or magnet (U)"
                    takesFocus: true
                    onClicked: pages.openLinkRequested()
                }
                IconButton {
                    anchors.verticalCenter: parent.verticalCenter
                    iconName: "file"
                    tip: "Open a video file (O)"
                    takesFocus: true
                    onClicked: pages.openFileRequested()
                }
            }
        }
        // Where a page goes.
        Item {
            id: pageArea
            anchors { left: parent.left; right: parent.right; top: header.bottom; bottom: parent.bottom; topMargin: 18 }
        }

        HomePage { id: homePage; nav: pages; anchors.fill: pageArea; visible: pages.shownPage === "home" }
        SearchPage { id: searchPage; nav: pages; anchors.fill: pageArea; visible: pages.shownPage === "search" }
        DiscoverPage { id: discoverPage; nav: pages; anchors.fill: pageArea; visible: pages.shownPage === "discover" }
        DetailPage { id: detailPage; nav: pages; anchors.fill: pageArea; visible: pages.shownPage === "detail" }
        StreamsPage { id: streamsPage; nav: pages; anchors.fill: pageArea; visible: pages.shownPage === "streams" }
        LibraryPage { id: libraryPage; nav: pages; anchors.fill: pageArea; visible: pages.shownPage === "library" }
        AddonsPage { id: addonsPage; nav: pages; anchors.fill: pageArea; visible: pages.shownPage === "addons" }
        FoldersPage { id: foldersPage; nav: pages; anchors.fill: pageArea; visible: pages.shownPage === "folders" }
        SettingsPanel {
            id: settingsPage
            objectName: "settingsPanel"
            anchors.fill: pageArea
            visible: pages.settingsOpen
            onCloseRequested: pages.settingsRequested()
        }
    }
}
