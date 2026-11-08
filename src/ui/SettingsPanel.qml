import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs

// Settings (press P): picture and motion options, applied at once, and where
// torrents are stored, applied the next time the app opens. Binds to
// `session` for playback options and `preferences` for the rest.
Rectangle {
    id: panel

    property color textColor: "#f2f2f5"
    property color mutedText: "#9a9aa6"
    property color accent: "#e8b04b"
    signal closeRequested()

    color: "#141418"
    border.color: "#30ffffff"
    radius: 12

    readonly property bool picture: session.supports("VideoOutput")

    // Clicks and scrolls on the panel stay here, away from the video below.
    MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons; onWheel: wheel => wheel.accepted = true }
    Keys.onEscapePressed: panel.closeRequested()

    component Heading: Text {
        color: panel.textColor
        font.pixelSize: 16
        font.weight: Font.DemiBold
        topPadding: 8
    }
    component Note: Text {
        width: parent ? parent.width : implicitWidth
        color: panel.mutedText
        font.pixelSize: 12
        wrapMode: Text.WordWrap
    }
    // A switch with a title and one line saying what it does.
    component Toggle: Item {
        id: toggle
        property string title
        property string detail
        property bool checked
        property bool enabledHere: true
        signal toggled(bool on)
        width: parent ? parent.width : 400
        height: Math.max(sw.implicitHeight, texts.implicitHeight) + 8
        opacity: enabledHere ? 1 : 0.45
        Column {
            id: texts
            anchors { left: parent.left; right: sw.left; rightMargin: 12; verticalCenter: parent.verticalCenter }
            spacing: 2
            Text { width: parent.width; text: toggle.title; color: panel.textColor; font.pixelSize: 14; wrapMode: Text.WordWrap }
            Text { width: parent.width; text: toggle.detail; color: panel.mutedText; font.pixelSize: 12; wrapMode: Text.WordWrap; visible: text !== "" }
        }
        Switch {
            id: sw
            anchors { right: parent.right; verticalCenter: parent.verticalCenter }
            focusPolicy: Qt.NoFocus
            enabled: toggle.enabledHere
            checked: toggle.checked
            onToggled: toggle.toggled(checked)
        }
    }
    // A title, then a row of choices with one picked.
    component Choice: Column {
        id: choice
        property string title
        property var options: []      // [[value, label], ...]
        property var value
        signal picked(var value)
        width: parent ? parent.width : 400
        spacing: 6
        Text {
            text: choice.title
            color: panel.textColor
            font.pixelSize: 14
        }
        Row {
            spacing: 4
            Repeater {
                model: choice.options
                Button {
                    focusPolicy: Qt.NoFocus
                    text: modelData[1]
                    checkable: true
                    checked: choice.value === modelData[0]
                    onClicked: choice.picked(modelData[0])
                }
            }
        }
    }

    // --- Header ------------------------------------------------------------------
    Text {
        id: title
        anchors { left: parent.left; top: parent.top; margins: 16 }
        text: "Settings"
        color: panel.textColor
        font.pixelSize: 20
        font.weight: Font.DemiBold
    }
    Button {
        anchors { right: parent.right; top: parent.top; margins: 12 }
        text: "Close"
        focusPolicy: Qt.NoFocus
        onClicked: panel.closeRequested()
    }

    Flickable {
        id: flick
        anchors { left: parent.left; right: parent.right; top: title.bottom; bottom: parent.bottom; margins: 16 }
        contentHeight: body.implicitHeight
        clip: true
        ScrollBar.vertical: ScrollBar {}

        Column {
            id: body
            width: flick.width - 12
            spacing: 6

            // --- Picture and motion ---------------------------------------------
            Heading { text: "Picture and motion" }
            Note {
                visible: !panel.picture
                text: "These options are for videos playing on this computer."
            }
            Toggle {
                title: "Sync to the display"
                detail: "Times each frame to your screen's refresh, so motion is even. Turn off if playback gets worse."
                enabledHere: panel.picture
                checked: session.displaySync
                onToggled: on => session.displaySync = on
            }
            Toggle {
                title: "Smooth motion"
                detail: "Blends frames when the video's frame rate doesn't fit the screen's (a 24 fps film on a 60 Hz screen), so pans don't judder. Needs Sync to the display."
                enabledHere: panel.picture && session.displaySync
                checked: session.smoothMotion
                onToggled: on => session.smoothMotion = on
            }
            Toggle {
                title: "High-quality scaling"
                detail: "Sharper when the video is smaller than the screen, but much heavier on the graphics card. Can cause stutter."
                enabledHere: panel.picture
                checked: session.highQualityScaling
                onToggled: on => session.highQualityScaling = on
            }
            Toggle {
                title: "Hardware decoding"
                detail: "Lets the graphics card decode the video. Turn off only if the picture shows glitches."
                enabledHere: session.supports("HardwareDecoding")
                checked: session.hardwareDecoding
                onToggled: on => session.hardwareDecoding = on
            }
            Note { text: "Press I while playing to see dropped and late frames." }

            // --- Torrents --------------------------------------------------------
            Heading { text: "Torrent storage" }
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
                options: [[5, "5 GB"], [10, "10 GB"], [20, "20 GB"], [50, "50 GB"]]
                value: preferences.diskCacheGB
                onPicked: v => preferences.diskCacheGB = v
            }
            Note {
                visible: !preferences.torrentsInMemory
                text: "Videos you watch stay on disk up to this size, so watching again starts at once. The oldest go first."
            }
            Item {
                visible: !preferences.torrentsInMemory
                width: parent.width
                height: folderColumn.implicitHeight + 8
                Column {
                    id: folderColumn
                    anchors { left: parent.left; right: folderButtons.left; rightMargin: 12; verticalCenter: parent.verticalCenter }
                    spacing: 2
                    Text { text: "Cache folder"; color: panel.textColor; font.pixelSize: 14 }
                    Text {
                        width: parent.width
                        text: preferences.cacheFolder
                        color: panel.mutedText
                        font.pixelSize: 12
                        elide: Text.ElideMiddle
                    }
                }
                Row {
                    id: folderButtons
                    anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                    spacing: 4
                    Button { text: "Change…"; focusPolicy: Qt.NoFocus; onClicked: folderDialog.open() }
                    Button {
                        text: "Default"
                        focusPolicy: Qt.NoFocus
                        visible: preferences.customCacheFolder
                        onClicked: preferences.resetCacheFolder()
                    }
                }
            }
            Note {
                visible: !preferences.torrentsInMemory
                text: "Pick a folder on a hard disk to keep torrent writes off your SSD."
            }
            Rectangle {
                visible: preferences.restartNeeded
                width: parent.width
                height: restartNote.implicitHeight + 16
                radius: 8
                color: "#33e8b04b"
                border.color: panel.accent
                Text {
                    id: restartNote
                    anchors { fill: parent; margins: 8 }
                    text: "Torrent storage changes apply the next time you open Corporal Jackie."
                    color: panel.textColor
                    font.pixelSize: 13
                    wrapMode: Text.WordWrap
                }
            }
        }
    }

    FolderDialog {
        id: folderDialog
        title: "Choose where torrents are cached"
        onAccepted: preferences.setCacheFolder(selectedFolder)
    }
}
