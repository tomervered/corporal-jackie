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
        for (const g of [appearanceGroup, pictureGroup, subtitlesGroup, torrentsGroup, privacyGroup, storageGroup, downloadsGroup, imagesGroup, historyGroup])
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
    // The four colours in use now.
    function currentColors() {
        return { accent: Theme.accent.toString(), canvas: Theme.canvas.toString(), panel: Theme.panel.toString(), text: Theme.text.toString() }
    }
    function paletteKey(colors) {
        return [colors.accent, colors.canvas, colors.panel, colors.text].join("/").toLowerCase()
    }
    function usePalette(entry) {
        const edits = JSON.parse(JSON.stringify(preferences.colorEdits || {}))
        if (entry[2] !== "custom") {
            delete edits[entry[2]]
        } else {
            const c = entry[1]
            edits.custom = { accent: c.accent, canvas: c.canvas, panel: c.panel, text: c.text }
        }
        preferences.setTheme(entry[2], edits)
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
        // A wide control (the palette) on a narrow page goes under the words,
        // so they are not squeezed into a column of single words.
        readonly property bool stacked: controlSlot.width > width * 0.5
        width: parent ? parent.width : 400
        height: stacked ? texts.implicitHeight + 10 + controlSlot.height + 20
                        : Math.max(controlSlot.childrenRect.height, texts.implicitHeight) + 20
        opacity: enabledHere ? 1 : 0.5
        Rectangle { width: parent.width; height: 1; color: Theme.divider }
        Column {
            id: texts
            anchors { left: parent.left; right: row.stacked ? parent.right : controlSlot.left; rightMargin: row.stacked ? 0 : 22 }
            y: row.stacked ? 10 : (row.height - implicitHeight) / 2
            spacing: 3
            Text { textFormat: Text.PlainText; width: parent.width; text: row.title; color: Theme.text; font.pixelSize: Theme.bodySize - 1; wrapMode: Text.WordWrap }
            Text { textFormat: Text.PlainText; width: parent.width; text: row.detail; color: Theme.muted; font.pixelSize: Theme.smallSize; wrapMode: Text.WordWrap; lineHeight: 1.1; visible: text !== "" }
        }
        Item {
            id: controlSlot
            x: row.stacked ? 0 : row.width - width
            y: row.stacked ? texts.y + texts.implicitHeight + 10 : (row.height - height) / 2
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
                // One list: the built-in looks, the ready-made palettes and
                // the ones the person saved. Commando Jackie and OLED black
                // are modes of their own; the rest set the Custom mode.
                SettingRow {
                    objectName: "colorPalettes"
                    title: "Color Palettes"
                    detail: "Commando Jackie is the default. OLED black turns the background and panels pure black. Palettes you save or import are added here."
                    Flow {
                        id: palettePicker
                        readonly property string liveKey: Theme.mode === "custom" ? panel.paletteKey(panel.currentColors()) : ""
                        width: 440
                        spacing: 8
                        Repeater {
                            model: Theme.palettes.concat((preferences.savedPalettes || []).map(p => [p.name, p, "custom", true]))
                            AbstractButton {
                                id: paletteButton
                                required property var modelData
                                readonly property var colors: modelData[1]
                                readonly property string mode: modelData[2]
                                readonly property string key: mode === "custom" ? panel.paletteKey(colors) : ""
                                readonly property bool chosen: Theme.mode === mode && (mode === "custom"
                                    ? key === palettePicker.liveKey : !(preferences.colorEdits || {})[mode])
                                objectName: "palette-" + modelData[0]
                                width: 104; height: 58
                                hoverEnabled: true
                                Accessible.name: modelData[0] + " palette" + (chosen ? ", chosen" : "")
                                onClicked: panel.usePalette(modelData)
                                background: Rectangle {
                                    radius: 7
                                    color: paletteButton.colors.canvas
                                    border.width: paletteButton.chosen || paletteButton.visualFocus || paletteButton.hovered ? 2 : 1
                                    border.color: paletteButton.chosen || paletteButton.visualFocus || paletteButton.hovered ? Theme.accent : Theme.line
                                    Rectangle {
                                        anchors { left: parent.left; right: parent.right; top: parent.top; margins: 5 }
                                        height: 20
                                        radius: 4
                                        color: paletteButton.colors.panel
                                        Rectangle {
                                            anchors { left: parent.left; leftMargin: 6; verticalCenter: parent.verticalCenter }
                                            width: 26; height: 8; radius: 4
                                            color: paletteButton.colors.accent
                                        }
                                        Icon {
                                            visible: paletteButton.chosen
                                            anchors { right: parent.right; rightMargin: 4; verticalCenter: parent.verticalCenter }
                                            name: "check"
                                            size: 14
                                            color: paletteButton.colors.text
                                        }
                                    }
                                    Text {
                                        textFormat: Text.PlainText
                                        anchors { left: parent.left; right: parent.right; leftMargin: 8; rightMargin: 6; bottom: parent.bottom; bottomMargin: 4 }
                                        text: paletteButton.modelData[0]
                                        color: paletteButton.colors.text
                                        font.pixelSize: 11
                                        lineHeight: 0.95
                                        wrapMode: Text.WordWrap
                                        maximumLineCount: 2
                                        elide: Text.ElideRight
                                    }
                                }
                                // A saved palette can be taken off the list.
                                IconButton {
                                    visible: paletteButton.modelData[3] === true && paletteButton.hovered
                                    x: parent.width - width + 6; y: -6
                                    width: 22; height: 22
                                    iconName: "close"
                                    iconSize: 11
                                    tip: "Remove " + paletteButton.modelData[0]
                                    background: Rectangle { radius: 11; color: Theme.panel; border.color: Theme.line }
                                    onClicked: preferences.removePalette(paletteButton.modelData[0])
                                }
                            }
                        }
                    }
                }
                SettingRow {
                    title: "Customize"
                    detail: "Changes the palette in use, in the pages and on the player."
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
                                    objectName: "swatch-" + swatchRow.modelData[0]
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
                                    onClicked: { colorPopup.key = swatchRow.modelData[0]; colorPopup.parent = swatch; colorPopup.open() }
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
                                    readonly property string presetName: Format.colorName(Theme[swatchRow.modelData[0]])
                                    text: presetName !== "" ? presetName : Theme[swatchRow.modelData[0]].toString().toUpperCase()
                                    color: Theme.muted
                                    font.family: presetName !== "" ? Qt.application.font.family : "monospace"
                                    font.pixelSize: Theme.smallSize
                                }
                            }
                        }
                    }
                }
                ActionRow {
                    title: "Restore this palette's colors"
                    AppButton {
                        text: "Restore defaults"
                        enabled: !!(preferences.colorEdits || {})[Theme.mode] && Theme.mode !== "custom"
                        onClicked: panel.resetColors()
                    }
                }
                SettingRow {
                    id: saveRow
                    title: "Save this palette"
                    detail: saveRow.result !== "" ? saveRow.result : "Name it to add it to Color Palettes, or export it to a file to share."
                    property string result: ""
                    Row {
                        spacing: 6
                        AppField {
                            id: paletteName
                            objectName: "paletteName"
                            width: 170
                            placeholderText: "Palette name"
                            maximumLength: 40
                            onAccepted: saveButton.clicked()
                        }
                        AppButton {
                            id: saveButton
                            objectName: "savePalette"
                            text: "Save"
                            enabled: paletteName.text.trim() !== ""
                            onClicked: {
                                const error = preferences.savePalette(paletteName.text, panel.currentColors())
                                if (error === "") panel.usePalette([paletteName.text.trim(), panel.currentColors(), "custom"])
                                saveRow.result = error !== "" ? error : "Saved as " + paletteName.text.trim() + "."
                            }
                        }
                        AppButton {
                            objectName: "exportColors"
                            text: "Export…"
                            onClicked: colorsExportDialog.open()
                        }
                    }
                }
                ActionRow {
                    id: importRow
                    title: "Import a palette"
                    detail: importRow.result !== "" ? importRow.result : "Load a palette file someone shared. It is added to Color Palettes and used."
                    property string result: ""
                    AppButton {
                        objectName: "importColors"
                        text: "Import…"
                        onClicked: colorsImportDialog.open()
                    }
                }
                Choice {
                    objectName: "playerHalo"
                    title: "Player control halo"
                    detail: "The soft fade behind the player's controls, in any color. With white or near-white panels the controls are charcoal."
                    options: [["full", "Full"], ["half", "Half"], ["little", "Just a little"], ["off", "Off"]]
                    value: preferences.playerHalo
                    onPicked: v => preferences.playerHalo = v
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
                // drawing from Gertie the Dinosaur (Winsor McCay, 1914, public
                // domain; images/README.md).
                Item { width: 1; height: 12 }
                Rectangle {
                    objectName: "subtitleSample"
                    width: Math.min(parent.width, 560)
                    height: Math.round(width * 9 / 16)
                    radius: 6
                    color: Theme.canvas
                    Image {
                        anchors.fill: parent
                        source: "images/subtitle-sample.jpg"
                        sourceSize: Qt.size(540, 365)
                        fillMode: Image.PreserveAspectCrop
                        smooth: true
                        asynchronous: true
                    }
                    CornerMask { anchors.fill: parent; radius: parent.radius; color: Theme.panel }
                    // Sized the way mpv sizes them: letters 55 and the outline
                    // its width in units of a 720-line picture, scaled to this
                    // one (Qt's own text outline is a fixed hairline).
                    readonly property real unit: height / 720
                    readonly property real outlineWidth: session.subtitleStyle.outline * unit
                    Rectangle {
                        anchors.centerIn: sample
                        width: sample.contentWidth + 2 * Math.max(4, parent.outlineWidth * 2)
                        height: sample.contentHeight + 2 * Math.max(2, parent.outlineWidth)
                        color: Qt.rgba(0, 0, 0, session.subtitleStyle.background / 100)
                    }
                    // The outline: copies of the line in black, all around it.
                    Repeater {
                        // Two rings of 16, so a thick outline has no gaps.
                        model: parent.outlineWidth > 0 ? 32 : 0
                        Text {
                            required property int index
                            readonly property real reach: sample.parent.outlineWidth * (index < 16 ? 1 : 0.5)
                            textFormat: Text.PlainText
                            x: sample.x + Math.cos((index % 16) * Math.PI / 8) * reach
                            y: sample.y + Math.sin((index % 16) * Math.PI / 8) * reach
                            text: sample.text
                            font: sample.font
                            color: "black"
                        }
                    }
                    Text {
                        textFormat: Text.PlainText
                        id: sample
                        anchors { horizontalCenter: parent.horizontalCenter; bottom: parent.bottom; bottomMargin: Math.round(parent.height * 0.05) }
                        text: "This is how subtitles look"
                        font.family: session.subtitleStyle.font !== "" ? session.subtitleStyle.font : "sans-serif"
                        font.pixelSize: Math.max(8, Math.round(55 * parent.unit * session.subtitleScale))
                        color: session.subtitleStyle.color
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
                    options: Format.subtitleSizes
                    value: Math.round(session.subtitleScale * 100) / 100
                    onPicked: v => session.subtitleScale = v
                }
                Choice {
                    title: "Color"
                    options: Format.subtitleColors
                    value: session.subtitleStyle.color.toUpperCase()
                    onPicked: v => panel.setSubtitleStyle("color", v)
                }
                Choice {
                    title: "Outline"
                    options: Format.subtitleOutlines
                    value: session.subtitleStyle.outline
                    onPicked: v => panel.setSubtitleStyle("outline", v)
                }
                Choice {
                    title: "Background"
                    options: Format.subtitleBackgrounds
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
                    title: "Use the built-in add-ons: WikiMaze and Public Domain Movies"
                    detail: "WikiMaze (on Vercel, from this app's author) sees your IP address and every title you search for or open. Public Domain Movies (on Stremio's server) sees your IP address, its list pages and your searches in it. Off removes both from Add-ons."
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

            // --- Downloads ---------------------------------------------------------
            Group {
                id: downloadsGroup
                objectName: "downloadsGroup"
                title: "Downloads"
                ActionRow {
                    title: "Download folder"
                    detail: downloads.folder + "\nVideos you download are saved here, apart from the torrent cache."
                    AppButton { text: "Change…"; onClicked: downloadFolderDialog.open() }
                    AppButton {
                        text: "Default"
                        visible: downloads.customFolder
                        onClicked: downloads.resetFolder()
                    }
                }
                SectionLabel { section: "Advanced" }
                Toggle {
                    objectName: "normalTorrentsToggle"
                    title: "Download torrents like a normal torrent program"
                    detail: "Pieces come rarest first and go straight into the folder, which can be faster on a big swarm, and a finished download can keep sharing. Off: the start of the video comes first, so you can play it while it downloads."
                    checked: downloads.normalTorrents
                    onToggled: on => downloads.normalTorrents = on
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

    // Named colors to pick from; "Other color…" opens the full picker.
    Popup {
        id: colorPopup
        objectName: "colorPopup"
        property string key
        y: parent ? parent.height + 6 : 0
        padding: 12
        background: Rectangle { radius: 10; color: Theme.overlay; border.color: Theme.line }
        Column {
            spacing: 10
            Grid {
                columns: 6
                spacing: 6
                Repeater {
                    model: Format.colorPresets
                    AbstractButton {
                        id: presetButton
                        required property var modelData
                        objectName: "colorPreset-" + modelData[1]
                        width: 34; height: 30
                        hoverEnabled: true
                        Accessible.name: modelData[1]
                        ToolTip.visible: hovered
                        ToolTip.text: modelData[1]
                        onClicked: { panel.setColor(colorPopup.key, modelData[0]); colorPopup.close() }
                        background: Rectangle {
                            radius: 5
                            color: presetButton.modelData[0]
                            border.width: presetButton.hovered || presetButton.visualFocus ? 2 : 1
                            border.color: presetButton.hovered || presetButton.visualFocus ? Theme.accent : Theme.line
                        }
                    }
                }
            }
            AppButton {
                text: "Other color…"
                onClicked: { colorDialog.key = colorPopup.key; colorDialog.selectedColor = Theme[colorPopup.key]; colorPopup.close(); colorDialog.open() }
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
    FileDialog {
        id: colorsExportDialog
        title: "Export colors"
        fileMode: FileDialog.SaveFile
        defaultSuffix: "json"
        nameFilters: ["Corporal Jackie colors (*.json)"]
        onAccepted: {
            const error = preferences.exportColors(selectedFile, paletteName.text, panel.currentColors())
            saveRow.result = error !== "" ? error : "Exported."
        }
    }
    FileDialog {
        id: colorsImportDialog
        title: "Import colors"
        fileMode: FileDialog.OpenFile
        nameFilters: ["Corporal Jackie colors (*.json)", "All files (*)"]
        onAccepted: {
            const error = preferences.importColors(selectedFile)
            importRow.result = error !== "" ? error : "Imported and added to Color Palettes."
        }
    }
    FolderDialog {
        id: downloadFolderDialog
        title: "Choose where downloads are saved"
        onAccepted: downloads.setFolder(selectedFolder)
    }
    FolderDialog {
        id: folderDialog
        title: "Choose where torrents are cached"
        onAccepted: preferences.setCacheFolder(selectedFolder)
    }
}
