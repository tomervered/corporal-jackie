import QtQuick
import QtQuick.Controls.Basic
import CorporalJackie.Player   // VideoSurface, Session and the Playback enums, from C++
import "Format.js" as Format

// The bar under the video, one row of icons on a fade: play, next chapter,
// the time, seek (with chapter breaks and the A-B range), the chapter list,
// the speaker (its volume slider opens above it), subtitles and audio, episodes (for a series), browse,
// the Options menu, full screen and close. Every icon has a tooltip and
// says what it does to a screen reader.
Item {
    id: controlBar
    property bool shown: true
    property bool fullScreen: false
    // The pointer is on the bar or one of its menus is open: keep it showing.
    readonly property bool inUse: barHover.hovered || seek.pressed || moreMenu.visible || captionsMenu.visible || trackMenu.visible
                                  || volumeOpen
    signal openRequested()
    signal openLinkRequested()
    signal subtitleFileRequested()
    signal browseRequested()
    signal settingsRequested()
    signal shortcutsRequested()
    signal statsRequested()
    signal fullScreenRequested()
    signal episodesRequested()
    property bool statsShown: false
    property bool shortcutsShown: false

    // The volume slider: open while the pointer is on the speaker or the
    // slider (and a moment after it leaves, to cross the gap). A click on the
    // speaker pins it open; a second click closes it, even under the pointer.
    property bool volumePinned: false
    property bool volumeHoverClosed: false
    readonly property bool volumeHovered: speaker.hovered || volumePanelHover.hovered
    readonly property bool volumeOpen: shown && (volumePinned || volumeSlider.pressed
                                                 || ((volumeHovered || volumeLeaveDelay.running) && !volumeHoverClosed))
    onShownChanged: if (!shown) volumePinned = false
    function toggleVolume() {
        if (volumePinned) { volumePinned = false; volumeHoverClosed = true; volumeLeaveDelay.stop() }
        else { volumePinned = true; volumeHoverClosed = false }
    }
    function volumeHoverChanged() {
        if (volumeHovered) return
        volumeHoverClosed = false
        volumeLeaveDelay.restart()
    }
    onVolumeHoveredChanged: volumeHoverChanged()
    // One wheel notch: 5%.
    function nudgeVolume(delta) {
        if (delta === 0) return
        session.volume = Math.max(0, Math.min(session.maxVolume, Math.round(session.volume + (delta > 0 ? 5 : -5))))
        if (session.muted) session.muted = false
    }
    Timer { id: volumeLeaveDelay; interval: 350 }

    height: 84
    opacity: shown ? 1 : 0
    Behavior on opacity { NumberAnimation { duration: 200 } }
    HoverHandler { id: barHover }

    // The fade that keeps the icons readable over a bright picture.
    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0; color: "transparent" }
            GradientStop { position: 0.7; color: Qt.rgba(Theme.panel.r, Theme.panel.g, Theme.panel.b, 0.92) }
            GradientStop { position: 1; color: Qt.rgba(Theme.panel.r, Theme.panel.g, Theme.panel.b, 0.96) }
        }
    }
    // Clicks on the bar stay here (they would pause the video below).
    MouseArea { anchors.fill: parent; enabled: controlBar.shown; acceptedButtons: Qt.AllButtons }

    Row {
        id: leftGroup
        anchors { left: parent.left; leftMargin: 14; bottom: parent.bottom; bottomMargin: 10 }
        spacing: 2
        IconButton {
            objectName: "playPause"
            iconName: session.paused || session.state === Playback.Ended ? "play" : "pause"
            tip: session.paused || session.state === Playback.Ended ? "Play (Space)" : "Pause (Space)"
            Accessible.name: session.paused || session.state === Playback.Ended ? "Play" : "Pause"
            onClicked: session.togglePause()
        }
        // Next track: the video's next chapter (a song in a concert, a
        // scene in a film). Shown only when the video has chapters.
        IconButton {
            objectName: "nextTrack"
            visible: session.chapters.length > 1
            enabled: session.currentChapter < session.chapters.length - 1
            iconName: "next"
            iconSize: 18
            tip: "Next chapter"
            onClicked: session.nextChapter()
        }
        Text {
            textFormat: Text.PlainText
            anchors.verticalCenter: parent.verticalCenter
            leftPadding: 6
            text: Format.time(seek.pressed ? seek.value : session.position)
                  + (controlBar.width > 640 ? " / " + Format.time(session.duration) : "")
            color: Theme.text
            font.pixelSize: Theme.smallSize
            Accessible.name: "Played " + Format.time(session.position) + " of " + Format.time(session.duration)
        }
    }

    // --- Seek ------------------------------------------------------------------------

    Slider {
        id: seek
        objectName: "seekBar"
        anchors { left: leftGroup.right; right: rightGroup.left; leftMargin: 14; rightMargin: 10; verticalCenter: leftGroup.verticalCenter }
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
            height: seekHover.hovered || seek.pressed ? 5 : 3
            Behavior on height { NumberAnimation { duration: 100 } }
            Rectangle { anchors.fill: parent; radius: height / 2; color: Theme.line }
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
                color: Theme.text
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
                    color: Theme.panel
                }
            }
        }
        handle: Rectangle {
            x: seek.leftPadding + seek.visualPosition * seek.availableWidth - width / 2
            y: seek.topPadding + seek.availableHeight / 2 - height / 2
            width: seekHover.hovered || seek.pressed ? 14 : 10
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
            color: Theme.panel
            border.color: Theme.line
            Text {
                id: hoverText
                textFormat: Text.PlainText
                anchors.centerIn: parent
                text: {
                    const i = seek.chapterAt(seek.hoverTime)
                    const c = i >= 0 ? session.chapters[i] : null
                    return Format.time(seek.hoverTime) + (c ? "  ·  " + (c.title || "Chapter " + (i + 1)) : "")
                }
                color: Theme.text
                font.pixelSize: 12
            }
        }
    }

    // --- Right-hand icons ----------------------------------------------------------

    Row {
        id: rightGroup
        anchors { right: parent.right; rightMargin: 10; verticalCenter: leftGroup.verticalCenter }
        spacing: 2
        readonly property bool compact: controlBar.width < 720

        IconButton {
            objectName: "trackList"
            visible: session.chapters.length > 1
            iconName: "chapters"
            tip: "Chapters"
            active: trackMenu.visible
            onClicked: { controlBar.volumePinned = false; trackMenu.open() }

            // Every chapter with where it starts; the one playing is ticked.
            Menu {
                id: trackMenu
                y: -height - 6
                Repeater {
                    model: session.chapters
                    PlainMenuItem {
                        required property var modelData
                        required property int index
                        text: (index + 1) + ".  " + (modelData.title || "Chapter " + (index + 1))
                              + "    " + Format.time(modelData.start)
                        checkable: true
                        checked: index === session.currentChapter
                        onTriggered: session.goToChapter(index)
                    }
                }
            }
        }
        // The speaker: 0 to 3 waves for the level, the accent colour above
        // 100%. Hover or click opens the volume slider above it; a click
        // keeps it open until the next click.
        IconButton {
            id: speaker
            objectName: "volumeButton"
            readonly property real level: session.muted ? 0 : session.volume
            iconName: session.muted ? "muted"
                    : level <= 0 ? "volume0" : level <= 33 ? "volume1" : level <= 66 ? "volume2" : "volume3"
            iconColor: level > 100 ? Theme.accent : Theme.text
            tip: controlBar.volumeOpen ? "" : "Volume " + Math.round(session.volume) + "%" + (session.muted ? ", muted" : "")
            Accessible.name: "Volume " + Math.round(session.volume) + "%" + (session.muted ? ", muted" : "")
            onClicked: controlBar.toggleVolume()
            WheelHandler { onWheel: event => controlBar.nudgeVolume(event.angleDelta.y) }
        }
        IconButton {
            objectName: "captionsButton"
            iconName: "captions"
            tip: "Subtitles and audio"
            active: captionsMenu.visible || session.subtitleTrack >= 0
            onClicked: { controlBar.volumePinned = false; captionsMenu.open() }

            // Subtitles: the one showing is ticked, or Off. Then audio.
            Menu {
                id: captionsMenu
                objectName: "captionsMenu"
                y: -height - 6
                MenuItem { text: "Subtitles"; enabled: false }
                PlainMenuItem {
                    text: "Off"
                    checkable: true
                    checked: session.subtitleTrack < 0
                    onTriggered: session.setSubtitleTrack(-1)
                }
                Instantiator {
                    model: session.subtitleTracks
                    delegate: PlainMenuItem {
                        text: modelData.label
                        checkable: true
                        checked: modelData.selected
                        onTriggered: session.setSubtitleTrack(modelData.id)
                    }
                    onObjectAdded: (index, object) => captionsMenu.insertItem(index + 2, object)
                    onObjectRemoved: (index, object) => captionsMenu.removeItem(object)
                }
                MenuItem { text: "Load subtitle file…"; onTriggered: controlBar.subtitleFileRequested() }
                MenuSeparator {}
                MenuItem { text: "Audio"; enabled: false }
                Instantiator {
                    model: session.audioTracks
                    delegate: PlainMenuItem {
                        text: modelData.label
                        checkable: true
                        checked: modelData.selected
                        onTriggered: session.setAudioTrack(modelData.id)
                    }
                    onObjectAdded: (index, object) => captionsMenu.addItem(object)
                    onObjectRemoved: (index, object) => captionsMenu.removeItem(object)
                }
            }
        }
        IconButton {
            visible: browser.playingEpisodes.length > 0
            iconName: "episodes"
            tip: "Episodes"
            onClicked: controlBar.episodesRequested()
        }
        IconButton {
            objectName: "browseButton"
            iconName: "browse"
            tip: "Browse (B)"
            onClicked: controlBar.browseRequested()
        }
        IconButton {
            id: moreButton
            objectName: "optionsButton"
            iconName: "gear"
            tip: "Options"
            active: moreMenu.visible
            onClicked: { controlBar.volumePinned = false; moreMenu.open() }

            Menu {
                id: moreMenu
                y: -height - 6

                Menu {
                    title: "Speed (" + Math.round(session.speed * 100) / 100 + "×)"
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

                // Upscaling, without opening Settings: the same choices.
                Menu {
                    id: upscaleMenu
                    title: "Upscaling: " + Format.upscalerLabel(session.upscaler)
                    enabled: session.upscalers.length > 1
                    Instantiator {
                        model: session.upscalers
                        delegate: PlainMenuItem {
                            text: Format.upscalerLabel(modelData)
                            checkable: true
                            checked: session.upscaler === modelData
                            onTriggered: session.upscaler = modelData
                        }
                        onObjectAdded: (index, object) => upscaleMenu.insertItem(index, object)
                        onObjectRemoved: (index, object) => upscaleMenu.removeItem(object)
                    }
                }
                Menu {
                    id: upscaleToMenu
                    title: "Upscale to: " + (Format.upscaleTargets.find(t => t[0] === session.upscaleHeight) || [0, "?"])[1]
                    enabled: session.upscaler !== "off"
                    Instantiator {
                        model: Format.upscaleTargets
                        delegate: PlainMenuItem {
                            text: modelData[1]
                            checkable: true
                            checked: session.upscaleHeight === modelData[0]
                            onTriggered: session.upscaleHeight = modelData[0]
                        }
                        onObjectAdded: (index, object) => upscaleToMenu.insertItem(index, object)
                        onObjectRemoved: (index, object) => upscaleToMenu.removeItem(object)
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
                MenuItem {
                    objectName: "diagnosticsItem"
                    text: "Playback diagnostics (I)"
                    checkable: true
                    checked: controlBar.statsShown
                    onTriggered: controlBar.statsRequested()
                }
                MenuSeparator {}
                MenuItem { text: "Open a video…"; onTriggered: controlBar.openRequested() }
                MenuItem { text: "Open a link…"; onTriggered: controlBar.openLinkRequested() }
                MenuSeparator {}
                MenuItem { text: "Settings…"; onTriggered: controlBar.settingsRequested() }
                MenuItem {
                    objectName: "shortcutsItem"
                    text: "Keyboard shortcuts"
                    checkable: true
                    checked: controlBar.shortcutsShown
                    onTriggered: controlBar.shortcutsRequested()
                }
            }
        }
        IconButton {
            iconName: controlBar.fullScreen ? "windowed" : "fullscreen"
            tip: controlBar.fullScreen ? "Exit full screen (F)" : "Full screen (F)"
            onClicked: controlBar.fullScreenRequested()
        }
        // Stops the video and lets go of it (the player's file, buffer and
        // stream), back to the pages. Q does the same.
        IconButton {
            objectName: "closeVideo"
            iconName: "close"
            tip: "Close video (Q)"
            onClicked: session.stop()
        }
    }

    // --- Volume slider, above the speaker -------------------------------------

    Rectangle {
        id: volumePanel
        objectName: "volumePanel"
        visible: controlBar.volumeOpen
        width: 64
        height: 232
        x: Math.min(controlBar.width - width - 4, rightGroup.x + speaker.x + speaker.width / 2 - width / 2)
        y: rightGroup.y - height - 6
        radius: 12
        color: Theme.overlay
        border.color: Theme.line
        HoverHandler { id: volumePanelHover }
        // Clicks between the parts stay here (they would pause the video).
        MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons }
        WheelHandler { onWheel: event => controlBar.nudgeVolume(event.angleDelta.y) }

        Text {
            id: volumeValue
            textFormat: Text.PlainText
            anchors { top: parent.top; topMargin: 12; horizontalCenter: parent.horizontalCenter }
            text: Math.round(volumeSlider.pressed ? volumeSlider.value : session.volume) + "%"
            color: session.volume > 100 ? Theme.accent : Theme.text
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
            // A small catch at 100%, so the normal level is easy to find.
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
                Rectangle { anchors.fill: parent; radius: 2; color: Theme.line }
                Rectangle {
                    anchors.bottom: parent.bottom
                    width: parent.width
                    height: (1 - volumeSlider.visualPosition) * parent.height
                    radius: 2
                    color: session.muted ? Theme.muted : Theme.accent
                }
                // The 100% mark.
                Rectangle {
                    y: (1 - 100 / volumeSlider.to) * parent.height - height / 2
                    x: -4
                    width: 12
                    height: 2
                    color: Theme.text
                    opacity: 0.7
                }
                Text {
                    textFormat: Text.PlainText
                    x: 12
                    y: (1 - 100 / volumeSlider.to) * parent.height - height / 2
                    text: "100"
                    color: Theme.muted
                    font.pixelSize: 10
                }
            }
            handle: Rectangle {
                x: volumeSlider.leftPadding + volumeSlider.availableWidth / 2 - width / 2
                y: volumeSlider.topPadding + volumeSlider.visualPosition * (volumeSlider.availableHeight - height)
                width: volumeSlider.pressed ? 16 : 14
                height: width
                radius: width / 2
                color: Theme.text
                border.color: "#80000000"
            }
        }
        IconButton {
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
}
