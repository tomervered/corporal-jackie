import QtQuick
import QtQuick.Controls.Basic
import CorporalJackie.Player   // VideoSurface, Session and the Playback enums, from C++
import "Format.js" as Format

// The bar under the video: play, time, seek (with chapters and the A-B
// range), volume, the Options menu, Browse, Open and full screen.
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
        Text {
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
                    MenuItem { text: "Load subtitle file…"; onTriggered: controlBar.subtitleFileRequested() }
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
                MenuItem { text: "Settings…"; onTriggered: controlBar.settingsRequested() }
                MenuItem { text: "Keyboard shortcuts"; onTriggered: controlBar.shortcutsRequested() }
            }
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
    }
}
