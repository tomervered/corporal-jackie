import QtQuick
import QtQuick.Controls.Basic
import "Notices.js" as Notices
import QtQuick.Dialogs

// Settings (press P): hardware decoding, applied at once; where torrents are
// stored, applied the next time the app opens; the caches; and the watch
// history and library. Binds to `session` for playback options, and to
// `preferences`, `caches` and `library` for the rest.
Rectangle {
    id: panel

    signal closeRequested()

    color: Theme.panel
    border.color: "#30ffffff"
    radius: 12

    // Clicks and scrolls on the panel stay here, away from the video below.
    MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons; onWheel: wheel => wheel.accepted = true }
    Keys.onEscapePressed: panel.closeRequested()
    // Tab moves through the switches and buttons; the list scrolls to show
    // the one the keyboard is on.
    Connections {
        target: panel.visible ? panel.Window.window : null
        function onActiveFocusItemChanged() {
            const item = panel.Window.activeFocusItem
            if (!item || item === panel) return
            for (let p = item.parent; p; p = p.parent) {
                if (p !== body) continue
                const top = item.mapToItem(body, 0, 0).y
                if (top < flick.contentY) flick.contentY = Math.max(0, top - 16)
                else if (top + item.height > flick.contentY + flick.height)
                    flick.contentY = Math.min(flick.contentHeight - flick.height, top + item.height - flick.height + 16)
                return
            }
        }
    }
    onVisibleChanged: if (visible) { caches.measure(); panel.confirmClear = false; panel.result = ""; panel.importAsks = [] }

    function setSubtitleStyle(key, value) {
        const style = session.subtitleStyle
        style[key] = value
        session.subtitleStyle = style
    }

    property bool confirmClear: false
    property string result: ""      // what the last export or import did
    property url importFile         // an import waiting for a yes or no...
    property var importAsks: []     // ...to the add-ons and folders it adds

    function finishImport(withAddonsAndFolders) {
        const error = library.importFrom(panel.importFile, withAddonsAndFolders)
        panel.result = error !== "" ? error : "Imported."
        panel.importAsks = []
        panel.importFile = ""
    }

    // A section title with a line above it.
    component Heading: Item {
        property alias text: headingText.text
        width: parent ? parent.width : 400
        height: headingText.implicitHeight + 22
        Rectangle { width: parent.width; height: 1; y: 6; color: Theme.line }
        Text {
            id: headingText
            textFormat: Text.PlainText
            y: 18
            color: Theme.accent
            font.pixelSize: 13
            font.weight: Font.DemiBold
            font.capitalization: Font.AllUppercase
            font.letterSpacing: 0.8
        }
    }
    component Note: Text {
        textFormat: Text.PlainText
        width: parent ? parent.width : implicitWidth
        color: Theme.muted
        font.pixelSize: 12
        wrapMode: Text.WordWrap
    }
    // A switch with a title and one line saying what it does.
    component Toggle: Item {
        id: toggle
        property string title
        property string detail
        property bool checked
        property bool enabledHere: true
        signal toggled(bool on)
        width: parent ? parent.width : 400
        height: Math.max(sw.implicitHeight, texts.implicitHeight) + 8
        opacity: enabledHere ? 1 : 0.45
        Column {
            id: texts
            anchors { left: parent.left; right: sw.left; rightMargin: 12; verticalCenter: parent.verticalCenter }
            spacing: 2
            Text { textFormat: Text.PlainText; width: parent.width; text: toggle.title; color: Theme.text; font.pixelSize: 14; wrapMode: Text.WordWrap }
            Text { textFormat: Text.PlainText; width: parent.width; text: toggle.detail; color: Theme.muted; font.pixelSize: 12; wrapMode: Text.WordWrap; visible: text !== "" }
        }
        AppSwitch {
            id: sw
            anchors { right: parent.right; verticalCenter: parent.verticalCenter }
            focusPolicy: Qt.TabFocus
            enabled: toggle.enabledHere
            checked: toggle.checked
            onToggled: toggle.toggled(checked)
        }
    }
    // A title and a value on the left, buttons on the right.
    component ActionRow: Item {
        id: actionRow
        property string title
        property string detail
        default property alias buttons: buttonRow.children
        width: parent ? parent.width : 400
        height: Math.max(buttonRow.implicitHeight, actionTexts.implicitHeight) + 8
        Column {
            id: actionTexts
            anchors { left: parent.left; right: buttonRow.left; rightMargin: 12; verticalCenter: parent.verticalCenter }
            spacing: 2
            Text { textFormat: Text.PlainText; width: parent.width; text: actionRow.title; color: Theme.text; font.pixelSize: 14; wrapMode: Text.WordWrap }
            Text { textFormat: Text.PlainText; width: parent.width; text: actionRow.detail; color: Theme.muted; font.pixelSize: 12; wrapMode: Text.WordWrap; visible: text !== "" }
        }
        Row {
            id: buttonRow
            anchors { right: parent.right; verticalCenter: parent.verticalCenter }
            spacing: 4
        }
    }
    // A title, then a row of choices with one picked.
    component Choice: Column {
        id: choice
        property string title
        objectName: "choice " + title
        property var options: []      // [[value, label], ...]
        property var value
        signal picked(var value)
        width: parent ? parent.width : 400
        spacing: 6
        Text {
            textFormat: Text.PlainText
            text: choice.title
            color: Theme.text
            font.pixelSize: 14
        }
        Flow {
            width: parent.width
            spacing: 6
            Repeater {
                model: choice.options
                Chip {
                    required property var modelData
                    focusPolicy: Qt.TabFocus
                    text: modelData[1]
                    selected: choice.value === modelData[0]
                    onClicked: choice.picked(modelData[0])
                }
            }
        }
    }

    // --- Header ------------------------------------------------------------------
    Text {
        textFormat: Text.PlainText
        id: title
        anchors { left: parent.left; top: parent.top; margins: 16 }
        text: "Settings"
        color: Theme.text
        font.pixelSize: 20
        font.weight: Font.DemiBold
    }
    BarButton {
        objectName: "settingsClose"
        anchors { right: parent.right; verticalCenter: title.verticalCenter; rightMargin: 12 }
        text: "✕"
        tip: "Close (Esc)"
        onClicked: panel.closeRequested()
    }

    Flickable {
        id: flick
        anchors { left: parent.left; right: parent.right; top: title.bottom; bottom: parent.bottom; margins: 16 }
        contentHeight: body.implicitHeight
        clip: true
        ScrollBar.vertical: ScrollBar {}

        Column {
            id: body
            width: flick.width - 12
            spacing: 6

            // --- Picture ------------------------------------------------------------
            Heading { text: "Picture" }
            Toggle {
                title: "Hardware decoding"
                detail: "Lets the graphics card decode the video. Turn off only if the picture shows glitches."
                enabledHere: session.supports("HardwareDecoding")
                checked: session.hardwareDecoding
                onToggled: on => session.hardwareDecoding = on
            }
            Note { text: "Press I while playing to see dropped and late frames." }

            // --- Subtitles ------------------------------------------------------------
            Heading { text: "Subtitles" }
            // A sample line, drawn the way the player will draw it.
            Rectangle {
                width: parent.width
                height: 64
                radius: Theme.radius
                color: "#3a4a5a"
                clip: true
                Rectangle {
                    anchors.centerIn: sample
                    width: sample.contentWidth + 16
                    height: sample.contentHeight + 6
                    color: Qt.rgba(0, 0, 0, session.subtitleStyle.background / 100)
                }
                Text {
                    textFormat: Text.PlainText
                    id: sample
                    anchors.centerIn: parent
                    text: "This is how subtitles look"
                    font.family: session.subtitleStyle.font !== "" ? session.subtitleStyle.font : "sans-serif"
                    font.pixelSize: Math.round(20 * session.subtitleScale)
                    color: session.subtitleStyle.color
                    style: session.subtitleStyle.outline > 0 ? Text.Outline : Text.Normal
                    styleColor: "black"
                }
            }
            Item {
                width: parent.width
                height: fontBox.implicitHeight + 8
                enabled: session.supports("SubtitleStyle")
                Text {
                    textFormat: Text.PlainText
                    anchors { left: parent.left; verticalCenter: parent.verticalCenter }
                    text: "Font"
                    color: Theme.text
                    font.pixelSize: 14
                }
                AppComboBox {
                    id: fontBox
                    anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                    width: Math.min(280, parent.width * 0.6)
                    focusPolicy: Qt.TabFocus
                    model: ["Default"].concat(Qt.fontFamilies())
                    currentIndex: session.subtitleStyle.font === "" ? 0 : Math.max(0, model.indexOf(session.subtitleStyle.font))
                    onActivated: index => panel.setSubtitleStyle("font", index === 0 ? "" : model[index])
                }
            }
            Choice {
                title: "Size"
                options: [[0.75, "Small"], [1.0, "Normal"], [1.25, "Large"], [1.5, "Larger"], [2.0, "Huge"]]
                value: Math.round(session.subtitleScale * 100) / 100
                onPicked: v => session.subtitleScale = v
            }
            Choice {
                title: "Color"
                options: [["#FFFFFF", "White"], ["#FFE14D", "Yellow"], ["#C8C8C8", "Grey"]]
                value: session.subtitleStyle.color.toUpperCase()
                onPicked: v => panel.setSubtitleStyle("color", v)
            }
            Choice {
                title: "Outline"
                options: [[0, "None"], [1.5, "Thin"], [3, "Normal"], [5, "Thick"]]
                value: session.subtitleStyle.outline
                onPicked: v => panel.setSubtitleStyle("outline", v)
            }
            Choice {
                title: "Background"
                options: [[0, "None"], [40, "Light"], [70, "Dark"], [100, "Solid"]]
                value: session.subtitleStyle.background
                onPicked: v => panel.setSubtitleStyle("background", v)
            }
            Toggle {
                title: "Also restyle subtitles that have their own look"
                detail: "Some subtitles (often anime) come with their own fonts and placement. Off keeps those as they are."
                checked: session.subtitleStyle.restyleAll
                onToggled: on => panel.setSubtitleStyle("restyleAll", on)
            }
            Note { text: "While watching, G makes subtitles bigger and Shift+G smaller." }

            // --- Torrents and copyright -----------------------------------------
            Heading { text: "Torrents and copyright" }
            Note { text: Notices.copyright }
            Toggle {
                objectName: "settingsTorrents"
                title: Notices.torrentsTitle
                detail: Notices.torrentsDetail
                checked: preferences.useTorrents
                onToggled: on => {
                    preferences.useTorrents = on
                    if (!on) preferences.allowUploads = false
                }
            }
            Toggle {
                objectName: "settingsUploads"
                title: Notices.uploadsTitle
                detail: Notices.uploadsDetail
                enabledHere: preferences.useTorrents
                checked: preferences.allowUploads
                onToggled: on => preferences.allowUploads = on
            }

            // --- What may be contacted ------------------------------------------
            Heading { text: "Privacy: what Corporal Jackie may contact" }
            Note { text: "Each applies at once. A VPN hides your IP address from torrent peers; this app does not. PRIVACY.md, next to the app, lists everything." }
            Toggle {
                title: "Browse and search with WikiMaze"
                detail: "WikiMaze (on Vercel, from this app's author) sees your IP address and every title you search for or open. Off removes it from Add-ons."
                checked: preferences.useCatalogAddon
                onToggled: on => preferences.useCatalogAddon = on
            }
            Toggle {
                title: "Find peers through the DHT and your local network"
                detail: "Tells unrelated computers the torrent you open and your IP address."
                checked: preferences.useDht
                onToggled: on => preferences.useDht = on
            }
            Toggle {
                title: "Ask eight public trackers for peers"
                detail: "They learn your IP address and the torrent you open. A magnet link's own trackers are used either way."
                checked: preferences.usePublicTrackers
                onToggled: on => preferences.usePublicTrackers = on
            }
            Toggle {
                title: "Let your router open a port (UPnP, NAT-PMP)"
                detail: "Lets more peers connect to you."
                checked: preferences.usePortMapping
                onToggled: on => preferences.usePortMapping = on
            }

            // --- Torrents --------------------------------------------------------
            Heading { text: "Torrent storage" }
            Toggle {
                title: "Keep torrents in memory only"
                detail: "Nothing from the video is written to your disk, which spares an SSD. Jumping far back downloads that part again."
                checked: preferences.torrentsInMemory
                onToggled: on => preferences.torrentsInMemory = on
            }
            Choice {
                visible: preferences.torrentsInMemory
                title: "Memory to use"
                options: [[512, "512 MB"], [1024, "1 GB"], [2048, "2 GB"], [4096, "4 GB"]]
                value: preferences.memoryCapMB
                onPicked: v => preferences.memoryCapMB = v
            }
            Choice {
                visible: !preferences.torrentsInMemory
                title: "Disk space to use"
                options: [[5, "5 GB"], [10, "10 GB"], [20, "20 GB"], [50, "50 GB"]]
                value: preferences.diskCacheGB
                onPicked: v => preferences.diskCacheGB = v
            }
            Note {
                visible: !preferences.torrentsInMemory
                text: "Videos you watch stay on disk up to this size, so watching again starts at once. The oldest go first."
            }
            Item {
                visible: !preferences.torrentsInMemory
                width: parent.width
                height: folderColumn.implicitHeight + 8
                Column {
                    id: folderColumn
                    anchors { left: parent.left; right: folderButtons.left; rightMargin: 12; verticalCenter: parent.verticalCenter }
                    spacing: 2
                    Text { textFormat: Text.PlainText; text: "Cache folder"; color: Theme.text; font.pixelSize: 14 }
                    Text {
                        textFormat: Text.PlainText
                        width: parent.width
                        text: preferences.cacheFolder
                        color: Theme.muted
                        font.pixelSize: 12
                        elide: Text.ElideMiddle
                    }
                }
                Row {
                    id: folderButtons
                    anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                    spacing: 4
                    AppButton { text: "Change…"; focusPolicy: Qt.TabFocus; onClicked: folderDialog.open() }
                    AppButton {
                        text: "Default"
                        focusPolicy: Qt.TabFocus
                        visible: preferences.customCacheFolder
                        onClicked: preferences.resetCacheFolder()
                    }
                }
            }
            Note {
                visible: !preferences.torrentsInMemory
                text: "Pick a folder on a hard disk to keep torrent writes off your SSD."
            }
            Rectangle {
                visible: preferences.restartNeeded
                width: parent.width
                height: restartNote.implicitHeight + 16
                radius: 8
                color: "#33e8b04b"
                border.color: Theme.accent
                Text {
                    textFormat: Text.PlainText
                    id: restartNote
                    anchors { fill: parent; margins: 8 }
                    text: "Torrent storage changes apply the next time you open Corporal Jackie."
                    color: Theme.text
                    font.pixelSize: 13
                    wrapMode: Text.WordWrap
                }
            }
            ActionRow {
                title: "Torrent cache on disk"
                detail: caches.busy ? "Measuring…" : caches.torrentCacheUsed !== "" ? "Using " + caches.torrentCacheUsed + ". What is playing now is kept." : ""
                AppButton { text: "Clear"; focusPolicy: Qt.TabFocus; enabled: !caches.busy; onClicked: caches.clearTorrents() }
            }

            // --- Images ------------------------------------------------------------
            Heading { text: "Posters and images" }
            Choice {
                title: "Disk space for posters"
                options: [[100, "100 MB"], [250, "250 MB"], [500, "500 MB"], [1000, "1 GB"]]
                value: caches.imageCacheMB
                onPicked: v => caches.imageCacheMB = v
            }
            ActionRow {
                title: "Image cache"
                detail: caches.busy ? "Measuring…" : caches.imageCacheUsed !== "" ? "Using " + caches.imageCacheUsed + ". Posters are kept so lists show quickly." : ""
                AppButton { text: "Clear"; focusPolicy: Qt.TabFocus; enabled: !caches.busy; onClicked: caches.clearImages() }
            }

            // --- History and library -------------------------------------------------
            Heading { text: "History and library" }
            Toggle {
                title: "Remember what I watch"
                detail: "Off: nothing new goes into the history or Continue Watching. What is already there stays until you clear it."
                checked: library.keepHistory
                onToggled: on => library.keepHistory = on
            }
            ActionRow {
                title: "Watch history"
                detail: (library.historyCount === 1 ? "1 video" : library.historyCount + " videos") + ". Continue Watching comes from this. Clearing it also clears the log files, which name what played."
                AppButton {
                    text: panel.confirmClear ? "Clear all history?" : "Clear"
                    focusPolicy: Qt.TabFocus
                    onClicked: {
                        if (!panel.confirmClear) { panel.confirmClear = true; return }
                        library.clearHistory()
                        panel.confirmClear = false
                    }
                }
            }
            ActionRow {
                title: "Back up or move"
                detail: "One file with your library, history, add-ons and folders. Importing adds to what is here. Logins, signed stream links and add-on settings are left out; add-ons with settings are listed in the file to set up again."
                AppButton { text: "Export…"; focusPolicy: Qt.TabFocus; onClicked: exportDialog.open() }
                AppButton { text: "Import…"; focusPolicy: Qt.TabFocus; onClicked: importDialog.open() }
            }
            // An export can come from someone else, so the add-ons and folders
            // it would add are listed for a yes or no first (an add-on decides
            // what plays; a folder can be another computer's share). History
            // and library items import either way.
            Column {
                width: parent.width
                spacing: 8
                visible: panel.importAsks.length > 0
                Note { text: "This file also adds these. Add them only if you know where the file came from:" }
                Repeater {
                    model: panel.importAsks
                    Note { required property string modelData; text: "•  " + modelData }
                }
                Row {
                    spacing: 8
                    AppButton { text: "Add them too"; focusPolicy: Qt.TabFocus; onClicked: panel.finishImport(true) }
                    AppButton { text: "Import without them"; focusPolicy: Qt.TabFocus; onClicked: panel.finishImport(false) }
                    AppButton { text: "Cancel"; focusPolicy: Qt.TabFocus; onClicked: { panel.importAsks = []; panel.importFile = "" } }
                }
            }
            Note { visible: panel.result !== ""; text: panel.result }
            Note { visible: library.storageError !== ""; text: library.storageError }
        }
    }

    FileDialog {
        id: exportDialog
        title: "Export library and history"
        fileMode: FileDialog.SaveFile
        defaultSuffix: "json"
        nameFilters: ["Corporal Jackie export (*.json)"]
        onAccepted: {
            const error = library.exportTo(selectedFile)
            panel.result = error !== "" ? error : "Exported."
        }
    }
    FileDialog {
        id: importDialog
        title: "Import library and history"
        fileMode: FileDialog.OpenFile
        nameFilters: ["Corporal Jackie export (*.json)", "All files (*)"]
        onAccepted: {
            panel.result = ""
            panel.importFile = selectedFile
            panel.importAsks = library.importAsks(selectedFile)
            if (panel.importAsks.length === 0)
                panel.finishImport(false)
        }
    }
    FolderDialog {
        id: folderDialog
        title: "Choose where torrents are cached"
        onAccepted: preferences.setCacheFolder(selectedFolder)
    }
}
