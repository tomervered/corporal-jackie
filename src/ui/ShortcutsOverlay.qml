import QtQuick
import QtQuick.Controls.Basic

// mpv's own keys, which the video uses (the app adds none of its own), in
// the middle of the picture: what each does on the left, its keys on the
// right. The full list is mpv's input.conf. The × (or Playback settings ›
// Keyboard shortcuts again, or Esc) closes it.
Rectangle {
    id: sheet
    signal closeRequested()

    // [group, [[what it does, keys], ...]]
    readonly property var groups: [
        ["Playback", [
            ["Play / pause", "Space · P"],
            ["Seek 5 seconds", "← / →"],
            ["Seek 1 minute", "↑ / ↓"],
            ["Fine seek", "Shift + arrows"],
            ["Previous / next chapter", "PgDn / PgUp"],
            ["Slower / faster", "[ / ]"],
            ["Normal speed", "Backspace"],
            ["Half / double speed", "{ / }"],
            ["Frame forward / back", ". / ,"],
            ["Show position", "O"],
            ["A–B repeat", "L"],
            ["Loop file", "Shift + L"]]],
        ["Audio and subtitles", [
            ["Volume down / up", "9 / 0"],
            ["Mute", "M"],
            ["Next subtitle / audio track", "J / #"],
            ["Toggle subtitles", "V"],
            ["Earlier / later subtitles", "Z / X"],
            ["Move subtitles up / down", "R / T"],
            ["Subtitle size", "Shift + G / F"],
            ["Audio delay", "Ctrl + + / −"],
            ["Track list", "F9"]]],
        ["Picture and window", [
            ["Full screen", "F"],
            ["Close panel / leave full screen", "Esc"],
            ["Close video", "Q"],
            ["Aspect ratio", "Shift + A"],
            ["Zoom", "W / E"],
            ["Picture adjustments", "1–8"],
            ["Hardware decoding", "Ctrl + H"],
            ["Screenshot", "S"],
            ["Diagnostics", "I"]]]
    ]
    readonly property real room: parent ? parent.width : 900
    readonly property int columns: room > 1250 ? 3 : room > 720 ? 2 : 1

    width: Math.min(columns === 3 ? 1060 : columns === 2 ? 760 : 420, room - 28)
    height: Math.min(header.height + body.implicitHeight + 20, (parent ? parent.height : 600) - 110)
    radius: 15
    color: Theme.pPanel
    border.color: Qt.rgba(Theme.pText.r, Theme.pText.g, Theme.pText.b, 0.2)
    Accessible.role: Accessible.Dialog
    Accessible.name: "Keyboard shortcuts"
    // Clicks on the sheet stay here (they would pause the video).
    MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons }

    Item {
        id: header
        anchors { left: parent.left; right: parent.right; top: parent.top }
        height: 58
        Text {
            textFormat: Text.PlainText
            anchors { left: parent.left; leftMargin: 20; verticalCenter: parent.verticalCenter }
            text: "Keyboard shortcuts"
            color: Theme.pText
            font.pixelSize: Theme.headingSize
            font.weight: Font.DemiBold
        }
        IconButton {
            onPlayer: true
            objectName: "closeShortcuts"
            anchors { right: parent.right; rightMargin: 10; verticalCenter: parent.verticalCenter }
            iconName: "close"
            iconSize: 18
            tip: "Close keyboard shortcuts"
            onClicked: sheet.closeRequested()
        }
    }

    Flickable {
        id: flick
        anchors { left: parent.left; right: parent.right; top: header.bottom; bottom: parent.bottom; bottomMargin: 14 }
        contentHeight: body.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { policy: flick.contentHeight > flick.height ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff }

        Grid {
            id: body
            x: 22
            width: flick.width - 44 - (flick.contentHeight > flick.height ? 10 : 0)
            columns: sheet.columns
            columnSpacing: 32
            readonly property real columnWidth: (width - (columns - 1) * columnSpacing) / columns
            // Two columns: Playback on the left, the other two on the right.
            Repeater {
                model: sheet.columns === 3 ? [[sheet.groups[0]], [sheet.groups[1]], [sheet.groups[2]]]
                     : sheet.columns === 2 ? [[sheet.groups[0]], sheet.groups.slice(1)]
                     : [sheet.groups]
                Column {
                    required property var modelData
                    width: body.columnWidth
                    spacing: 12
                    Repeater {
                        model: parent.modelData
                        Column {
                            required property var modelData
                            width: body.columnWidth
                            Text {
                                textFormat: Text.PlainText
                                text: parent.modelData[0]
                                color: Theme.pMuted
                                font.pixelSize: Theme.smallSize
                                font.weight: Font.DemiBold
                                bottomPadding: 6
                            }
                            Repeater {
                                model: parent.modelData[1]
                                Item {
                                    id: keyRow
                                    required property var modelData
                                    width: body.columnWidth
                                    height: 32
                                    Text {
                                        textFormat: Text.PlainText
                                        anchors { left: parent.left; right: cap.left; rightMargin: 10; verticalCenter: parent.verticalCenter }
                                        text: keyRow.modelData[0]
                                        color: Theme.pText
                                        font.pixelSize: 14
                                        elide: Text.ElideRight
                                    }
                                    Rectangle {
                                        id: cap
                                        anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                                        width: capText.implicitWidth + 14
                                        height: 24
                                        radius: 5
                                        color: Theme.pCanvas
                                        border.color: Theme.pLine
                                        Text {
                                            id: capText
                                            textFormat: Text.PlainText
                                            anchors.centerIn: parent
                                            text: keyRow.modelData[1]
                                            color: Theme.pText
                                            font.family: "monospace"
                                            font.pixelSize: 12
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
