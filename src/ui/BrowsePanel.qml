import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs

// The browse panel (press B): catalogs from add-ons and your folders, an
// item's details and episodes, and the ways to play it, best first. It binds
// only to `browser`; picking a stream hands it to the session.
Rectangle {
    id: panel

    property color textColor: "#f2f2f5"
    property color mutedText: "#9a9aa6"
    property color accent: "#e8b04b"
    property string tab: "discover"     // discover, addons, folders
    property string page: "items"       // items, detail, streams
    signal closeRequested()
    signal message(string text, bool isError)

    color: "#141418"
    border.color: "#30ffffff"
    radius: 12

    function show() {
        visible = true
        if (browser.catalogIndex < 0 && browser.catalogs.length > 0) browser.openCatalog(0, "")
        searchField.forceActiveFocus()
    }

    Connections {
        target: browser
        function onNotice(text, isError) { panel.message(text, isError) }
        function onPlayStarted(title) { panel.closeRequested() }
    }

    component Muted: Text {
        color: panel.mutedText
        font.pixelSize: 13
        wrapMode: Text.WordWrap
    }
    component Heading: Text {
        color: panel.textColor
        font.pixelSize: 18
        font.weight: Font.DemiBold
        elide: Text.ElideRight
    }
    component RowItem: ItemDelegate {
        id: rowItem
        property string primary
        property string secondary
        width: ListView.view ? ListView.view.width : implicitWidth
        contentItem: Column {
            spacing: 2
            Text { width: parent.width; text: rowItem.primary; color: panel.textColor; font.pixelSize: 14; elide: Text.ElideRight }
            Text {
                width: parent.width
                visible: text !== ""
                text: rowItem.secondary
                color: panel.mutedText
                font.pixelSize: 12
                wrapMode: Text.WordWrap
                maximumLineCount: 3
                elide: Text.ElideRight
            }
        }
        background: Rectangle { radius: 6; color: rowItem.down ? "#40ffffff" : rowItem.hovered ? "#1fffffff" : "transparent" }
    }

    // Clicks and scrolls on the panel stay here, away from the video below.
    MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons; onWheel: wheel => wheel.accepted = true }

    Keys.onEscapePressed: panel.page !== "items" && panel.tab === "discover" ? backButton.clicked() : panel.closeRequested()

    // --- Header: tabs and close ------------------------------------------------
    Row {
        id: tabs
        anchors { left: parent.left; top: parent.top; margins: 16 }
        spacing: 6
        Repeater {
            model: [["discover", "Discover"], ["addons", "Add-ons"], ["folders", "Your folders"]]
            Button {
                text: modelData[1]
                highlighted: panel.tab === modelData[0]
                onClicked: panel.tab = modelData[0]
            }
        }
    }
    Button {
        anchors { right: parent.right; top: parent.top; margins: 16 }
        text: "Close"
        onClicked: panel.closeRequested()
    }

    Item {
        id: body
        anchors { left: parent.left; right: parent.right; top: tabs.bottom; bottom: parent.bottom; margins: 16 }

        // --- Discover ------------------------------------------------------------
        Item {
            anchors.fill: parent
            visible: panel.tab === "discover"

            // Catalog, search and items
            Column {
                id: itemsPage
                anchors.fill: parent
                visible: panel.page === "items"
                spacing: 10

                Row {
                    width: parent.width
                    spacing: 8
                    ComboBox {
                        id: catalogBox
                        width: Math.min(320, parent.width * 0.5)
                        model: browser.catalogs
                        textRole: "name"
                        currentIndex: browser.catalogIndex
                        displayText: currentIndex >= 0 ? currentText : "Pick a list"
                        onActivated: index => browser.openCatalog(index, searchField.text)
                    }
                    TextField {
                        id: searchField
                        width: parent.width - catalogBox.width - 8
                        enabled: browser.catalogIndex >= 0 && browser.catalogs[browser.catalogIndex].searchable
                        placeholderText: enabled ? "Search" : "This list can't be searched"
                        onAccepted: browser.openCatalog(browser.catalogIndex, text)
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
                ListView {
                    id: itemList
                    width: parent.width
                    height: parent.height - y
                    clip: true
                    model: browser.items
                    ScrollBar.vertical: ScrollBar {}
                    delegate: RowItem {
                        primary: modelData.name + (modelData.releaseInfo ? "  ·  " + modelData.releaseInfo : "")
                        secondary: modelData.description
                        onClicked: { browser.openItem(index); panel.page = "detail" }
                    }
                    onAtYEndChanged: if (atYEnd && count > 0) browser.loadMore()
                    footer: Muted { width: itemList.width; height: browser.itemsLoading && browser.items.length > 0 ? 28 : 0; text: height > 0 ? "Loading more…" : "" }
                }
            }

            // Back, used by details and streams
            Button {
                id: backButton
                visible: panel.page !== "items"
                anchors { left: parent.left; top: parent.top }
                text: "‹ Back"
                onClicked: panel.page = panel.page === "streams" && browser.detail.isSeries ? "detail" : "items"
            }

            // Details and episodes
            Column {
                anchors { left: parent.left; right: parent.right; top: backButton.bottom; bottom: parent.bottom; topMargin: 10 }
                visible: panel.page === "detail"
                spacing: 8
                Row {
                    width: parent.width
                    spacing: 14
                    Image {
                        id: poster
                        width: 110; height: 163
                        source: browser.detail.poster || ""
                        visible: status === Image.Ready
                        fillMode: Image.PreserveAspectCrop
                        sourceSize.width: 220
                        asynchronous: true
                    }
                    Column {
                        width: parent.width - (poster.visible ? poster.width + 14 : 0)
                        spacing: 6
                        Heading { width: parent.width; text: browser.detail.name || "" }
                        Muted {
                            width: parent.width
                            visible: text !== ""
                            text: [browser.detail.releaseInfo, browser.detail.runtime, browser.detail.genres].filter(s => s).join("  ·  ")
                        }
                        Muted { width: parent.width; visible: text !== ""; text: browser.detail.description || ""; maximumLineCount: 6; elide: Text.ElideRight }
                        Button {
                            visible: !browser.detail.isSeries && !browser.detailLoading
                            text: "Find ways to play"
                            onClicked: { browser.openVideo(-1); panel.page = "streams" }
                        }
                    }
                }
                Muted {
                    width: parent.width
                    visible: text !== ""
                    text: browser.detailLoading ? "Loading details…" : browser.detailError !== "" ? "Details could not be loaded: " + browser.detailError : ""
                }
                ListView {
                    width: parent.width
                    height: parent.height - y
                    visible: browser.detail.isSeries === true
                    clip: true
                    model: browser.videos
                    ScrollBar.vertical: ScrollBar {}
                    section.property: "season"
                    section.delegate: Text {
                        required property string section
                        text: section
                        color: panel.accent
                        font.pixelSize: 14
                        font.weight: Font.DemiBold
                        topPadding: 10
                        bottomPadding: 4
                    }
                    delegate: RowItem {
                        primary: modelData.label
                        secondary: modelData.released
                        onClicked: { browser.openVideo(index); panel.page = "streams" }
                    }
                }
            }

            // Ways to play, best first, grouped by quality
            Column {
                anchors { left: parent.left; right: parent.right; top: backButton.bottom; bottom: parent.bottom; topMargin: 10 }
                visible: panel.page === "streams"
                spacing: 8
                Heading { width: parent.width; text: browser.streamsTitle }
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
                    width: parent.width
                    height: parent.height - y
                    clip: true
                    model: browser.streams
                    ScrollBar.vertical: ScrollBar {}
                    section.property: "group"
                    section.delegate: Text {
                        required property string section
                        text: section
                        color: panel.accent
                        font.pixelSize: 14
                        font.weight: Font.DemiBold
                        topPadding: 10
                        bottomPadding: 4
                    }
                    delegate: RowItem {
                        primary: modelData.provider + " · " + modelData.name
                        secondary: (modelData.description ? modelData.description.replace(/\n/g, "  ") + "\n" : "") + modelData.facts
                        onClicked: browser.play(index)
                    }
                }
            }
        }

        // --- Add-ons -----------------------------------------------------------------
        Column {
            anchors.fill: parent
            visible: panel.tab === "addons"
            spacing: 10
            Muted {
                width: parent.width
                text: "Add-ons give the lists, details and streams. Paste an add-on's install link (it ends in manifest.json). Cinemeta comes installed: it has lists and details, not streams."
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
                delegate: Rectangle {
                    width: ListView.view.width
                    height: addonText.implicitHeight + 20
                    radius: 8
                    color: "#14ffffff"
                    Column {
                        id: addonText
                        anchors { left: parent.left; right: removeButton.left; verticalCenter: parent.verticalCenter; margins: 10 }
                        spacing: 2
                        Text { text: modelData.name + "  " + modelData.version; color: panel.textColor; font.pixelSize: 14; font.weight: Font.DemiBold }
                        Muted { width: parent.width; text: modelData.description; visible: text !== ""; maximumLineCount: 2; elide: Text.ElideRight }
                        Muted { width: parent.width; text: "Offers: " + modelData.offers }
                        Muted { width: parent.width; visible: modelData.error !== ""; color: "#e09080"; text: "Last request failed: " + modelData.error }
                    }
                    Button {
                        id: removeButton
                        anchors { right: parent.right; verticalCenter: parent.verticalCenter; margins: 10 }
                        text: "Remove"
                        onClicked: browser.removeAddon(modelData.id)
                    }
                }
            }
        }

        // --- Your folders -------------------------------------------------------------
        Column {
            anchors.fill: parent
            visible: panel.tab === "folders"
            spacing: 10
            Muted {
                width: parent.width
                text: "Videos in these folders show up under This computer in Discover. Subtitle files next to a video, or in a Subs folder beside it, load with it."
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
                width: parent.width
                height: parent.height - y
                clip: true
                model: browser.folders
                delegate: RowItem {
                    primary: modelData
                    secondary: "Click to remove"
                    onClicked: browser.removeFolder(modelData)
                }
            }
        }
    }

    FolderDialog {
        id: folderDialog
        title: "Add a folder with videos"
        onAccepted: browser.addFolder(selectedFolder)
    }
}
