import QtQuick
import QtQuick.Controls.Basic
import CorporalJackie.Player   // VideoSurface, Session and the Playback enums, from C++
import "Format.js" as Format

// The bar under the video, one row on a fade: play, next chapter, the time,
// seek (with chapter breaks and the A-B range), then chapters, the speaker,
// audio and subtitles, episodes (for a series), playback settings and full
// screen. Back and Close sit at the top right (Main.qml).
//
// Chapters, the speaker, CC and the gear each open a panel above the bar.
// One is open at a time: a click on the same button closes it, a click on
// another switches. Every icon has a tooltip and a screen-reader name.
Item {
    id: controlBar
    property bool shown: true
    property bool fullScreen: false
    property bool statsShown: false
    property bool shortcutsShown: false
    // "", "chapters", "volume", "tracks" or "settings".
    property string openPanel: ""
    // The playback setting showing its choices, or "".
    property string openChoice: ""
    property string tracksTab: "subtitles"
    // The pointer is on the bar or a panel is open: keep it showing.
    readonly property bool inUse: barHover.hovered || seek.pressed || openPanel !== ""
    signal subtitleFileRequested()
    signal audioFileRequested()
    signal settingsRequested()
    signal shortcutsRequested()
    signal statsRequested()
    signal fullScreenRequested()
    signal episodesRequested()

    function togglePanel(name) {
        openPanel = openPanel === name ? "" : name
        openChoice = ""
    }
    function closePanel() { openPanel = ""; openChoice = "" }
    onShownChanged: if (!shown) closePanel()

    // One wheel notch over the speaker or its slider: 5%.
    function nudgeVolume(delta) {
        if (delta === 0) return
        session.volume = Math.max(0, Math.min(session.maxVolume, Math.round(session.volume + (delta > 0 ? 5 : -5))))
        if (session.muted) session.muted = false
    }

    height: 84
    opacity: shown ? 1 : 0
    Behavior on opacity { NumberAnimation { duration: 200 } }
    HoverHandler { id: barHover }

    // The fade that keeps the icons readable over a bright picture.
    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0; color: Qt.rgba(Theme.pPanel.r, Theme.pPanel.g, Theme.pPanel.b, 0) }
            GradientStop { position: 0.7; color: Qt.rgba(Theme.pPanel.r, Theme.pPanel.g, Theme.pPanel.b, Theme.haloStrength) }
            GradientStop { position: 1; color: Qt.rgba(Theme.pPanel.r, Theme.pPanel.g, Theme.pPanel.b, Theme.haloStrength) }
        }
    }
    // Clicks on the bar stay here (they would pause the video below).
    MouseArea { anchors.fill: parent; enabled: controlBar.shown; acceptedButtons: Qt.AllButtons }

    Row {
        id: leftGroup
        anchors { left: parent.left; leftMargin: 14; bottom: parent.bottom; bottomMargin: 10 }
        spacing: 2
        IconButton {
            onPlayer: true
            objectName: "playPause"
            iconName: session.paused || session.state === Playback.Ended ? "play" : "pause"
            tip: session.paused || session.state === Playback.Ended ? "Play (Space)" : "Pause (Space)"
            Accessible.name: session.paused || session.state === Playback.Ended ? "Play" : "Pause"
            onClicked: session.togglePause()
        }
        // The video's next chapter (a song in a concert, a scene in a film).
        IconButton {
            onPlayer: true
            objectName: "nextTrack"
            visible: session.chapters.length > 1
            enabled: session.currentChapter < session.chapters.length - 1
            iconName: "next"
            iconSize: 18
            tip: "Next chapter"
            onClicked: session.nextChapter()
        }
        IconButton {
            onPlayer: true
            id: chaptersButton
            objectName: "trackList"
            visible: session.chapters.length > 1
            iconName: "chapters"
            tip: controlBar.openPanel === "chapters" ? "" : "Chapters"
            Accessible.name: "Chapters"
            active: controlBar.openPanel === "chapters"
            onClicked: controlBar.togglePanel("chapters")
        }
        // Where it is, bright; how long it is, muted.
        Row {
            anchors.verticalCenter: parent.verticalCenter
            leftPadding: 8
            Accessible.role: Accessible.StaticText
            Accessible.name: "Played " + Format.time(session.position) + " of " + Format.time(session.duration)
            Text {
                textFormat: Text.PlainText
                text: Format.time(seek.pressed ? seek.value : session.position)
                color: Theme.pText
                font.pixelSize: Theme.smallSize + 1
            }
            Text {
                textFormat: Text.PlainText
                visible: controlBar.width > 640
                text: " / " + Format.time(session.duration)
                color: Theme.pMuted
                font.pixelSize: Theme.smallSize + 1
            }
        }
    }

    // --- Seek ------------------------------------------------------------------------

    Slider {
        id: seek
        objectName: "seekBar"
        anchors { left: leftGroup.right; right: rightGroup.left; leftMargin: 14; rightMargin: 8; verticalCenter: leftGroup.verticalCenter }
        height: 30
        from: 0
        to: Math.max(1, session.duration)
        value: pressed ? value : session.position
        focusPolicy: Qt.NoFocus
        onMoved: session.seek(value)
        Accessible.name: "Playback position"
        Accessible.description: Format.time(session.position) + " of " + Format.time(session.duration)
        HoverHandler { id: seekHover }

        // The chapter a time falls in, or -1.
        function chapterAt(t) {
            let found = -1
            for (let i = 0; i < session.chapters.length; ++i)
                if (session.chapters[i].start <= t) found = i
            return found
        }
        readonly property real hoverTime: seekHover.hovered
            ? Math.max(0, Math.min(1, (seekHover.point.position.x - leftPadding) / availableWidth)) * to : 0

        background: Item {
            x: seek.leftPadding
            y: seek.topPadding + seek.availableHeight / 2 - height / 2
            width: seek.availableWidth
            height: seekHover.hovered || seek.pressed ? 5 : 4
            Behavior on height { NumberAnimation { duration: 100 } }
            Rectangle { anchors.fill: parent; radius: height / 2; color: Theme.pLine }
            Rectangle {
                width: seek.visualPosition * parent.width
                height: parent.height
                radius: height / 2
                color: Theme.accent
            }
            // A-B repeat range
            Rectangle {
                visible: session.abLoopA >= 0 && session.duration > 0
                x: (session.abLoopA / Math.max(1, session.duration)) * parent.width
                width: session.abLoopB >= 0 ? ((session.abLoopB - session.abLoopA) / Math.max(1, session.duration)) * parent.width : 2
                y: parent.height + 3
                height: 3
                color: Theme.pText
                opacity: 0.8
            }
            // Chapter starts: small breaks in the track.
            Repeater {
                model: session.duration > 0 ? session.chapters : []
                Rectangle {
                    required property var modelData
                    visible: modelData.start > 0
                    x: (modelData.start / session.duration) * parent.width - 1
                    y: -1
                    width: 2
                    height: parent.height + 2
                    color: Theme.pCanvas
                }
            }
        }
        handle: Rectangle {
            x: seek.leftPadding + seek.visualPosition * seek.availableWidth - width / 2
            y: seek.topPadding + seek.availableHeight / 2 - height / 2
            width: seekHover.hovered || seek.pressed ? 14 : 11
            height: width
            radius: width / 2
            color: Theme.accent
            border.color: "#80000000"
            border.width: 1
            Behavior on width { NumberAnimation { duration: 100 } }
        }
        // The time (and chapter) under the pointer.
        Rectangle {
            visible: seekHover.hovered && session.duration > 0
            x: Math.max(0, Math.min(seek.width - width, seekHover.point.position.x - width / 2))
            y: -height - 4
            width: hoverText.implicitWidth + 14
            height: hoverText.implicitHeight + 8
            radius: 4
            color: Theme.pPanel
            border.color: Theme.pLine
            Text {
                id: hoverText
                textFormat: Text.PlainText
                anchors.centerIn: parent
                text: {
                    const i = seek.chapterAt(seek.hoverTime)
                    const c = i >= 0 ? session.chapters[i] : null
                    return Format.time(seek.hoverTime) + (c ? "  ·  " + (c.title || "Chapter " + (i + 1)) : "")
                }
                color: Theme.pText
                font.pixelSize: 12
            }
        }
    }

    // --- Right-hand icons ----------------------------------------------------------

    Row {
        id: rightGroup
        anchors { right: parent.right; rightMargin: 12; verticalCenter: leftGroup.verticalCenter }
        spacing: 0

        IconButton {

            onPlayer: true
            objectName: "episodesButton"
            visible: browser.playingEpisodes.length > 0
            iconName: "episodes"
            tip: "Episodes"
            onClicked: { controlBar.closePanel(); controlBar.episodesRequested() }
        }
        // The speaker: 0 to 3 waves for the level, the accent colour above
        // 100%. A click opens the volume slider above it, the next closes it.
        IconButton {
            onPlayer: true
            id: speaker
            objectName: "volumeButton"
            readonly property real level: session.muted ? 0 : session.volume
            iconName: session.muted ? "muted"
                    : level <= 0 ? "volume0" : level <= 33 ? "volume1" : level <= 66 ? "volume2" : "volume3"
            iconColor: level > 100 ? Theme.accent : Theme.pText
            tip: ""
            Accessible.name: "Volume " + Math.round(session.volume) + "%" + (session.muted ? ", muted" : "")
            onClicked: controlBar.togglePanel("volume")
            WheelHandler { onWheel: event => controlBar.nudgeVolume(event.angleDelta.y) }
        }
        IconButton {
            onPlayer: true
            id: tracksButton
            objectName: "captionsButton"
            iconName: "captions"
            tip: controlBar.openPanel === "tracks" ? "" : "Audio and subtitles"
            Accessible.name: "Audio and subtitles"
            active: controlBar.openPanel === "tracks"
            onClicked: controlBar.togglePanel("tracks")
        }
        IconButton {
            onPlayer: true
            id: gearButton
            objectName: "optionsButton"
            iconName: "gear"
            tip: controlBar.openPanel === "settings" ? "" : "Playback settings"
            Accessible.name: "Playback settings"
            active: controlBar.openPanel === "settings"
            onClicked: controlBar.togglePanel("settings")
        }
        IconButton {
            onPlayer: true
            iconName: controlBar.fullScreen ? "windowed" : "fullscreen"
            tip: controlBar.fullScreen ? "Exit full screen (F)" : "Full screen (F)"
            onClicked: controlBar.fullScreenRequested()
        }
    }

    // --- Panels, above the bar -------------------------------------------------------

    readonly property real panelBottom: rightGroup.y - 8
    readonly property real panelMaxHeight: Math.max(160, (parent ? parent.height : 700) - height - 70)

    // Chapters: the file's names (or "Chapter N"), where each starts, and
    // the one playing filled. A click goes there.
    PlayerPanel {
        id: chaptersPanel
        objectName: "chaptersPanel"
        visible: controlBar.openPanel === "chapters"
        title: "Chapters"
        width: Math.min(340, controlBar.width - 24)
        maxHeight: controlBar.panelMaxHeight
        x: Math.max(12, Math.min(controlBar.width - width - 12, leftGroup.x + chaptersButton.x + chaptersButton.width / 2 - width / 2))
        y: controlBar.panelBottom - height
        Repeater {
            model: chaptersPanel.visible ? session.chapters : []
            PanelRow {
                required property var modelData
                required property int index
                label: modelData.title || "Chapter " + (index + 1)
                value: Format.time(modelData.start)
                selected: index === session.currentChapter
                tickSpace: true
                onClicked: { session.goToChapter(index); controlBar.closePanel() }
            }
        }
    }

    // Volume: a vertical slider to 130%, with a mark and a small catch at 100%.
    Rectangle {
        id: volumePanel
        objectName: "volumePanel"
        visible: controlBar.openPanel === "volume"
        width: 64
        height: 232
        x: Math.min(controlBar.width - width - 4, rightGroup.x + speaker.x + speaker.width / 2 - width / 2)
        y: controlBar.panelBottom - height
        radius: 12
        color: Theme.pPanel
        border.color: Qt.rgba(Theme.pText.r, Theme.pText.g, Theme.pText.b, 0.17)
        MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons }
        WheelHandler { onWheel: event => controlBar.nudgeVolume(event.angleDelta.y) }

        Text {
            id: volumeValue
            textFormat: Text.PlainText
            anchors { top: parent.top; topMargin: 12; horizontalCenter: parent.horizontalCenter }
            text: Math.round(volumeSlider.pressed ? volumeSlider.value : session.volume) + "%"
            color: session.volume > 100 ? Theme.accent : Theme.pText
            font.pixelSize: Theme.bodySize
            font.weight: Font.DemiBold
        }
        Slider {
            id: volumeSlider
            objectName: "volumeSlider"
            orientation: Qt.Vertical
            anchors { top: volumeValue.bottom; topMargin: 8; bottom: muteToggle.top; bottomMargin: 4; horizontalCenter: parent.horizontalCenter }
            width: 30
            from: 0; to: session.maxVolume
            stepSize: 1
            value: session.volume
            focusPolicy: Qt.NoFocus
            onMoved: {
                session.volume = Math.abs(value - 100) < 3 ? 100 : value
                if (session.muted) session.muted = false
            }
            Accessible.name: "Player volume"
            Accessible.description: Math.round(session.volume) + "%"
            background: Item {
                x: volumeSlider.leftPadding + volumeSlider.availableWidth / 2 - width / 2
                y: volumeSlider.topPadding
                width: 4
                height: volumeSlider.availableHeight
                Rectangle { anchors.fill: parent; radius: 2; color: Theme.pLine }
                Rectangle {
                    anchors.bottom: parent.bottom
                    width: parent.width
                    height: (1 - volumeSlider.visualPosition) * parent.height
                    radius: 2
                    color: session.muted ? Theme.pMuted : Theme.accent
                }
                Rectangle {
                    y: (1 - 100 / volumeSlider.to) * parent.height - height / 2
                    x: -4
                    width: 12
                    height: 2
                    color: Theme.pText
                    opacity: 0.7
                }
                Text {
                    textFormat: Text.PlainText
                    x: 12
                    y: (1 - 100 / volumeSlider.to) * parent.height - height / 2
                    text: "100"
                    color: Theme.pMuted
                    font.pixelSize: 10
                }
            }
            handle: Rectangle {
                x: volumeSlider.leftPadding + volumeSlider.availableWidth / 2 - width / 2
                y: volumeSlider.topPadding + volumeSlider.visualPosition * (volumeSlider.availableHeight - height)
                width: volumeSlider.pressed ? 16 : 14
                height: width
                radius: width / 2
                color: Theme.pText
                border.color: "#80000000"
            }
        }
        IconButton {
            onPlayer: true
            id: muteToggle
            objectName: "muteButton"
            anchors { bottom: parent.bottom; bottomMargin: 6; horizontalCenter: parent.horizontalCenter }
            width: 36; height: 36
            iconSize: 18
            iconName: session.muted ? "muted" : "volume0"
            active: session.muted
            tip: session.muted ? "Unmute (M)" : "Mute (M)"
            onClicked: session.muted = !session.muted
        }
    }

    // Audio and subtitles: two tabs of tracks, each with its language and
    // where it comes from, a way to add a file, and the subtitle timing.
    component SmallButton: AbstractButton {
        id: small
        focusPolicy: Qt.NoFocus
        implicitHeight: 30
        leftPadding: 10
        rightPadding: 10
        hoverEnabled: true
        Accessible.name: text
        contentItem: Text {
            textFormat: Text.PlainText
            text: small.text
            color: Theme.pText
            font.pixelSize: 13
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            opacity: small.enabled ? 1 : 0.45
        }
        background: Rectangle {
            radius: 7
            color: small.checked ? Theme.pRaised : small.down ? Theme.pPressed : small.hovered ? Theme.pHover : "transparent"
            border.color: small.checked ? Theme.accent : Theme.pLine
        }
    }
    PlayerPanel {
        id: tracksPanel
        objectName: "tracksPanel"
        visible: controlBar.openPanel === "tracks"
        readonly property bool subtitles: controlBar.tracksTab === "subtitles"
        readonly property bool style: controlBar.tracksTab === "style"
        function setStyle(key, value) {
            const st = session.subtitleStyle
            st[key] = value
            session.subtitleStyle = st
        }
        title: "Audio & subtitles"
        width: Math.min(365, controlBar.width - 24)
        maxHeight: controlBar.panelMaxHeight
        x: Math.max(12, Math.min(controlBar.width - width - 12, rightGroup.x + tracksButton.x + tracksButton.width / 2 - width / 2))
        y: controlBar.panelBottom - height

        Row {
            spacing: 7
            bottomPadding: 8
            leftPadding: 7
            SmallButton {
                objectName: "subtitlesTab"
                text: "Subtitles"
                checked: tracksPanel.subtitles
                onClicked: controlBar.tracksTab = "subtitles"
            }
            // How subtitles look, between the subtitle and audio tracks.
            SmallButton {
                objectName: "styleTab"
                text: "Style"
                checked: tracksPanel.style
                onClicked: controlBar.tracksTab = "style"
            }
            SmallButton {
                objectName: "audioTab"
                text: "Audio"
                checked: controlBar.tracksTab === "audio"
                onClicked: controlBar.tracksTab = "audio"
            }
        }
        PanelChoices {
            key: "subSize"
            visible: tracksPanel.style
            open: controlBar.openChoice === "subSize"
            onToggled: controlBar.openChoice = open ? "" : "subSize"
            label: "Size"
            options: Format.subtitleSizes
            current: Math.round(session.subtitleScale * 100) / 100
            onPicked: v => session.subtitleScale = v
        }
        PanelChoices {
            key: "subColor"
            visible: tracksPanel.style
            open: controlBar.openChoice === "subColor"
            onToggled: controlBar.openChoice = open ? "" : "subColor"
            label: "Color"
            options: Format.subtitleColors
            current: session.subtitleStyle.color.toUpperCase()
            onPicked: v => tracksPanel.setStyle("color", v)
        }
        PanelChoices {
            key: "subOutline"
            visible: tracksPanel.style
            open: controlBar.openChoice === "subOutline"
            onToggled: controlBar.openChoice = open ? "" : "subOutline"
            label: "Outline"
            options: Format.subtitleOutlines
            current: session.subtitleStyle.outline
            onPicked: v => tracksPanel.setStyle("outline", v)
        }
        PanelChoices {
            key: "subBackground"
            visible: tracksPanel.style
            open: controlBar.openChoice === "subBackground"
            onToggled: controlBar.openChoice = open ? "" : "subBackground"
            label: "Background"
            options: Format.subtitleBackgrounds
            current: session.subtitleStyle.background
            onPicked: v => tracksPanel.setStyle("background", v)
        }
        PanelChoices {
            key: "subFont"
            visible: tracksPanel.style
            open: controlBar.openChoice === "subFont"
            onToggled: controlBar.openChoice = open ? "" : "subFont"
            label: "Font"
            options: [["", "Default"]].concat(Qt.fontFamilies().map(f => [f, f]))
            current: session.subtitleStyle.font
            onPicked: v => tracksPanel.setStyle("font", v)
        }
        PanelRow {
            visible: tracksPanel.subtitles
            label: "Off"
            detail: "Hide subtitles"
            selected: session.subtitleTrack < 0
            onClicked: session.setSubtitleTrack(-1)
        }
        Repeater {
            model: tracksPanel.visible && !tracksPanel.style ? (tracksPanel.subtitles ? session.subtitleTracks : session.audioTracks) : []
            PanelRow {
                required property var modelData
                label: modelData.name || modelData.label
                detail: modelData.detail || ""
                selected: modelData.selected
                onClicked: tracksPanel.subtitles ? session.setSubtitleTrack(modelData.id) : session.setAudioTrack(modelData.id)
            }
        }
        Muted {
            visible: controlBar.tracksTab === "audio" && session.audioTracks.length === 0
            leftPadding: 10
            text: "No audio tracks."
        }
        PanelRow {
            visible: !tracksPanel.style
            objectName: tracksPanel.subtitles ? "loadSubtitle" : "loadAudio"
            label: tracksPanel.subtitles ? "Load subtitle file…" : "Load audio track…"
            onClicked: {
                controlBar.closePanel()
                if (tracksPanel.subtitles) controlBar.subtitleFileRequested()
                else controlBar.audioFileRequested()
            }
        }

        footer: Column {
            visible: tracksPanel.subtitles && session.supports("SubtitleDelay")
            height: visible ? implicitHeight : 0
            width: tracksPanel.width
            spacing: 6
            leftPadding: 14
            Rectangle { width: parent.width - 28; height: 1; color: Theme.pDivider }
            Text {
                textFormat: Text.PlainText
                topPadding: 4
                text: "Subtitle timing"
                color: Theme.pText
                font.pixelSize: 13
                font.weight: Font.DemiBold
            }
            Row {
                spacing: 7
                SmallButton { text: "Earlier"; onClicked: session.subtitleDelay = Math.round((session.subtitleDelay - 0.1) * 10) / 10 }
                Text {
                    textFormat: Text.PlainText
                    objectName: "subtitleDelayValue"
                    anchors.verticalCenter: parent.verticalCenter
                    width: 56
                    horizontalAlignment: Text.AlignHCenter
                    text: (session.subtitleDelay > 0 ? "+" : session.subtitleDelay < 0 ? "−" : "")
                          + Math.abs(session.subtitleDelay).toFixed(1) + " s"
                    color: Theme.pText
                    font.pixelSize: 13
                }
                SmallButton { text: "Later"; onClicked: session.subtitleDelay = Math.round((session.subtitleDelay + 0.1) * 10) / 10 }
                SmallButton { text: "Reset"; enabled: session.subtitleDelay !== 0; onClicked: session.subtitleDelay = 0 }
            }
        }
    }

    // Playback settings: each choice shows its value and opens its list in
    // place; a second click on it closes the list.
    PlayerPanel {
        id: settingsPanel
        objectName: "settingsPopup"
        visible: controlBar.openPanel === "settings"
        title: "Playback settings"
        width: Math.min(365, controlBar.width - 24)
        maxHeight: controlBar.panelMaxHeight
        x: Math.max(12, Math.min(controlBar.width - width - 12, rightGroup.x + gearButton.x + gearButton.width / 2 - width / 2))
        y: controlBar.panelBottom - height

        PanelChoices {
            key: "speed"
            open: controlBar.openChoice === "speed"
            onToggled: controlBar.openChoice = open ? "" : key
            label: "Speed"
            options: [0.5, 0.75, 1, 1.25, 1.5, 2].map(v => [v, v + "×"])
                     .concat([0.5, 0.75, 1, 1.25, 1.5, 2].indexOf(session.speed) < 0
                             ? [[session.speed, Math.round(session.speed * 100) / 100 + "×"]] : [])
            current: session.speed
            onPicked: v => session.speed = v
        }
        PanelChoices {
            key: "aspect"
            open: controlBar.openChoice === "aspect"
            onToggled: controlBar.openChoice = open ? "" : key
            label: "Aspect ratio"
            enabled: session.supports("AspectRatio")
            options: session.aspectRatios.map(r => [r, r === "" ? "Automatic" : r])
                     .concat(session.aspectRatios.indexOf(session.aspectRatio) < 0 ? [[session.aspectRatio, session.aspectRatio]] : [])
            current: session.aspectRatio
            onPicked: v => session.setAspectRatio(v)
        }
        PanelChoices {
            key: "crop"
            open: controlBar.openChoice === "crop"
            onToggled: controlBar.openChoice = open ? "" : key
            label: "Crop"
            enabled: session.supports("Crop")
            options: session.crops.map(r => [r, r === "" ? "None" : r])
            current: session.crop
            onPicked: v => session.setCrop(v)
        }
        PanelChoices {
            key: "upscaler"
            open: controlBar.openChoice === "upscaler"
            onToggled: controlBar.openChoice = open ? "" : key
            label: "Upscaling"
            visible: session.upscalers.length > 1
            options: session.upscalers.map(name => [name, Format.upscalerLabel(name)])
            current: session.upscaler
            onPicked: v => session.upscaler = v
        }
        PanelChoices {
            key: "upscaleTo"
            open: controlBar.openChoice === "upscaleTo"
            onToggled: controlBar.openChoice = open ? "" : key
            label: "Upscale to"
            visible: session.upscalers.length > 1
            enabled: session.upscaler !== "off"
            options: Format.upscaleTargets
            current: session.upscaleHeight
            onPicked: v => session.upscaleHeight = v
        }
        Rectangle { width: parent.width; height: 1; color: Theme.pDivider }
        PanelRow {
            label: "Reset picture"
            enabled: session.supports("AspectRatio") || session.supports("VideoEqualizer")
            onClicked: {
                if (session.supports("AspectRatio")) session.setAspectRatio("")
                if (session.supports("Crop")) session.setCrop("")
                if (session.supports("VideoEqualizer")) session.resetEqualizer()
            }
        }
        PanelRow {
            objectName: "diagnosticsItem"
            label: "Playback diagnostics"
            value: "I"
            selected: controlBar.statsShown
            onClicked: controlBar.statsRequested()
        }
        PanelRow {
            label: "Take screenshot"
            value: "S"
            enabled: session.supports("Screenshot")
            onClicked: session.screenshot()
        }
        PanelRow {
            objectName: "shortcutsItem"
            label: "Keyboard shortcuts"
            selected: controlBar.shortcutsShown
            onClicked: controlBar.shortcutsRequested()
        }
        PanelRow {
            label: "All settings"
            value: "↗"
            onClicked: { controlBar.closePanel(); controlBar.settingsRequested() }
        }
    }
}
