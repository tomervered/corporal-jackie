import QtQuick
import QtQuick.Window
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import CorporalJackie
import "Format.js" as Format

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
    title: (session.hasMedia || session.loading) && session.title !== "" ? session.title + " — Corporal Jackie" : "Corporal Jackie"

    property bool controlsVisible: true
    property bool statsVisible: false
    property bool helpVisible: false
    property bool browseVisible: false
    // The pages fill the window when nothing plays, and cover the video on B.
    readonly property bool pagesVisible: browseVisible || (!session.hasMedia && !session.loading)
    property bool settingsVisible: false
    property string errorText: ""
    property string osdText: ""
    // When the pages close by themselves (a video started from them), the
    // keys go to the player again.
    onPagesVisibleChanged: if (!pagesVisible && !settingsVisible) inputLayer.forceActiveFocus()

    // --- Helpers -----------------------------------------------------------

    // What a torrent is doing, in words, while the player waits for it.
    readonly property bool streaming: session.streamPhase !== Playback.Inactive
    readonly property string streamText: {
        const peers = session.streamPeers === 1 ? "1 peer" : session.streamPeers + " peers"
        switch (session.streamPhase) {
        case Playback.FindingPeers: return "Looking for peers…"
        case Playback.FetchingMetadata: return "Getting the file list from " + peers + "…"
        case Playback.Stalled:
            return session.streamPeers === 0 ? "Waiting for peers… still trying"
                 : "Waiting for the next part… " + peers + " connected, " + Format.rate(session.streamRate)
        case Playback.Buffering:
            return "Buffering " + session.streamPercent + "% · " + peers + " · " + Format.rate(session.streamRate)
        default:
            return "Buffering " + session.bufferPercent + "% · " + peers + " · " + Format.rate(session.streamRate)
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

    function osd(text) {
        root.osdText = text
        osdTimer.restart()
    }

    function toggleBrowse() {
        if (!session.hasMedia && !session.loading) { pagesView.goTop("home"); pagesView.show(); return }
        root.browseVisible = !root.browseVisible
        if (!root.browseVisible) inputLayer.forceActiveFocus()
    }

    function toggleSettings() {
        root.settingsVisible = !root.settingsVisible
        if (root.settingsVisible) settingsPanel.forceActiveFocus()
        else if (root.pagesVisible) pagesView.show()
        else inputLayer.forceActiveFocus()
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
        onTriggered: if (session.hasMedia && !session.paused && !controlBar.inUse) root.controlsVisible = false
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

    LinkPopup {
        id: linkPopup
        anchors.centerIn: parent
        width: Math.min(560, root.width - 48)
        onOpenRequested: text => { if (root.openText(text)) linkPopup.close() }
        onClosed: inputLayer.forceActiveFocus()
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
            case Qt.Key_B:     root.toggleBrowse(); break
            case Qt.Key_P:     root.toggleSettings(); break
            case Qt.Key_Escape:
                if (root.settingsVisible) root.toggleSettings()
                else if (root.browseVisible) root.toggleBrowse()
                else if (root.helpVisible) root.helpVisible = false
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
                if (shift) { session.audioDelay = session.audioDelay - 0.1; root.osd("Audio delay " + Format.signedMs(session.audioDelay)) }
                else { session.subtitleDelay = session.subtitleDelay - 0.1; root.osd("Subtitle delay " + Format.signedMs(session.subtitleDelay)) }
                break
            case Qt.Key_X:
                if (shift) { session.audioDelay = session.audioDelay + 0.1; root.osd("Audio delay " + Format.signedMs(session.audioDelay)) }
                else { session.subtitleDelay = session.subtitleDelay + 0.1; root.osd("Subtitle delay " + Format.signedMs(session.subtitleDelay)) }
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
                         : session.abLoopB < 0 ? "A-B repeat: start set at " + Format.time(session.abLoopA)
                         : "A-B repeat " + Format.time(session.abLoopA) + " – " + Format.time(session.abLoopB))
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

    // While a new item loads, the last frame of the old one may still be on
    // screen: dim it, and show only what the loading is doing.
    Rectangle {
        anchors.fill: parent
        visible: session.loading && !session.hasMedia
        color: "#c0000000"
    }
    Toast {
        anchors.centerIn: parent
        visible: session.loading && !session.hasMedia
        text: root.streaming ? root.streamText : "Opening…"
    }

    // --- Pages: home, discover, details, library... (press B while playing) -----

    Pages {
        id: pagesView
        objectName: "pages"
        visible: root.pagesVisible
        anchors.fill: parent
        // Above the control bar, which stays usable while they are open.
        anchors.bottomMargin: session.hasMedia ? 64 : 0
        onCloseRequested: { root.browseVisible = false; inputLayer.forceActiveFocus() }
        onSettingsRequested: root.toggleSettings()
        onOpenFileRequested: openDialog.open()
        onOpenLinkRequested: linkPopup.open()
        onMessage: (text, isError) => { if (isError) { root.errorText = text; errorTimer.restart() } else root.osd(text) }
    }

    // --- Settings (press P) ----------------------------------------------------

    SettingsPanel {
        id: settingsPanel
        visible: root.settingsVisible
        anchors { top: parent.top; bottom: parent.bottom; right: parent.right; margins: 16 }
        // Above the control bar, which stays usable while it is open.
        anchors.bottomMargin: session.hasMedia ? 72 : 16
        width: Math.min(520, parent.width - 32)
        onCloseRequested: { root.settingsVisible = false; inputLayer.forceActiveFocus() }
    }

    // --- Overlays ------------------------------------------------------------

    // Seek indicator, e.g. "+40 s", while tapping or holding an arrow key
    Toast {
        anchors.centerIn: parent
        visible: root.seekTotal !== 0
        text: (root.seekTotal > 0 ? "» +" : "« −") + Format.time(Math.abs(root.seekTotal))
        fontSize: 28
        bold: true
    }

    // Short messages for shortcuts, top centre
    Toast {
        anchors { top: parent.top; horizontalCenter: parent.horizontalCenter; topMargin: 24 }
        visible: root.osdText !== "" && root.errorText === ""
        text: root.osdText
        fontSize: 16
    }

    // Buffering. A torrent that stalls mid-play shows here too, even before
    // mpv's own buffer runs out.
    Toast {
        anchors.centerIn: parent
        visible: root.seekTotal === 0 && session.hasMedia
                 && (session.buffering || session.streamPhase === Playback.Stalled)
        text: root.streaming ? root.streamText : "Buffering… " + session.bufferPercent + "%"
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
            color: Theme.text
            font.pixelSize: 14
        }
    }

    StatsOverlay {
        anchors { top: parent.top; left: parent.left; margins: 12 }
        visible: root.statsVisible
        streaming: root.streaming
    }

    ShortcutsOverlay {
        anchors.centerIn: parent
        visible: root.helpVisible
    }

    // --- Controls bar ---------------------------------------------------------

    ControlBar {
        id: controlBar
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        visible: session.hasMedia
        shown: root.controlsVisible
        fullScreen: root.visibility === Window.FullScreen
        onOpenRequested: openDialog.open()
        onSubtitleFileRequested: subtitleDialog.open()
        onBrowseRequested: root.toggleBrowse()
        onSettingsRequested: root.toggleSettings()
        onShortcutsRequested: root.helpVisible = true
        onFullScreenRequested: root.toggleFullscreen()
    }
}
