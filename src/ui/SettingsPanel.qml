import QtQuick
import QtQuick.Controls.Basic
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
    onVisibleChanged: if (visible) { caches.measure(); panel.confirmClear = false; panel.result = "" }

    function setSubtitleStyle(key, value) {
        const style = session.subtitleStyle
        style[key] = value
        session.subtitleStyle = style
    }

    property bool confirmClear: false
    property string result: ""      // what the last export or import did

    component Heading: Text {
        color: Theme.text
        font.pixelSize: 16
        font.weight: Font.DemiBold
        topPadding: 8
    }
    component Note: Text {
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
            Text { width: parent.width; text: toggle.title; color: Theme.text; font.pixelSize: 14; wrapMode: Text.WordWrap }
            Text { width: parent.width; text: toggle.detail; color: Theme.muted; font.pixelSize: 12; wrapMode: Text.WordWrap; visible: text !== "" }
        }
        Switch {
            id: sw
            anchors { right: parent.right; verticalCenter: parent.verticalCenter }
            focusPolicy: Qt.NoFocus
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
            Text { width: parent.width; text: actionRow.title; color: Theme.text; font.pixelSize: 14; wrapMode: Text.WordWrap }
            Text { width: parent.width; text: actionRow.detail; color: Theme.muted; font.pixelSize: 12; wrapMode: Text.WordWrap; visible: text !== "" }
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
        property var options: []      // [[value, label], ...]
        property var value
        signal picked(var value)
        width: parent ? parent.width : 400
        spacing: 6
        Text {
            text: choice.title
            color: Theme.text
            font.pixelSize: 14
        }
        Row {
            spacing: 4
            Repeater {
                model: choice.options
                Button {
                    focusPolicy: Qt.NoFocus
                    text: modelData[1]
                    checkable: true
                    checked: choice.value === modelData[0]
                    onClicked: choice.picked(modelData[0])
                }
            }
        }
    }

    // --- Header ------------------------------------------------------------------
    Text {
        id: title
        anchors { left: parent.left; top: parent.top; margins: 16 }
        text: "Settings"
        color: Theme.text
        font.pixelSize: 20
        font.weight: Font.DemiBold
    }
    Button {
        anchors { right: parent.right; top: parent.top; margins: 12 }
        text: "Close"
        focusPolicy: Qt.NoFocus
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
                radius: 8
                color: "#3a4a5a"
                clip: true
                Rectangle {
                    anchors.centerIn: sample
                    width: sample.contentWidth + 16
                    height: sample.contentHeight + 6
                    color: Qt.rgba(0, 0, 0, session.subtitleStyle.background / 100)
                }
                Text {
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
                    anchors { left: parent.left; verticalCenter: parent.verticalCenter }
                    text: "Font"
                    color: Theme.text
                    font.pixelSize: 14
                }
                ComboBox {
                    id: fontBox
                    anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                    width: Math.min(280, parent.width * 0.6)
                    focusPolicy: Qt.NoFocus
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
                    Text { text: "Cache folder"; color: Theme.text; font.pixelSize: 14 }
                    Text {
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
                    Button { text: "Change…"; focusPolicy: Qt.NoFocus; onClicked: folderDialog.open() }
                    Button {
                        text: "Default"
                        focusPolicy: Qt.NoFocus
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
                Button { text: "Clear"; focusPolicy: Qt.NoFocus; enabled: !caches.busy; onClicked: caches.clearTorrents() }
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
                Button { text: "Clear"; focusPolicy: Qt.NoFocus; enabled: !caches.busy; onClicked: caches.clearImages() }
            }

            // --- History and library -------------------------------------------------
            Heading { text: "History and library" }
            ActionRow {
                title: "Watch history"
                detail: (library.historyCount === 1 ? "1 video" : library.historyCount + " videos") + ". Continue Watching comes from this."
                Button {
                    text: panel.confirmClear ? "Clear all history?" : "Clear"
                    focusPolicy: Qt.NoFocus
                    enabled: library.historyCount > 0
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
                Button { text: "Export…"; focusPolicy: Qt.NoFocus; onClicked: exportDialog.open() }
                Button { text: "Import…"; focusPolicy: Qt.NoFocus; onClicked: importDialog.open() }
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
            const error = library.importFrom(selectedFile)
            panel.result = error !== "" ? error : "Imported."
        }
    }
    FolderDialog {
        id: folderDialog
        title: "Choose where torrents are cached"
        onAccepted: preferences.setCacheFolder(selectedFolder)
    }
}
