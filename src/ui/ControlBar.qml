import QtQuick
import QtQuick.Controls.Basic
import CorporalJackie.Player   // VideoSurface, Session and the Playback enums, from C++
import "Format.js" as Format

// The bar under the video: play, time, seek (with chapters and the A-B
// range), volume, the Options menu, Episodes (for a series), Browse, Open
// and full screen.
Rectangle {
    id: controlBar
    property bool shown: true
    property bool fullScreen: false
    // The pointer is on the bar or its menu is open: keep it showing.
    readonly property bool inUse: barHover.hovered || moreMenu.visible
    signal openRequested()
    signal subtitleFileRequested()
    signal browseRequested()
    signal settingsRequested()
    signal shortcutsRequested()
    signal fullScreenRequested()
    signal episodesRequested()

    height: 64
    opacity: shown ? 1 : 0
    Behavior on opacity { NumberAnimation { duration: 200 } }
    gradient: Gradient {
        GradientStop { position: 0; color: "#00000000" }
        GradientStop { position: 1; color: "#d0000000" }
    }
    HoverHandler { id: barHover }

    // A thin bar with a round handle, for the seek and volume sliders.
    component BarSlider: Slider {
        id: barSlider
        focusPolicy: Qt.NoFocus
        readonly property bool active: pressed || sliderHover.hovered
        implicitHeight: 24
        HoverHandler { id: sliderHover }
        background: Rectangle {
            x: barSlider.leftPadding
            y: barSlider.topPadding + barSlider.availableHeight / 2 - height / 2
            width: barSlider.availableWidth
            height: barSlider.active ? 6 : 4
            radius: height / 2
            color: "#40ffffff"
            Rectangle {
                width: barSlider.visualPosition * parent.width
                height: parent.height
                radius: parent.radius
                color: Theme.accent
            }
        }
        handle: Rectangle {
            x: barSlider.leftPadding + barSlider.visualPosition * (barSlider.availableWidth - width)
            y: barSlider.topPadding + barSlider.availableHeight / 2 - height / 2
            width: barSlider.active ? 14 : 10
            height: width
            radius: width / 2
            color: Theme.text
        }
    }

    Row {
        id: leftGroup
        anchors { left: parent.left; leftMargin: 12; verticalCenter: parent.verticalCenter }
        spacing: 4
        BarButton {
            objectName: "playPause"
            iconName: session.paused || session.state === Playback.Ended ? "play" : "pause"
            tip: session.paused ? "Play (Space)" : "Pause (Space)"
            onClicked: session.togglePause()
        }
        // Next track: the video's next chapter (a song in a concert, a
        // scene in a film). Shown only when the video has chapters.
        BarButton {
            objectName: "nextTrack"
            visible: session.chapters.length > 1
            enabled: session.currentChapter < session.chapters.length - 1
            iconName: "next"
            tip: "Next track"
            onClicked: session.nextChapter()
        }
        BarButton {
            objectName: "mute"
            iconName: session.muted || session.volume === 0 ? "muted" : "volume"
            tip: session.muted ? "Unmute (M)" : "Mute (M)"
            onClicked: session.muted = !session.muted
        }
        BarSlider {
            objectName: "volume"
            width: 90
            anchors.verticalCenter: parent.verticalCenter
            from: 0; to: 100
            value: session.muted ? 0 : session.volume
            onMoved: { session.volume = value; if (session.muted) session.muted = false }
        }
        // As wide as the longest time it can show, so the seek bar beside
        // it does not jump as the digits change.
        Text {
            id: timeLabel
            textFormat: Text.PlainText
            anchors.verticalCenter: parent.verticalCenter
            width: timeMetrics.width + 16
            leftPadding: 8
            text: Format.time(seek.pressed ? seek.value : session.position) + " / " + Format.time(session.duration)
            color: Theme.text
            font.pixelSize: 13
            TextMetrics {
                id: timeMetrics
                font: timeLabel.font
                text: session.duration >= 3600 ? "00:00:00 / 00:00:00" : "00:00 / 00:00"
            }
        }
    }

    BarSlider {
        id: seek
        objectName: "seek"
        anchors { left: leftGroup.right; right: rightGroup.left; leftMargin: 8; rightMargin: 12; verticalCenter: parent.verticalCenter }
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
                color: Theme.text
                opacity: 0.7
            }
        }
        // A-B repeat range
        Rectangle {
            visible: session.abLoopA >= 0 && session.duration > 0
            x: seek.leftPadding + (session.abLoopA / Math.max(1, session.duration)) * seek.availableWidth
            width: session.abLoopB >= 0 ? ((session.abLoopB - session.abLoopA) / Math.max(1, session.duration)) * seek.availableWidth : 2
            y: seek.topPadding + seek.availableHeight / 2 + 6
            height: 3
            color: Theme.accent
        }
        // The time under the pointer, above the bar.
        HoverHandler { id: seekHover }
        Rectangle {
            readonly property real at: Math.max(0, Math.min(1, (seekHover.point.position.x - seek.leftPadding) / seek.availableWidth))
            visible: seekHover.hovered && session.duration > 0 && !seek.pressed
            x: Math.max(0, Math.min(seek.width - width, seekHover.point.position.x - width / 2))
            y: -height - 6
            width: hoverTime.implicitWidth + 12
            height: hoverTime.implicitHeight + 6
            radius: 4
            color: Theme.overlay
            Text {
                id: hoverTime
                textFormat: Text.PlainText
                anchors.centerIn: parent
                text: Format.time(parent.at * session.duration)
                color: Theme.text
                font.pixelSize: 12
            }
        }
    }

    Row {
        id: rightGroup
        anchors { right: parent.right; rightMargin: 12; verticalCenter: parent.verticalCenter }
        spacing: 4
        BarButton {
            objectName: "trackList"
            visible: session.chapters.length > 1
            text: "Tracks"
            tip: "Chapters of this video"
            onClicked: trackMenu.open()

            // Every chapter with where it starts; the one playing is ticked.
            Menu {
                id: trackMenu
                y: -height
                Repeater {
                    model: session.chapters
                    PlainMenuItem {
                        required property var modelData
                        required property int index
                        text: (index + 1) + ".  " + (modelData.title || "Track " + (index + 1))
                              + "    " + Format.time(modelData.start)
                        checkable: true
                        checked: index === session.currentChapter
                        onTriggered: session.goToChapter(index)
                    }
                }
            }
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
                        delegate: PlainMenuItem {
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
                        delegate: PlainMenuItem {
                            text: modelData.label
                            checkable: true
                            checked: modelData.selected
                            onTriggered: session.setSubtitleTrack(modelData.id)
                        }
                        onObjectAdded: (index, object) => subtitleMenu.insertItem(index + 1, object)
                        onObjectRemoved: (index, object) => subtitleMenu.removeItem(object)
                    }
                    MenuSeparator {}
                    MenuItem { text: "Load subtitle file…"; onTriggered: controlBar.subtitleFileRequested() }
                }

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
                MenuItem { text: "Settings…"; onTriggered: controlBar.settingsRequested() }
                MenuItem { text: "Keyboard keys (mpv's)"; onTriggered: controlBar.shortcutsRequested() }
            }
        }
        BarButton {
            visible: browser.playingEpisodes.length > 0
            text: "Episodes"
            tip: "Other episodes of this show"
            onClicked: controlBar.episodesRequested()
        }
        BarButton {
            text: "Browse"
            tip: "Home, Discover and your library (B)"
            onClicked: controlBar.browseRequested()
        }
        BarButton {
            text: "Open"
            tip: "Open a video file"
            onClicked: controlBar.openRequested()
        }
        BarButton {
            objectName: "fullScreen"
            iconName: controlBar.fullScreen ? "windowed" : "fullscreen"
            tip: controlBar.fullScreen ? "Leave full screen (Esc)" : "Full screen (F)"
            onClicked: controlBar.fullScreenRequested()
        }
    }
}
