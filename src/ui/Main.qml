import QtQuick
import QtQuick.Window
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import CorporalJackie.Video

Window {
    id: root
    width: 1280
    height: 720
    minimumWidth: 640
    minimumHeight: 360
    visible: true
    color: "black"
    title: player.fileLoaded && player.title !== "" ? player.title + " — Corporal Jackie" : "Corporal Jackie"

    readonly property color textColor: "#f2f2f5"
    readonly property color mutedText: "#9a9aa6"
    readonly property color accent: "#e8b04b"
    property bool controlsVisible: true
    property bool statsVisible: false
    property string errorText: ""

    // Arrow-key seeking: a tap jumps 10 s. Holding keeps jumping every 250 ms,
    // further the longer it is held: 10 s steps, then 30 s, then 60 s.
    property int seekSteps: 0
    property real seekTotal: 0
    property real lastSeekAt: 0

    function arrowSeek(direction, isAutoRepeat) {
        if (!player.fileLoaded) return
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
        player.seekRelative(direction * step)
        seekToastTimer.restart()
    }

    function formatTime(seconds) {
        if (!isFinite(seconds) || seconds < 0) seconds = 0
        const s = Math.floor(seconds % 60), m = Math.floor(seconds / 60) % 60, h = Math.floor(seconds / 3600)
        const pad = n => (n < 10 ? "0" : "") + n
        return h > 0 ? h + ":" + pad(m) + ":" + pad(s) : m + ":" + pad(s)
    }

    component BarButton: ToolButton {
        id: barButton
        palette.buttonText: root.textColor
        font.pixelSize: 16
        implicitWidth: Math.max(36, implicitContentWidth + 16)
        implicitHeight: 36
        background: Rectangle {
            radius: 6
            color: barButton.down ? "#40ffffff" : barButton.hovered ? "#26ffffff" : "transparent"
        }
    }

    function toggleFullscreen() {
        root.visibility = root.visibility === Window.FullScreen ? Window.Windowed : Window.FullScreen
    }

    function showControls() {
        root.controlsVisible = true
        hideTimer.restart()
    }

    Connections {
        target: player
        function onErrorOccurred(error, message) {
            root.errorText = error === MpvPlayer.UnsupportedFile
                ? "This file isn't a video or audio format the player understands."
                : error === MpvPlayer.LoadFailed
                    ? "The file couldn't be opened. It may have moved, or it may be damaged."
                    : "Something went wrong inside the player (" + message + "). The log has details."
            errorTimer.restart()
        }
        function onFileLoadedChanged() { if (player.fileLoaded) root.errorText = "" }
    }

    Timer { id: errorTimer; interval: 8000; onTriggered: root.errorText = "" }
    Timer { id: seekToastTimer; interval: 900; onTriggered: root.seekTotal = 0 }

    Timer {
        id: hideTimer
        interval: 2500
        onTriggered: if (player.fileLoaded && !player.paused && !controlBar.hovered) root.controlsVisible = false
    }

    // The video. QML controls below are drawn on top of it.
    // "player" inside MpvVideo would mean its own property, so go through root.
    readonly property MpvPlayer appPlayer: player

    MpvVideo {
        id: video
        anchors.fill: parent
        player: root.appPlayer
    }

    FileDialog {
        id: openDialog
        title: "Open a video"
        nameFilters: ["Videos (*.mkv *.mp4 *.m4v *.avi *.mov *.webm *.ts *.m2ts *.wmv *.flv)", "All files (*)"]
        onAccepted: player.open(selectedFile)
    }

    // Keyboard shortcuts and mouse activity
    Item {
        id: inputLayer
        anchors.fill: parent
        focus: true

        Keys.onPressed: event => {
            switch (event.key) {
            case Qt.Key_Space: case Qt.Key_K: player.togglePause(); break
            case Qt.Key_Left:  root.arrowSeek(-1, event.isAutoRepeat); break
            case Qt.Key_Right: root.arrowSeek(1, event.isAutoRepeat); break
            case Qt.Key_Up:    player.volume = Math.min(100, player.volume + 5); break
            case Qt.Key_Down:  player.volume = Math.max(0, player.volume - 5); break
            case Qt.Key_M:     player.muted = !player.muted; break
            case Qt.Key_F:     root.toggleFullscreen(); break
            case Qt.Key_Escape: if (root.visibility === Window.FullScreen) root.visibility = Window.Windowed; break
            case Qt.Key_I:     root.statsVisible = !root.statsVisible; break
            case Qt.Key_H:     player.hwdecEnabled = !player.hwdecEnabled; break
            case Qt.Key_O:     openDialog.open(); break
            default: return
            }
            root.showControls()
            event.accepted = true
        }

        MouseArea {
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: root.controlsVisible || !player.fileLoaded ? Qt.ArrowCursor : Qt.BlankCursor
            onPositionChanged: root.showControls()
            onClicked: { inputLayer.forceActiveFocus(); if (player.fileLoaded) player.togglePause() }
            onDoubleClicked: root.toggleFullscreen()
        }

        DropArea {
            anchors.fill: parent
            onDropped: drop => { if (drop.hasUrls) player.open(drop.urls[0]) }
        }
    }

    // Empty state
    Column {
        anchors.centerIn: parent
        spacing: 12
        visible: !player.fileLoaded

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "Corporal Jackie"
            color: root.textColor
            font.pixelSize: 40
            font.weight: Font.DemiBold
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "Drop a video here, or open one"
            color: root.mutedText
            font.pixelSize: 16
        }
        Button {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "Open a video…"
            onClicked: openDialog.open()
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "Build " + appVersion
            color: "#5c5c66"
            font.pixelSize: 12
        }
    }

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
            text: "Decoder:    " + (player.hwdecCurrent !== "" ? player.hwdecCurrent + " (hardware)" : "software")
                  + "\nHW decode:  " + (player.hwdecEnabled ? "on" : "off") + "  (H to toggle)"
                  + "\nCodec:      " + (player.videoCodec || "-")
                  + "\nResolution: " + (player.videoWidth > 0 ? player.videoWidth + "×" + player.videoHeight : "-")
                  + "\nFPS:        " + (player.fps > 0 ? player.fps.toFixed(2) : "-")
                  + "\nDropped:    " + player.droppedFrames
        }
    }

    // Controls bar
    Rectangle {
        id: controlBar
        property bool hovered: barHover.hovered
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        height: 64
        visible: player.fileLoaded
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
                text: player.paused ? "▶" : "❚❚"
                onClicked: player.togglePause()
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: root.formatTime(seek.pressed ? seek.value : player.position) + " / " + root.formatTime(player.duration)
                color: root.textColor
                font.pixelSize: 13
            }
        }

        Slider {
            id: seek
            anchors { left: leftGroup.right; right: rightGroup.left; leftMargin: 12; rightMargin: 12; verticalCenter: parent.verticalCenter }
            from: 0
            to: Math.max(1, player.duration)
            value: pressed ? value : player.position
            onMoved: player.seek(value)
        }

        Row {
            id: rightGroup
            anchors { right: parent.right; rightMargin: 12; verticalCenter: parent.verticalCenter }
            spacing: 4
            BarButton {
                text: player.muted || player.volume === 0 ? "Muted" : "Vol"
                font.pixelSize: 13
                onClicked: player.muted = !player.muted
            }
            Slider {
                width: 100
                anchors.verticalCenter: parent.verticalCenter
                from: 0; to: 100
                value: player.volume
                onMoved: { player.volume = value; if (player.muted) player.muted = false }
            }
            BarButton {
                text: "Open"
                font.pixelSize: 13
                onClicked: openDialog.open()
            }
            BarButton {
                text: root.visibility === Window.FullScreen ? "Exit full screen" : "Full screen"
                font.pixelSize: 13
                onClicked: root.toggleFullscreen()
            }
        }
    }
}
