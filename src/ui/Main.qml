import QtQuick
import QtQuick.Window
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import CorporalJackie

// The main window. It binds only to `session` (the Playback Session) and never
// to the player behind it, so a Cast target can replace it later.
Window {
    id: root
    width: 1280
    height: 720
    minimumWidth: 640
    minimumHeight: 360
    visible: true
    color: "black"
    title: session.hasMedia && session.title !== "" ? session.title + " — Corporal Jackie" : "Corporal Jackie"

    readonly property color textColor: "#f2f2f5"
    readonly property color mutedText: "#9a9aa6"
    readonly property color accent: "#e8b04b"
    readonly property color panel: "#e01a1a20"
    property bool controlsVisible: true
    property bool statsVisible: false
    property bool helpVisible: false
    property string errorText: ""
    property string osdText: ""

    // --- Helpers -----------------------------------------------------------

    function formatTime(seconds) {
        if (!isFinite(seconds) || seconds < 0) seconds = 0
        const s = Math.floor(seconds % 60), m = Math.floor(seconds / 60) % 60, h = Math.floor(seconds / 3600)
        const pad = n => (n < 10 ? "0" : "") + n
        return h > 0 ? h + ":" + pad(m) + ":" + pad(s) : m + ":" + pad(s)
    }

    function formatRate(bytesPerSecond) {
        const mb = bytesPerSecond / (1024 * 1024)
        return mb >= 1 ? mb.toFixed(1) + " MB/s" : Math.round(bytesPerSecond / 1024) + " KB/s"
    }

    // What a torrent is doing, in words, while the player waits for it.
    readonly property bool streaming: session.streamPhase !== Playback.Inactive
    readonly property string streamText: {
        const peers = session.streamPeers === 1 ? "1 peer" : session.streamPeers + " peers"
        switch (session.streamPhase) {
        case Playback.FindingPeers: return "Looking for peers…"
        case Playback.FetchingMetadata: return "Getting the file list from " + peers + "…"
        case Playback.Stalled: return "Waiting for peers… still trying (" + peers + ")"
        case Playback.Buffering:
            return "Buffering " + session.streamPercent + "% · " + peers + " · " + formatRate(session.streamRate)
        default:
            return "Buffering " + session.bufferPercent + "% · " + peers + " · " + formatRate(session.streamRate)
        }
    }

    // Opens what was pasted or typed: a magnet link, an info hash, a web link
    // or a file path. Returns false if it is empty.
    function openText(text) {
        text = (text || "").trim()
        if (text === "") return false
        session.openPath(text)
        return true
    }

    function signedMs(seconds) {
        const ms = Math.round(seconds * 1000)
        return (ms > 0 ? "+" : "") + ms + " ms"
    }

    function osd(text) {
        root.osdText = text
        osdTimer.restart()
    }

    function toggleFullscreen() {
        root.visibility = root.visibility === Window.FullScreen ? Window.Windowed : Window.FullScreen
    }

    function showControls() {
        root.controlsVisible = true
        hideTimer.restart()
    }

    function cycleTrack(tracks, current, allowOff) {
        const ids = tracks.map(t => t.id)
        if (allowOff) ids.push(-1)
        if (ids.length === 0) return undefined
        const i = ids.indexOf(current)
        return ids[(i + 1) % ids.length]
    }

    function trackLabel(tracks, id) {
        if (id < 0) return "Off"
        const t = tracks.find(t => t.id === id)
        return t ? t.label : "Track " + id
    }

    // Arrow-key seeking: a tap jumps 10 s. Holding keeps jumping every 250 ms,
    // further the longer it is held: 10 s steps, then 30 s, then 60 s.
    property int seekSteps: 0
    property real seekTotal: 0
    property real lastSeekAt: 0

    function arrowSeek(direction, isAutoRepeat) {
        if (!session.hasMedia) return
        if (!isAutoRepeat) {
            seekSteps = 0
            seekTotal = 0
        } else if (Date.now() - lastSeekAt < 250) {
            return
        }
        const step = seekSteps < 5 ? 10 : seekSteps < 12 ? 30 : 60
        seekSteps++
        lastSeekAt = Date.now()
        seekTotal += direction * step
        session.seekRelative(direction * step)
        seekToastTimer.restart()
    }

    // name: brightness, contrast, saturation, gamma, hue
    function nudgeEqualizer(name, delta) {
        if (!session.supports("VideoEqualizer")) return
        session.setEqualizer(name, session[name] + delta)
        osd(name.charAt(0).toUpperCase() + name.slice(1) + " " + session[name])
    }

    component BarButton: ToolButton {
        id: barButton
        palette.buttonText: root.textColor
        font.pixelSize: 13
        implicitWidth: Math.max(36, implicitContentWidth + 16)
        implicitHeight: 36
        background: Rectangle {
            radius: 6
            color: barButton.down ? "#40ffffff" : barButton.hovered ? "#26ffffff" : "transparent"
        }
    }

    // --- Session events ---------------------------------------------------

    Connections {
        target: session
        function onErrorOccurred(error, message) {
            root.errorText = error === Playback.UnsupportedFile
                ? "This file isn't a video or audio format the player understands."
                : error === Playback.LoadFailed
                    ? "The file couldn't be opened. It may have moved, or it may be damaged."
                    : error === Playback.TargetUnavailable
                        ? "The playback device stopped responding."
                    : error === Playback.NoPeers
                        ? "Nobody sharing this torrent was found. Try again later, or pick another source."
                    : error === Playback.StreamFailed
                        ? "The torrent stopped with an error (a full disk, maybe). The log has details."
                        : "Something went wrong inside the player (" + message + "). The log has details."
            errorTimer.restart()
        }
        function onStateChanged() { if (session.hasMedia) root.errorText = "" }
        function onScreenshotSaved(path) { root.osd("Screenshot saved to Pictures › Corporal Jackie") }
    }

    Timer { id: errorTimer; interval: 8000; onTriggered: root.errorText = "" }
    Timer { id: seekToastTimer; interval: 900; onTriggered: root.seekTotal = 0 }
    Timer { id: osdTimer; interval: 1600; onTriggered: root.osdText = "" }
    Timer {
        id: hideTimer
        interval: 2500
        onTriggered: if (session.hasMedia && !session.paused && !controlBar.hovered && !moreMenu.visible) root.controlsVisible = false
    }

    // --- Video --------------------------------------------------------------

    VideoSurface {
        anchors.fill: parent
        playbackSession: session  // not "session:", which would bind to itself
    }

    FileDialog {
        id: openDialog
        title: "Open a video"
        nameFilters: ["Videos (*.mkv *.mp4 *.m4v *.avi *.mov *.webm *.ts *.m2ts *.wmv *.flv)", "All files (*)"]
        onAccepted: session.open(selectedFile)
    }

    // "Open a link": a magnet link, an info hash or a web address
    Popup {
        id: linkPopup
        anchors.centerIn: parent
        width: Math.min(560, root.width - 48)
        modal: true
        focus: true
        padding: 20
        background: Rectangle { radius: 12; color: root.panel; border.color: "#30ffffff" }
        onOpened: { linkField.text = ""; linkField.paste(); linkField.selectAll(); linkField.forceActiveFocus() }
        onClosed: inputLayer.forceActiveFocus()
        Column {
            width: parent.width
            spacing: 12
            Text {
                text: "Open a link"
                color: root.textColor
                font.pixelSize: 18
                font.weight: Font.DemiBold
            }
            Text {
                width: parent.width
                wrapMode: Text.WordWrap
                text: "A magnet link, a torrent info hash, or a web address of a video."
                color: root.mutedText
                font.pixelSize: 13
            }
            TextField {
                id: linkField
                width: parent.width
                placeholderText: "magnet:?xt=urn:btih:…"
                onAccepted: if (root.openText(text)) linkPopup.close()
            }
            Row {
                anchors.right: parent.right
                spacing: 8
                Button { text: "Cancel"; onClicked: linkPopup.close() }
                Button { text: "Open"; enabled: linkField.text.trim() !== ""; onClicked: if (root.openText(linkField.text)) linkPopup.close() }
            }
        }
    }

    // Reads the clipboard: QML has no clipboard API, but a text field can paste.
    TextInput { id: clipboardReader; visible: false }
    function pasteAndOpen() {
        clipboardReader.text = ""
        clipboardReader.paste()
        if (!root.openText(clipboardReader.text)) linkPopup.open()
    }

    FileDialog {
        id: subtitleDialog
        title: "Load a subtitle file"
        nameFilters: ["Subtitles (*.srt *.ass *.ssa *.vtt *.sub *.sup)", "All files (*)"]
        onAccepted: session.addExternalSubtitle(selectedFile)
    }

    // --- Keyboard and mouse ------------------------------------------------

    Item {
        id: inputLayer
        anchors.fill: parent
        focus: true

        Keys.onPressed: event => {
            const shift = event.modifiers & Qt.ShiftModifier
            switch (event.key) {
            case Qt.Key_Space: case Qt.Key_K: session.togglePause(); break
            case Qt.Key_Left:  root.arrowSeek(-1, event.isAutoRepeat); break
            case Qt.Key_Right: root.arrowSeek(1, event.isAutoRepeat); break
            case Qt.Key_Up:    session.volume = Math.min(100, session.volume + 5); root.osd("Volume " + Math.round(session.volume)); break
            case Qt.Key_Down:  session.volume = Math.max(0, session.volume - 5); root.osd("Volume " + Math.round(session.volume)); break
            case Qt.Key_M:     session.muted = !session.muted; root.osd(session.muted ? "Muted" : "Sound on"); break
            case Qt.Key_F:     root.toggleFullscreen(); break
            case Qt.Key_Escape:
                if (root.helpVisible) root.helpVisible = false
                else if (root.visibility === Window.FullScreen) root.visibility = Window.Windowed
                break
            case Qt.Key_I:     root.statsVisible = !root.statsVisible; break
            case Qt.Key_H:
                session.hardwareDecoding = !session.hardwareDecoding
                root.osd("Hardware decoding " + (session.hardwareDecoding ? "on" : "off"))
                break
            case Qt.Key_O:     openDialog.open(); break
            case Qt.Key_U:     linkPopup.open(); break
            case Qt.Key_V:
                if (!(event.modifiers & Qt.ControlModifier)) return
                root.pasteAndOpen()
                break
            case Qt.Key_Question: case Qt.Key_Slash: root.helpVisible = !root.helpVisible; break
            // Speed
            case Qt.Key_BracketLeft:  session.speed = session.speed - 0.25; root.osd("Speed " + session.speed + "×"); break
            case Qt.Key_BracketRight: session.speed = session.speed + 0.25; root.osd("Speed " + session.speed + "×"); break
            case Qt.Key_Backspace:    session.speed = 1; root.osd("Speed 1×"); break
            // Tracks
            case Qt.Key_J: {
                const id = root.cycleTrack(session.subtitleTracks, session.subtitleTrack, true)
                if (id !== undefined) { session.setSubtitleTrack(id); root.osd("Subtitles: " + root.trackLabel(session.subtitleTracks, id)) }
                break
            }
            case Qt.Key_Y: {
                const id = root.cycleTrack(session.audioTracks, session.audioTrack, false)
                if (id !== undefined) { session.setAudioTrack(id); root.osd("Audio: " + root.trackLabel(session.audioTracks, id)) }
                break
            }
            // Subtitle and audio sync (Shift = audio)
            case Qt.Key_Z:
                if (shift) { session.audioDelay = session.audioDelay - 0.1; root.osd("Audio delay " + root.signedMs(session.audioDelay)) }
                else { session.subtitleDelay = session.subtitleDelay - 0.1; root.osd("Subtitle delay " + root.signedMs(session.subtitleDelay)) }
                break
            case Qt.Key_X:
                if (shift) { session.audioDelay = session.audioDelay + 0.1; root.osd("Audio delay " + root.signedMs(session.audioDelay)) }
                else { session.subtitleDelay = session.subtitleDelay + 0.1; root.osd("Subtitle delay " + root.signedMs(session.subtitleDelay)) }
                break
            case Qt.Key_G:
                session.subtitleScale = session.subtitleScale + (shift ? -0.1 : 0.1)
                root.osd("Subtitle size " + Math.round(session.subtitleScale * 100) + "%")
                break
            // Chapters, frames, screenshots, A-B
            case Qt.Key_PageDown: session.nextChapter(); break
            case Qt.Key_PageUp:   session.previousChapter(); break
            case Qt.Key_Period:   session.frameStep(true); break
            case Qt.Key_Comma:    session.frameStep(false); break
            case Qt.Key_S:        session.screenshot(); break
            case Qt.Key_L:
                session.toggleAbLoop()
                root.osd(session.abLoopA < 0 ? "A-B repeat off"
                         : session.abLoopB < 0 ? "A-B repeat: start set at " + root.formatTime(session.abLoopA)
                         : "A-B repeat " + root.formatTime(session.abLoopA) + " – " + root.formatTime(session.abLoopB))
                break
            // Picture
            case Qt.Key_A: session.cycleAspectRatio(); root.osd("Aspect: " + (session.aspectRatio || "automatic")); break
            case Qt.Key_C: session.cycleCrop(); root.osd("Crop: " + (session.crop || "none")); break
            case Qt.Key_1: root.nudgeEqualizer("contrast", -5); break
            case Qt.Key_2: root.nudgeEqualizer("contrast", 5); break
            case Qt.Key_3: root.nudgeEqualizer("brightness", -5); break
            case Qt.Key_4: root.nudgeEqualizer("brightness", 5); break
            case Qt.Key_5: root.nudgeEqualizer("gamma", -5); break
            case Qt.Key_6: root.nudgeEqualizer("gamma", 5); break
            case Qt.Key_7: root.nudgeEqualizer("saturation", -5); break
            case Qt.Key_8: root.nudgeEqualizer("saturation", 5); break
            case Qt.Key_9: root.nudgeEqualizer("hue", -5); break
            case Qt.Key_0: root.nudgeEqualizer("hue", 5); break
            case Qt.Key_R: session.resetEqualizer(); root.osd("Picture reset"); break
            default: return
            }
            root.showControls()
            event.accepted = true
        }

        MouseArea {
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: root.controlsVisible || !session.hasMedia ? Qt.ArrowCursor : Qt.BlankCursor
            onPositionChanged: root.showControls()
            onClicked: { inputLayer.forceActiveFocus(); if (session.hasMedia) session.togglePause() }
            onDoubleClicked: root.toggleFullscreen()
        }

        DropArea {
            anchors.fill: parent
            onDropped: drop => {
                // A magnet link dragged from a browser arrives as text.
                if (!drop.hasUrls) { if (drop.hasText) root.openText(drop.text); return }
                const url = drop.urls[0].toString()
                if (/^magnet:/i.test(url)) { root.openText(url); return }
                // A subtitle file dropped on a playing video is added to it.
                if (session.hasMedia && /\.(srt|ass|ssa|vtt|sub|sup)$/i.test(url)) session.addExternalSubtitle(drop.urls[0])
                else session.open(drop.urls[0])
            }
        }
    }

    // --- Empty state -------------------------------------------------------

    Column {
        anchors.centerIn: parent
        spacing: 12
        visible: !session.hasMedia

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "Corporal Jackie"
            color: root.textColor
            font.pixelSize: 40
            font.weight: Font.DemiBold
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: session.loading ? (root.streaming ? root.streamText : "Opening…")
                                  : "Drop a video or a magnet link here, or open one"
            color: root.mutedText
            font.pixelSize: 16
        }
        Row {
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 8
            Button { text: "Open a video…"; onClicked: openDialog.open() }
            Button { text: "Open a link…"; onClicked: linkPopup.open() }
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "Press ? for keyboard shortcuts · Build " + appVersion
            color: "#5c5c66"
            font.pixelSize: 12
        }
    }

    // --- Overlays ------------------------------------------------------------

    // Seek indicator, e.g. "+40 s", while tapping or holding an arrow key
    Rectangle {
        anchors.centerIn: parent
        visible: root.seekTotal !== 0
        width: seekLabel.implicitWidth + 40
        height: seekLabel.implicitHeight + 20
        radius: 10
        color: "#a0000000"
        Text {
            id: seekLabel
            anchors.centerIn: parent
            text: (root.seekTotal > 0 ? "» +" : "« −") + root.formatTime(Math.abs(root.seekTotal))
            color: root.textColor
            font.pixelSize: 28
            font.weight: Font.DemiBold
        }
    }

    // Short messages for shortcuts, top centre
    Rectangle {
        anchors { top: parent.top; horizontalCenter: parent.horizontalCenter; topMargin: 24 }
        visible: root.osdText !== "" && root.errorText === ""
        width: osdLabel.implicitWidth + 28
        height: osdLabel.implicitHeight + 14
        radius: 8
        color: "#b0000000"
        Text {
            id: osdLabel
            anchors.centerIn: parent
            text: root.osdText
            color: root.textColor
            font.pixelSize: 16
        }
    }

    // Buffering
    Rectangle {
        anchors.centerIn: parent
        // A torrent that stalls mid-play shows here too, even before mpv's own
        // buffer runs out.
        visible: root.seekTotal === 0 && session.hasMedia
                 && (session.buffering || session.streamPhase === Playback.Stalled)
        width: bufferLabel.implicitWidth + 32
        height: bufferLabel.implicitHeight + 18
        radius: 10
        color: "#a0000000"
        Text {
            id: bufferLabel
            anchors.centerIn: parent
            text: root.streaming ? root.streamText : "Buffering… " + session.bufferPercent + "%"
            color: root.textColor
            font.pixelSize: 18
        }
    }

    // Error banner
    Rectangle {
        anchors { top: parent.top; horizontalCenter: parent.horizontalCenter; topMargin: 16 }
        visible: root.errorText !== ""
        width: Math.min(parent.width - 32, errorLabel.implicitWidth + 32)
        height: errorLabel.implicitHeight + 20
        radius: 8
        color: "#cc3b2a2a"
        border.color: "#e06c5a"
        Text {
            id: errorLabel
            anchors.centerIn: parent
            width: Math.min(implicitWidth, root.width - 64)
            wrapMode: Text.WordWrap
            text: root.errorText
            color: root.textColor
            font.pixelSize: 14
        }
    }

    // Debug overlay (press I)
    Rectangle {
        anchors { top: parent.top; left: parent.left; margins: 12 }
        visible: root.statsVisible
        width: stats.implicitWidth + 24
        height: stats.implicitHeight + 16
        radius: 6
        color: "#b0000000"
        Text {
            id: stats
            anchors.centerIn: parent
            color: root.textColor
            font.family: "monospace"
            font.pixelSize: 13
            text: {
                let lines = ["Playing on: " + session.targetName]
                for (const key in session.stats)
                    lines.push(key + ": " + session.stats[key])
                lines.push("Speed: " + session.speed + "×")
                if (root.streaming) lines.push("Torrent: " + session.streamPeers + " peers, " + root.formatRate(session.streamRate)
                                               + ", " + session.streamBufferedSeconds.toFixed(1) + " s buffered ahead")
                if (session.subtitleDelay !== 0) lines.push("Subtitle delay: " + root.signedMs(session.subtitleDelay))
                if (session.audioDelay !== 0) lines.push("Audio delay: " + root.signedMs(session.audioDelay))
                return lines.join("\n")
            }
        }
    }

    // Keyboard shortcuts (press ?)
    Rectangle {
        anchors.centerIn: parent
        visible: root.helpVisible
        width: helpGrid.implicitWidth + 48
        height: helpGrid.implicitHeight + 48
        radius: 12
        color: root.panel
        border.color: "#30ffffff"
        Grid {
            id: helpGrid
            anchors.centerIn: parent
            columns: 4
            columnSpacing: 16
            rowSpacing: 6
            Repeater {
                model: [
                    "Space", "Play / pause",               "← →", "Seek (hold to go faster)",
                    "↑ ↓", "Volume",                        "M", "Mute",
                    "F", "Full screen",                     "O", "Open a file",
                    "U", "Open a link (magnet, web)",       "Ctrl+V", "Open the copied link",
                    "[ ]", "Slower / faster",               "Backspace", "Normal speed",
                    "J", "Next subtitle track",             "Y", "Next audio track",
                    "Z X", "Subtitle delay −/+",            "Shift+Z X", "Audio delay −/+",
                    "G", "Bigger subtitles (Shift: smaller)", "PgUp PgDn", "Previous / next chapter",
                    ", .", "Frame back / forward",          "S", "Screenshot",
                    "L", "A-B repeat",                      "A", "Aspect ratio",
                    "C", "Crop",                            "1–0", "Contrast, brightness, gamma, saturation, hue",
                    "R", "Reset picture",                   "I", "Debug overlay",
                    "H", "Hardware decoding on/off",        "Esc", "Close / leave full screen",
                ]
                Text {
                    text: modelData
                    color: index % 2 === 0 ? root.accent : root.textColor
                    font.pixelSize: 14
                    font.weight: index % 2 === 0 ? Font.DemiBold : Font.Normal
                }
            }
        }
    }

    // --- Controls bar ---------------------------------------------------------

    Rectangle {
        id: controlBar
        property bool hovered: barHover.hovered
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        height: 64
        visible: session.hasMedia
        opacity: root.controlsVisible ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: 200 } }
        gradient: Gradient {
            GradientStop { position: 0; color: "#00000000" }
            GradientStop { position: 1; color: "#d0000000" }
        }
        HoverHandler { id: barHover }

        Row {
            id: leftGroup
            anchors { left: parent.left; leftMargin: 12; verticalCenter: parent.verticalCenter }
            spacing: 8
            BarButton {
                text: session.paused || session.state === Playback.Ended ? "▶" : "❚❚"
                font.pixelSize: 16
                onClicked: session.togglePause()
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: root.formatTime(seek.pressed ? seek.value : session.position) + " / " + root.formatTime(session.duration)
                color: root.textColor
                font.pixelSize: 13
            }
        }

        Slider {
            id: seek
            anchors { left: leftGroup.right; right: rightGroup.left; leftMargin: 12; rightMargin: 12; verticalCenter: parent.verticalCenter }
            from: 0
            to: Math.max(1, session.duration)
            value: pressed ? value : session.position
            onMoved: session.seek(value)

            // Chapter marks
            Repeater {
                model: session.chapters
                Rectangle {
                    visible: modelData.start > 0 && session.duration > 0
                    x: seek.leftPadding + (modelData.start / session.duration) * seek.availableWidth - 1
                    y: seek.topPadding + seek.availableHeight / 2 - 5
                    width: 2; height: 10
                    color: root.accent
                }
            }
            // A-B repeat range
            Rectangle {
                visible: session.abLoopA >= 0 && session.duration > 0
                x: seek.leftPadding + (session.abLoopA / Math.max(1, session.duration)) * seek.availableWidth
                width: session.abLoopB >= 0 ? ((session.abLoopB - session.abLoopA) / Math.max(1, session.duration)) * seek.availableWidth : 2
                y: seek.topPadding + seek.availableHeight / 2 + 6
                height: 3
                color: root.accent
            }
        }

        Row {
            id: rightGroup
            anchors { right: parent.right; rightMargin: 12; verticalCenter: parent.verticalCenter }
            spacing: 4
            BarButton {
                text: session.muted || session.volume === 0 ? "Muted" : "Vol"
                onClicked: session.muted = !session.muted
            }
            Slider {
                width: 100
                anchors.verticalCenter: parent.verticalCenter
                from: 0; to: 100
                value: session.volume
                onMoved: { session.volume = value; if (session.muted) session.muted = false }
            }
            BarButton {
                id: moreButton
                text: "Options"
                onClicked: moreMenu.open()

                Menu {
                    id: moreMenu
                    y: -height

                    Menu {
                        id: audioMenu
                        title: "Audio track"
                        enabled: session.audioTracks.length > 0
                        Instantiator {
                            model: session.audioTracks
                            delegate: MenuItem {
                                text: modelData.label
                                checkable: true
                                checked: modelData.selected
                                onTriggered: session.setAudioTrack(modelData.id)
                            }
                            onObjectAdded: (index, object) => audioMenu.insertItem(index, object)
                            onObjectRemoved: (index, object) => audioMenu.removeItem(object)
                        }
                    }

                    Menu {
                        id: subtitleMenu
                        title: "Subtitles"
                        MenuItem {
                            text: "Off"
                            checkable: true
                            checked: session.subtitleTrack < 0
                            onTriggered: session.setSubtitleTrack(-1)
                        }
                        Instantiator {
                            model: session.subtitleTracks
                            delegate: MenuItem {
                                text: modelData.label
                                checkable: true
                                checked: modelData.selected
                                onTriggered: session.setSubtitleTrack(modelData.id)
                            }
                            onObjectAdded: (index, object) => subtitleMenu.insertItem(index + 1, object)
                            onObjectRemoved: (index, object) => subtitleMenu.removeItem(object)
                        }
                        MenuSeparator {}
                        MenuItem { text: "Load subtitle file…"; onTriggered: subtitleDialog.open() }
                    }

                    Menu {
                        title: "Speed (" + session.speed + "×)"
                        Repeater {
                            model: [0.5, 0.75, 1, 1.25, 1.5, 2]
                            MenuItem {
                                text: modelData + "×"
                                checkable: true
                                checked: session.speed === modelData
                                onTriggered: session.speed = modelData
                            }
                        }
                    }

                    MenuSeparator {}
                    MenuItem {
                        text: "Aspect ratio: " + (session.aspectRatio || "automatic")
                        enabled: session.supports("AspectRatio")
                        onTriggered: session.cycleAspectRatio()
                    }
                    MenuItem {
                        text: "Crop: " + (session.crop || "none")
                        enabled: session.supports("Crop")
                        onTriggered: session.cycleCrop()
                    }
                    MenuItem {
                        text: "Reset picture"
                        enabled: session.supports("VideoEqualizer")
                        onTriggered: session.resetEqualizer()
                    }
                    MenuItem {
                        text: "Take screenshot"
                        enabled: session.supports("Screenshot")
                        onTriggered: session.screenshot()
                    }
                    MenuSeparator {}
                    MenuItem { text: "Keyboard shortcuts"; onTriggered: root.helpVisible = true }
                }
            }
            BarButton {
                text: "Open"
                onClicked: openDialog.open()
            }
            BarButton {
                text: root.visibility === Window.FullScreen ? "Exit full screen" : "Full screen"
                onClicked: root.toggleFullscreen()
            }
        }
    }
}
