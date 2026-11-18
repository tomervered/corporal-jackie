import QtQuick
import QtQuick.Window
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import CorporalJackie.Player   // VideoSurface, Session and the Playback enums, from C++
import "Format.js" as Format
import "Notices.js" as Notices

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
    property bool episodesVisible: false
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

    function toggleEpisodes() {
        root.episodesVisible = !root.episodesVisible && browser.playingEpisodes.length > 0
        if (root.episodesVisible) root.browseVisible = false
        else inputLayer.forceActiveFocus()
    }

    // Full screen lives in the Session, so mpv's F and Esc keys and the
    // window agree on it.
    function toggleFullscreen() { session.fullScreen = !session.fullScreen }
    onVisibilityChanged: {
        if (root.visibility === Window.FullScreen || root.visibility === Window.Windowed
                || root.visibility === Window.Maximized)
            session.fullScreen = root.visibility === Window.FullScreen
    }

    function showControls() {
        root.controlsVisible = true
        hideTimer.restart()
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
                    : error === Playback.TorrentsOff
                        ? Notices.torrentsOff
                        : "Something went wrong inside the player (" + message + "). The log has details."
            errorTimer.restart()
        }
        function onStateChanged() { if (session.hasMedia) root.errorText = "" }
        function onScreenshotSaved(path) { root.osd("Screenshot saved to Pictures › Corporal Jackie") }
        function onFullScreenChanged() {
            if ((root.visibility === Window.FullScreen) !== session.fullScreen)
                root.visibility = session.fullScreen ? Window.FullScreen : Window.Windowed
        }
        // mpv's I key: mpv's own stats need a script, so the app shows its own.
        function onAppActionRequested(action) { if (action === "stats") root.statsVisible = !root.statsVisible }
    }

    Timer { id: errorTimer; interval: 8000; onTriggered: root.errorText = "" }
    Timer { id: osdTimer; interval: 1600; onTriggered: root.osdText = "" }
    Timer {
        id: hideTimer
        interval: 2500
        onTriggered: if (session.hasMedia && !session.paused && !controlBar.inUse && !root.episodesVisible) root.controlsVisible = false
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

        // Every key goes to mpv, which runs its own bindings (its standard
        // input.conf: Space, arrows, F, M, J...). The app adds none here.
        // mpv repeats a held key itself, so Qt's repeats are dropped.
        Keys.onPressed: event => {
            if (!event.isAutoRepeat)
                session.keyEvent(event.key, event.modifiers, event.text, true)
            event.accepted = true
        }
        Keys.onReleased: event => {
            if (!event.isAutoRepeat)
                session.keyEvent(event.key, event.modifiers, event.text, false)
            event.accepted = true
        }
        // A key held while a panel opens is never released here: stop mpv
        // repeating it.
        onActiveFocusChanged: if (!activeFocus) session.keyEvent(0, 0, "", false)

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
            enabled: preferences.privacyChosen   // nothing opens before the first-run answer
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

    // --- Episodes of the series that is playing ---------------------------------

    EpisodesPanel {
        id: episodesPanel
        objectName: "episodesPanel"
        visible: root.episodesVisible && !root.pagesVisible && session.hasMedia
        anchors { top: parent.top; bottom: parent.bottom; right: parent.right; margins: 16 }
        // Above the control bar, which stays usable while it is open.
        anchors.bottomMargin: 72
        width: Math.min(440, parent.width - 32)
        onCloseRequested: root.toggleEpisodes()
        onStreamPicked: { root.episodesVisible = false; inputLayer.forceActiveFocus() }
    }
    // A movie, a file or a link has no episodes.
    Connections {
        target: browser
        function onPlayingShowChanged() { if (browser.playingEpisodes.length === 0) root.episodesVisible = false }
    }

    // --- Overlays ------------------------------------------------------------

    // Short messages, top centre
    Toast {
        anchors { top: parent.top; horizontalCenter: parent.horizontalCenter; topMargin: 24 }
        visible: root.osdText !== "" && root.errorText === ""
        text: root.osdText
        fontSize: 16
    }

    // What the upscaler is doing ("NVIDIA VSR on · 720p → 1440p"), top left:
    // for a few seconds each time that changes, and whenever the controls
    // show. Green: running; grey: idle or starting; amber: it could not run.
    Rectangle {
        id: upscalerBadge
        objectName: "upscalerBadge"
        anchors { top: parent.top; left: parent.left; margins: 16 }
        visible: session.hasMedia && session.upscalerStatus !== "" && !root.pagesVisible
                 && (upscalerBadgeTimer.running || root.controlsVisible)
        width: badgeRow.implicitWidth + 24
        height: badgeRow.implicitHeight + 12
        radius: height / 2
        color: "#a0000000"
        Row {
            id: badgeRow
            anchors.centerIn: parent
            spacing: 8
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: 10; height: 10; radius: 5
                color: session.upscalerProblem ? "#f0a030"
                     : session.upscalerStatus.indexOf(" on · ") >= 0 ? "#40c060" : "#909090"
            }
            Text {
                textFormat: Text.PlainText
                text: session.upscalerStatus
                color: Theme.text
                font.pixelSize: 14
            }
        }
        Timer { id: upscalerBadgeTimer; interval: 5000 }
        Connections {
            target: session
            function onUpscalerStatusChanged() { if (session.upscalerStatus !== "") upscalerBadgeTimer.restart() }
        }
    }

    // Buffering. A torrent that stalls mid-play shows here too, even before
    // mpv's own buffer runs out.
    Toast {
        anchors.centerIn: parent
        visible: session.hasMedia
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
            textFormat: Text.PlainText
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

    // mpv's keys, from the Options menu. A click anywhere closes it.
    MouseArea {
        anchors.fill: parent
        visible: root.helpVisible
        onClicked: { root.helpVisible = false; inputLayer.forceActiveFocus() }
        ShortcutsOverlay { anchors.centerIn: parent }
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
        onEpisodesRequested: root.toggleEpisodes()
    }

    // --- First run: what may be contacted -----------------------------------

    PrivacyScreen {
        id: privacyScreen
        anchors.fill: parent
        visible: !preferences.privacyChosen
        onVisibleChanged: if (visible) forceActiveFocus(); else inputLayer.forceActiveFocus()
        Component.onCompleted: if (visible) forceActiveFocus()
    }
}
