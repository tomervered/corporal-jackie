import QtQuick
import QtQuick.Controls.Basic

// mpv's own keys, which the video uses (the app adds none of its own), in
// the middle of the picture. The full list is mpv's input.conf. The × (or
// Options › Keyboard shortcuts again, or Esc) closes it.
Rectangle {
    id: sheet
    signal closeRequested()

    // [group, [[keys...], what it does], ...]
    readonly property var groups: [
        ["Playback", [
            [["Space", "P"], "Play / pause"],
            [["←", "→"], "Seek 5 s (hold to keep going)"],
            [["↑", "↓"], "Seek 1 minute"],
            [["Shift", "arrows"], "Seek exactly 1 s / 5 s"],
            [["[", "]"], "Slower / faster"],
            [["{", "}"], "Half / double speed"],
            [["Backspace"], "Normal speed"],
            [[".", ","], "Frame forward / back"],
            [["PgUp", "PgDn"], "Next / previous chapter"],
            [["L"], "A-B repeat"],
            [["Shift+L"], "Loop this file"],
            [["Q"], "Close the video"]]],
        ["Sound", [
            [["9", "0"], "Volume down / up"],
            [["/", "*"], "Volume down / up"],
            [["M"], "Mute"],
            [["#"], "Next audio track"],
            [["Ctrl +", "Ctrl −"], "Audio delay"]]],
        ["Subtitles", [
            [["V"], "Subtitles on / off"],
            [["J"], "Next subtitle track"],
            [["Z", "X"], "Subtitle delay − / +"],
            [["Shift+G", "Shift+F"], "Bigger / smaller"],
            [["R", "T"], "Up / down"]]],
        ["Picture and window", [
            [["F"], "Full screen"],
            [["Esc"], "Leave full screen"],
            [["Shift+A"], "Aspect ratio"],
            [["W", "E"], "Zoom to fill (pan and scan)"],
            [["1–8"], "Contrast, brightness, gamma, saturation"],
            [["Ctrl+H"], "Hardware decoding on / off"],
            [["S"], "Screenshot (Pictures › Corporal Jackie)"],
            [["O"], "Show position"],
            [["I"], "Playback diagnostics"],
            [["F9"], "List of audio and subtitle tracks"]]]
    ]
    readonly property int columns: parent && parent.width > 900 ? 2 : 1

    width: Math.min(columns === 2 ? 800 : 440, (parent ? parent.width : 800) - 32)
    height: Math.min(header.height + body.implicitHeight + 20, (parent ? parent.height : 600) - 110)
    radius: 15
    color: Theme.overlay
    border.color: Theme.line
    Accessible.role: Accessible.Dialog
    Accessible.name: "Keyboard shortcuts"
    // Clicks on the sheet stay here (they would pause the video).
    MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons }

    Item {
        id: header
        anchors { left: parent.left; right: parent.right; top: parent.top }
        height: 56
        Text {
            textFormat: Text.PlainText
            anchors { left: parent.left; leftMargin: 20; verticalCenter: parent.verticalCenter }
            text: "Keyboard shortcuts"
            color: Theme.text
            font.pixelSize: Theme.headingSize
            font.weight: Font.DemiBold
        }
        IconButton {
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
        anchors { left: parent.left; right: parent.right; top: header.bottom; bottom: parent.bottom; bottomMargin: 12 }
        contentHeight: body.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { policy: flick.contentHeight > flick.height ? ScrollBar.AlwaysOn : ScrollBar.AsNeeded }

        Grid {
            id: body
            x: 20
            width: flick.width - 40
            columns: sheet.columns
            columnSpacing: 32
            rowSpacing: 14
            readonly property real columnWidth: (width - (columns - 1) * columnSpacing) / columns
            // Two columns: Playback and Sound on the left, the rest on the right.
            Repeater {
                model: sheet.columns === 2 ? [sheet.groups.slice(0, 2), sheet.groups.slice(2)] : [sheet.groups]
                Column {
                    required property var modelData
                    width: body.columnWidth
                    spacing: 14
                    Repeater {
                        model: parent.modelData
                        Column {
                            required property var modelData
                            width: body.columnWidth
                            spacing: 2
                            Text {
                                textFormat: Text.PlainText
                                text: parent.modelData[0]
                                color: Theme.muted
                                font.pixelSize: Theme.smallSize
                                font.weight: Font.DemiBold
                                bottomPadding: 4
                            }
                            Repeater {
                                model: parent.modelData[1]
                                Item {
                                    id: keyRow
                                    required property var modelData
                                    width: body.columnWidth
                                    height: Math.max(26, what.implicitHeight + 6)
                                    Row {
                                        id: caps
                                        anchors.verticalCenter: parent.verticalCenter
                                        width: 132
                                        spacing: 4
                                        Repeater {
                                            model: keyRow.modelData[0]
                                            Rectangle {
                                                required property string modelData
                                                width: Math.max(24, cap.implicitWidth + 12)
                                                height: 22
                                                radius: 4
                                                color: Theme.raised
                                                border.color: Theme.divider
                                                Text {
                                                    id: cap
                                                    textFormat: Text.PlainText
                                                    anchors.centerIn: parent
                                                    text: parent.modelData
                                                    color: Theme.text
                                                    font.family: "monospace"
                                                    font.pixelSize: 12
                                                }
                                            }
                                        }
                                    }
                                    Text {
                                        id: what
                                        textFormat: Text.PlainText
                                        anchors { left: caps.right; leftMargin: 8; right: parent.right; verticalCenter: parent.verticalCenter }
                                        text: keyRow.modelData[1]
                                        color: Theme.text
                                        font.pixelSize: Theme.smallSize
                                        wrapMode: Text.WordWrap
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
