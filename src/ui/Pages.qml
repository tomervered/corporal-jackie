import QtQuick
import QtQuick.Controls.Basic

// The app's pages: Home, Discover, search results, an item's details, the
// ways to play it, Library, Add-ons and Your folders. They fill the window
// when nothing plays, and cover the video when opened with B. They bind to
// `browser` and `library`; picking a way to play hands it to the session.
//
// Keyboard: Tab moves between controls and rows, arrow keys move inside a
// row or grid, Enter opens, Escape goes back.
Rectangle {
    id: pages

    property string page: "home"   // home, discover, search, detail, streams, library, addons, folders
    property var history: []
    property string searchType: ""     // the type picked on the search page; "" = all
    property string discoverType: ""   // the type picked on Discover
    property string streamSort: "best" // how ways to play are ordered: best, seeders, largest, smallest
    property var hiddenProviders: []   // add-ons whose ways to play are left out
    property bool settingsOpen: false  // the Settings panel shows over the pages
    signal closeRequested()        // back to the video
    signal settingsRequested()
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
    Keys.onEscapePressed: pages.back()
    Keys.onBackPressed: pages.back()
    // Shortcuts that work on every page, unless a text field takes the key.
    Keys.onPressed: event => {
        switch (event.key) {
        case Qt.Key_P: pages.settingsRequested(); break
        case Qt.Key_O: pages.openFileRequested(); break
        case Qt.Key_U: pages.openLinkRequested(); break
        case Qt.Key_B: if (session.hasMedia) pages.closeRequested(); break
        case Qt.Key_Slash: pages.goTop("home"); homePage.focusSearch(); break
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
    function focusPage() {
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
        id: sideBarBackground
        anchors { left: parent.left; top: parent.top; bottom: parent.bottom }
        width: sideBar.width + 32
        color: Theme.sideBar
        Rectangle { anchors { right: parent.right; top: parent.top; bottom: parent.bottom } width: 1; color: Theme.line }
    }
    Column {
        id: sideBar
        anchors { left: parent.left; top: parent.top; margins: 16 }
        // Narrower in a small window, so the page keeps most of it.
        width: pages.width < 900 ? 148 : 180
        spacing: 2
        Text {
            textFormat: Text.PlainText
            text: "Corporal Jackie"
            color: Theme.accent
            font.pixelSize: 18
            font.weight: Font.Bold
            bottomPadding: 18
            leftPadding: 12
            topPadding: 4
        }
        NavButton { text: "Home"; selected: pages.page === "home" || pages.page === "search"; onClicked: pages.goTop("home") }
        NavButton {
            text: "Discover"
            selected: pages.page === "discover"
            onClicked: {
                if (browser.catalogIndex < 0 && browser.catalogs.length > 0) browser.openCatalog(0)
                pages.goTop("discover")
            }
        }
        NavButton { text: "Library"; selected: pages.page === "library"; onClicked: pages.goTop("library") }
        Item { width: 1; height: 12 }
        NavButton { text: "Add-ons"; selected: pages.page === "addons"; onClicked: pages.goTop("addons") }
        NavButton { text: "Your folders"; selected: pages.page === "folders"; onClicked: pages.goTop("folders") }
        NavButton { text: "Settings"; selected: pages.settingsOpen; onClicked: pages.settingsRequested() }
        Item { width: 1; height: 12 }
        NavButton {
            visible: session.hasMedia || session.loading
            text: "Back to the video"
            accent: true
            onClicked: pages.closeRequested()
        }
    }
    // Left out when the window is too short for it under the side bar.
    Text {
        textFormat: Text.PlainText
        anchors { left: sideBar.left; bottom: parent.bottom; leftMargin: 12; bottomMargin: 16 }
        visible: sideBar.y + sideBar.height + 32 < y
        text: "Version " + appVersion
        color: Theme.faint
        font.pixelSize: 11
    }

    Item {
        id: content
        anchors { left: sideBarBackground.right; right: parent.right; top: parent.top; bottom: parent.bottom; margins: 20; leftMargin: 28 }

        // Back, on every page reached from another one
        ToolButton {
            id: backButton
            visible: pages.history.length > 0
            anchors { left: parent.left; top: parent.top; leftMargin: -8 }
            focusPolicy: Qt.TabFocus
            contentItem: Text {
                textFormat: Text.PlainText
                text: "‹  Back"
                color: backButton.hovered || backButton.visualFocus ? Theme.text : Theme.muted
                font.pixelSize: 14
            }
            background: Rectangle {
                radius: 6
                color: backButton.hovered ? "#14ffffff" : "transparent"
                border.color: backButton.visualFocus ? Theme.accent : "transparent"
            }
            onClicked: pages.back()
        }
        // Where a page goes: below Back when it shows, else the whole area.
        Item {
            id: pageArea
            anchors { left: parent.left; right: parent.right; top: backButton.visible ? backButton.bottom : parent.top
                      bottom: parent.bottom; topMargin: backButton.visible ? 8 : 0 }
        }

        HomePage { id: homePage; nav: pages; anchors.fill: parent; visible: pages.page === "home" }
        SearchPage { id: searchPage; nav: pages; anchors.fill: pageArea; visible: pages.page === "search" }
        DiscoverPage { id: discoverPage; nav: pages; anchors.fill: pageArea; visible: pages.page === "discover" }
        DetailPage { id: detailPage; nav: pages; anchors.fill: pageArea; visible: pages.page === "detail" }
        StreamsPage { id: streamsPage; nav: pages; anchors.fill: pageArea; visible: pages.page === "streams" }
        LibraryPage { id: libraryPage; nav: pages; anchors.fill: parent; visible: pages.page === "library" }
        AddonsPage { id: addonsPage; nav: pages; anchors.fill: parent; visible: pages.page === "addons" }
        FoldersPage { id: foldersPage; nav: pages; anchors.fill: parent; visible: pages.page === "folders" }
    }
}
