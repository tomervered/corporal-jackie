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

    Row {
        id: leftGroup
        anchors { left: parent.left; leftMargin: 12; verticalCenter: parent.verticalCenter }
        spacing: 8
        BarButton {
            text: session.paused || session.state === Playback.Ended ? "▶" : "❚❚"
            font.pixelSize: 16
            onClicked: session.togglePause()
        }
        // Next track: the video's next chapter (a song in a concert, a
        // scene in a film). Shown only when the video has chapters.
        BarButton {
            objectName: "nextTrack"
            visible: session.chapters.length > 1
            enabled: session.currentChapter < session.chapters.length - 1
            text: "⏭\uFE0E"
            font.pixelSize: 16
            ToolTip.visible: hovered
            ToolTip.text: "Next track"
            onClicked: session.nextChapter()
        }
        Text {
            textFormat: Text.PlainText
            anchors.verticalCenter: parent.verticalCenter
            text: Format.time(seek.pressed ? seek.value : session.position) + " / " + Format.time(session.duration)
            color: Theme.text
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
                color: Theme.accent
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
            objectName: "trackList"
            visible: session.chapters.length > 1
            text: "Tracks"
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
                MenuSeparator {}
                MenuItem { text: "Settings…"; onTriggered: controlBar.settingsRequested() }
                MenuItem { text: "Keyboard keys (mpv's)"; onTriggered: controlBar.shortcutsRequested() }
            }
        }
        BarButton {
            visible: browser.playingEpisodes.length > 0
            text: "Episodes"
            onClicked: controlBar.episodesRequested()
        }
        BarButton {
            text: "Browse"
            onClicked: controlBar.browseRequested()
        }
        BarButton {
            text: "Open"
            onClicked: controlBar.openRequested()
        }
        BarButton {
            text: controlBar.fullScreen ? "Exit full screen" : "Full screen"
            onClicked: controlBar.fullScreenRequested()
        }
        // Stops the video and lets go of it (the player's file, buffer and
        // stream), back to the pages. Q does the same.
        BarButton {
            objectName: "closeVideo"
            text: "Close"
            ToolTip.visible: hovered
            ToolTip.text: "Close the video (Q)"
            onClicked: session.stop()
        }
    }
}
