import QtQuick
import QtQuick.Controls.Basic
import "Notices.js" as Notices
import "Format.js" as Format
import QtQuick.Dialogs

// Settings (press P), a page of groups that each open on their own:
// Appearance, Picture, Subtitles, Torrents and copyright, Privacy, Torrent
// storage, Posters and images, History and backup. Binds to `session` for
// playback options, and to `preferences`, `caches`, `graphics` and
// `library` for the rest.
Item {
    id: panel

    signal closeRequested()

    function takeFocus() { appearanceGroup.focusHeader() }
    // Opens one group (by its title) and scrolls to it.
    function openGroup(title) {
        for (const g of [appearanceGroup, pictureGroup, subtitlesGroup, torrentsGroup, privacyGroup, storageGroup, imagesGroup, historyGroup])
            if (g.title === title) { g.expanded = true; Qt.callLater(() => flick.contentY = Math.min(g.y, Math.max(0, flick.contentHeight - flick.height))) }
    }

    // Clicks and scrolls on the page stay here, away from the video below.
    MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons; onWheel: wheel => wheel.accepted = true }
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

    // One colour of the current look, as Appearance edits it.
    function setColor(key, value) {
        const edits = JSON.parse(JSON.stringify(preferences.colorEdits || {}))
        const own = edits[Theme.mode] || {}
        own[key] = value.toString().slice(0, 7)
        edits[Theme.mode] = own
        preferences.colorEdits = edits
    }
    function resetColors() {
        const edits = JSON.parse(JSON.stringify(preferences.colorEdits || {}))
        delete edits[Theme.mode]
        preferences.colorEdits = edits
    }

    // --- Pieces --------------------------------------------------------------------

    component Note: Text {
        textFormat: Text.PlainText
        width: parent ? parent.width : implicitWidth
        color: Theme.muted
        font.pixelSize: Theme.smallSize
        wrapMode: Text.WordWrap
        lineHeight: 1.1
        topPadding: 4
        bottomPadding: 4
    }
    // A title and one line saying what it does on the left; its control on
    // the right; a thin line above.
    component SettingRow: Item {
        id: row
        property string title
        property string detail
        property bool enabledHere: true
        default property alias control: controlSlot.data
        width: parent ? parent.width : 400
        height: Math.max(controlSlot.childrenRect.height, texts.implicitHeight) + 20
        opacity: enabledHere ? 1 : 0.5
        Rectangle { width: parent.width; height: 1; color: Theme.divider }
        Column {
            id: texts
            anchors { left: parent.left; right: controlSlot.left; rightMargin: 22; verticalCenter: parent.verticalCenter }
            spacing: 3
            Text { textFormat: Text.PlainText; width: parent.width; text: row.title; color: Theme.text; font.pixelSize: Theme.bodySize - 1; wrapMode: Text.WordWrap }
            Text { textFormat: Text.PlainText; width: parent.width; text: row.detail; color: Theme.muted; font.pixelSize: Theme.smallSize; wrapMode: Text.WordWrap; lineHeight: 1.1; visible: text !== "" }
        }
        Item {
            id: controlSlot
            anchors { right: parent.right; verticalCenter: parent.verticalCenter }
            width: childrenRect.width
            height: childrenRect.height
        }
    }
    // A switch row.
    component Toggle: SettingRow {
        id: toggle
        property bool checked
        signal toggled(bool on)
        AppSwitch {
            enabled: toggle.enabledHere
            checked: toggle.checked
            Accessible.name: toggle.title
            onToggled: toggle.toggled(checked)
        }
    }
    // A row with a drop-down of choices, one picked.
    component Choice: SettingRow {
        id: choice
        property var options: []      // [[value, label], ...]
        property var value
        signal picked(var value)
        AppCombo {
            width: 220
            enabled: choice.enabledHere
            Accessible.name: choice.title
            model: choice.options.map(o => o[1])
            currentIndex: choice.options.findIndex(o => o[0] === choice.value)
            displayText: currentIndex >= 0 ? currentText : ""
            onActivated: index => choice.picked(choice.options[index][0])
        }
    }
    // A title and a value on the left, buttons on the right.
    component ActionRow: SettingRow {
        id: actionRow
        default property alias buttons: buttonRow.data
        Row { id: buttonRow; spacing: 6 }
    }
    // A group that opens and closes on its title.
    component Group: Rectangle {
        id: group
        property string title
        property bool expanded: false
        default property alias content: groupBody.data
        function focusHeader() { groupHeader.forceActiveFocus() }
        width: parent ? parent.width : 400
        height: groupHeader.height + (expanded ? groupBody.implicitHeight + 14 : 0)
        radius: 7
        color: Theme.panel
        border.color: Theme.divider
        clip: true
        ItemDelegate {
            id: groupHeader
            width: parent.width
            height: 50
            leftPadding: 16
            rightPadding: 16
            Accessible.name: group.title
            Accessible.role: Accessible.Button
            contentItem: Item {
                Text {
                    textFormat: Text.PlainText
                    anchors { left: parent.left; verticalCenter: parent.verticalCenter }
                    text: group.title
                    color: Theme.text
                    font.pixelSize: Theme.bodySize
                    font.weight: Font.DemiBold
                }
                Icon {
                    anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                    name: group.expanded ? "chevronDown" : "chevronRight"
                    size: 17
                    color: Theme.muted
                }
            }
            background: Rectangle {
                radius: 7
                color: groupHeader.down ? Theme.pressed : groupHeader.hovered ? Theme.hover : "transparent"
                border.width: groupHeader.visualFocus ? 2 : 0
                border.color: Theme.accent
            }
            onClicked: group.expanded = !group.expanded
        }
        Column {
            id: groupBody
            visible: group.expanded
            anchors { left: parent.left; right: parent.right; top: groupHeader.bottom; leftMargin: 16; rightMargin: 16 }
        }
    }

    // --- The page --------------------------------------------------------------------

    Flickable {
        id: flick
        anchors.fill: parent
        contentHeight: body.implicitHeight + 24
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {}

        Column {
            id: body
            width: Math.min(920, flick.width - 14)
            spacing: 8

            // --- Appearance ----------------------------------------------------------
            Group {
                id: appearanceGroup
                objectName: "settingsAppearance"
                title: "Appearance"
                expanded: true
                Choice {
                    objectName: "colorMode"
                    title: "Color mode"
                    detail: "Olive dark is the default. OLED black turns the background and panels pure black. Bright is a light look for a sunny room."
                    options: [["dark", "Olive dark"], ["oled", "OLED black"], ["bright", "Bright"]]
                    value: preferences.colorMode
                    onPicked: v => preferences.colorMode = v
                }
                SettingRow {
                    title: "Color palette"
                    detail: "Changes apply to this mode only, in the pages and on the player."
                    Grid {
                        columns: 2
                        columnSpacing: 18
                        rowSpacing: 8
                        Repeater {
                            model: [["accent", "Accent"], ["canvas", "Background"], ["panel", "Panels"], ["text", "Text"]]
                            Row {
                                id: swatchRow
                                required property var modelData
                                readonly property bool locked: Theme.mode === "oled" && (modelData[0] === "canvas" || modelData[0] === "panel")
                                spacing: 8
                                opacity: locked ? 0.45 : 1
                                AbstractButton {
                                    id: swatch
                                    width: 34; height: 30
                                    enabled: !swatchRow.locked
                                    Accessible.name: swatchRow.modelData[1] + " color"
                                    ToolTip.visible: hovered
                                    ToolTip.text: "Change the " + swatchRow.modelData[1].toLowerCase() + " color"
                                    background: Rectangle {
                                        radius: 5
                                        color: Theme.canvas
                                        border.width: swatch.visualFocus ? 2 : 1
                                        border.color: swatch.visualFocus ? Theme.accent : Theme.line
                                        Rectangle { anchors { fill: parent; margins: 4 } radius: 3; color: Theme[swatchRow.modelData[0]] }
                                    }
                                    onClicked: { colorDialog.key = swatchRow.modelData[0]; colorDialog.selectedColor = Theme[swatchRow.modelData[0]]; colorDialog.open() }
                                }
                                Text {
                                    textFormat: Text.PlainText
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: 86
                                    text: swatchRow.modelData[1]
                                    color: Theme.text
                                    font.pixelSize: Theme.smallSize + 1
                                }
                                Text {
                                    textFormat: Text.PlainText
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: Theme[swatchRow.modelData[0]].toString().toUpperCase()
                                    color: Theme.muted
                                    font.family: "monospace"
                                    font.pixelSize: Theme.smallSize
                                }
                            }
                        }
                    }
                }
                ActionRow {
                    title: "Restore this mode's colors"
                    AppButton {
                        text: "Restore defaults"
                        enabled: !!(preferences.colorEdits || {})[Theme.mode]
                        onClicked: panel.resetColors()
                    }
                }
            }

            // --- Picture ------------------------------------------------------------
            Group {
                id: pictureGroup
                title: "Picture"
                Toggle {
                    title: "Hardware decoding"
                    detail: "Lets the graphics card decode the video. Turn off only if the picture shows glitches."
                    enabledHere: session.supports("HardwareDecoding")
                    checked: session.hardwareDecoding
                    onToggled: on => session.hardwareDecoding = on
                }
                // Laptops with two graphics cards (Windows): which one the app runs on.
                Choice {
                    visible: graphics.canChoose
                    title: "Graphics card"
                    detail: (graphics.restartNeeded ? "Restart the app to switch. " : "")
                          + "Power saving: " + graphics.savingName + ". High performance: " + graphics.performanceName + "."
                          + (graphics.activeName !== "" ? " Now running on " + graphics.activeName + "." : "")
                    options: [["auto", "Automatic"], ["saving", "Power saving"], ["performance", "High performance"]]
                    value: graphics.preference
                    onPicked: v => graphics.preference = v
                }
                Note {
                    visible: graphics.vsrHint !== ""
                    text: graphics.vsrHint
                }
                Choice {
                    visible: session.upscalers.length > 1
                    title: "Upscaling"
                    detail: session.upscaler === "off"
                          ? "Sharpens videos smaller than your screen. Snapdragon GSR and AMD CAS are the lightest, for weak graphics chips; AMD FSR is sharper; Anime4K is for cartoons and anime. Off uses no extra memory or graphics power."
                          : "Uses extra graphics power while a video plays. Turn it off if playback stutters on this computer."
                    options: session.upscalers.map(name => [name, Format.upscalerLabel(name)])
                    value: session.upscaler
                    onPicked: v => session.upscaler = v
                }
                Choice {
                    visible: session.upscaler !== "off"
                    title: "Upscale to"
                    options: Format.upscaleTargets
                    value: session.upscaleHeight
                    onPicked: v => session.upscaleHeight = v
                }
                Toggle {
                    visible: session.upscalers.length > 1
                    objectName: "upscalerBadgeToggle"
                    title: "Show upscaling indicator during playback"
                    detail: "The note at the top left of the video that says what the upscaler is doing."
                    checked: preferences.showUpscalerBadge
                    onToggled: on => preferences.showUpscalerBadge = on
                }
                Note { text: "Press I while playing to see dropped and late frames." }
            }

            // --- Subtitles ------------------------------------------------------------
            Group {
                id: subtitlesGroup
                title: "Subtitles"
                // A sample line, drawn the way the player will draw it, over a
                // frame of A Trip to the Moon (Georges Méliès, 1902, public
                // domain; images/README.md).
                Item { width: 1; height: 12 }
                Rectangle {
                    objectName: "subtitleSample"
                    width: Math.min(parent.width, 560)
                    height: 190
                    radius: 6
                    clip: true
                    color: "black"
                    Image {
                        anchors { top: parent.top; bottom: parent.bottom; horizontalCenter: parent.horizontalCenter }
                        width: height * 480 / 365
                        source: "images/subtitle-sample.jpg"
                        sourceSize: Qt.size(480, 365)
                        fillMode: Image.PreserveAspectFit
                        smooth: true
                        asynchronous: true
                    }
                    Rectangle {
                        anchors.centerIn: sample
                        width: sample.contentWidth + 16
                        height: sample.contentHeight + 6
                        color: Qt.rgba(0, 0, 0, session.subtitleStyle.background / 100)
                    }
                    Text {
                        textFormat: Text.PlainText
                        id: sample
                        anchors { horizontalCenter: parent.horizontalCenter; bottom: parent.bottom; bottomMargin: 14 }
                        text: "This is how subtitles look"
                        font.family: session.subtitleStyle.font !== "" ? session.subtitleStyle.font : "sans-serif"
                        font.pixelSize: Math.round(20 * session.subtitleScale)
                        color: session.subtitleStyle.color
                        style: session.subtitleStyle.outline > 0 ? Text.Outline : Text.Normal
                        styleColor: "black"
                    }
                }
                Item { width: 1; height: 12 }
                Choice {
                    title: "Font"
                    enabledHere: session.supports("SubtitleStyle")
                    options: [["", "Default"]].concat(Qt.fontFamilies().map(f => [f, f]))
                    value: session.subtitleStyle.font
                    onPicked: v => panel.setSubtitleStyle("font", v)
                }
                Choice {
                    title: "Size"
                    detail: "While watching, G makes subtitles bigger and Shift+G smaller."
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
            }

            // --- Torrents and copyright -----------------------------------------
            Group {
                id: torrentsGroup
                title: "Torrents and copyright"
                Note { text: Notices.copyright; topPadding: 4; bottomPadding: 10 }
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
            }

            // --- What may be contacted ------------------------------------------
            Group {
                id: privacyGroup
                title: "Privacy"
                Note { text: "What Corporal Jackie may contact. Each applies at once. A VPN hides your IP address from torrent peers; this app does not. PRIVACY.md, next to the app, lists everything."; bottomPadding: 10 }
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
            }

            // --- Torrents --------------------------------------------------------
            Group {
                id: storageGroup
                title: "Torrent storage"
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
                    detail: "Videos you watch stay on disk up to this size, so watching again starts at once. The oldest go first."
                    options: [[5, "5 GB"], [10, "10 GB"], [20, "20 GB"], [50, "50 GB"]]
                    value: preferences.diskCacheGB
                    onPicked: v => preferences.diskCacheGB = v
                }
                ActionRow {
                    visible: !preferences.torrentsInMemory
                    title: "Cache folder"
                    detail: preferences.cacheFolder + "\nPick a folder on a hard disk to keep torrent writes off your SSD."
                    AppButton { text: "Change…"; onClicked: folderDialog.open() }
                    AppButton {
                        text: "Default"
                        visible: preferences.customCacheFolder
                        onClicked: preferences.resetCacheFolder()
                    }
                }
                Rectangle {
                    visible: preferences.restartNeeded
                    width: parent.width
                    height: restartNote.implicitHeight + 16
                    radius: 6
                    color: Theme.raised
                    border.color: Theme.accent
                    Text {
                        textFormat: Text.PlainText
                        id: restartNote
                        anchors { fill: parent; margins: 8 }
                        text: "Torrent storage changes apply the next time you open Corporal Jackie."
                        color: Theme.text
                        font.pixelSize: Theme.smallSize
                        wrapMode: Text.WordWrap
                    }
                }
                ActionRow {
                    title: "Torrent cache on disk"
                    detail: caches.busy ? "Measuring…" : caches.torrentCacheUsed !== "" ? "Using " + caches.torrentCacheUsed + ". What is playing now is kept." : ""
                    AppButton { text: "Clear torrent cache"; enabled: !caches.busy; onClicked: caches.clearTorrents() }
                }
            }

            // --- Images ------------------------------------------------------------
            Group {
                id: imagesGroup
                title: "Posters and images"
                Choice {
                    title: "Disk space for posters"
                    options: [[100, "100 MB"], [250, "250 MB"], [500, "500 MB"], [1000, "1 GB"]]
                    value: caches.imageCacheMB
                    onPicked: v => caches.imageCacheMB = v
                }
                ActionRow {
                    title: "Image cache"
                    detail: caches.busy ? "Measuring…" : caches.imageCacheUsed !== "" ? "Using " + caches.imageCacheUsed + ". Posters are kept so lists show quickly." : ""
                    AppButton { text: "Clear image cache"; enabled: !caches.busy; onClicked: caches.clearImages() }
                }
            }

            // --- History and library -------------------------------------------------
            Group {
                id: historyGroup
                title: "History and backup"
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
                        text: panel.confirmClear ? "Clear all history?" : "Clear watch history"
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
                    AppButton { text: "Export…"; onClicked: exportDialog.open() }
                    AppButton { text: "Import…"; onClicked: importDialog.open() }
                }
                // An export can come from someone else, so the add-ons and folders
                // it would add are listed for a yes or no first (an add-on decides
                // what plays; a folder can be another computer's share). History
                // and library items import either way.
                Column {
                    width: parent.width
                    spacing: 8
                    visible: panel.importAsks.length > 0
                    bottomPadding: 8
                    Note { text: "This file also adds these. Add them only if you know where the file came from:" }
                    Repeater {
                        model: panel.importAsks
                        Note { required property string modelData; text: "•  " + modelData }
                    }
                    Row {
                        spacing: 8
                        AppButton { text: "Add them too"; onClicked: panel.finishImport(true) }
                        AppButton { text: "Import without them"; onClicked: panel.finishImport(false) }
                        AppButton { text: "Cancel"; onClicked: { panel.importAsks = []; panel.importFile = "" } }
                    }
                }
                Note { visible: panel.result !== ""; text: panel.result }
                Note { visible: library.storageError !== ""; text: library.storageError }
            }
        }
    }

    ColorDialog {
        id: colorDialog
        property string key
        title: "Pick a color"
        onAccepted: panel.setColor(key, selectedColor)
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
