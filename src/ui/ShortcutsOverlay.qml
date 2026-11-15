import QtQuick

// mpv's own keys, which the video uses (the app adds none of its own).
// The full list is mpv's input.conf.
Rectangle {
    width: helpGrid.implicitWidth + 48
    height: helpGrid.implicitHeight + 48
    radius: 12
    color: Theme.overlay
    border.color: "#30ffffff"
    Grid {
        id: helpGrid
        anchors.centerIn: parent
        columns: 4
        columnSpacing: 16
        rowSpacing: 6
        Repeater {
            model: [
                "Space  P", "Play / pause",              "← →", "Seek 5 s (hold to keep going)",
                "↑ ↓", "Seek 1 minute",                  "Shift+arrows", "Seek exactly 1 s / 5 s",
                "9 0  / *", "Volume down / up",          "M", "Mute",
                "F", "Full screen",                      "Esc", "Leave full screen",
                "[ ]", "Slower / faster",                "{ }", "Half / double speed",
                "Backspace", "Normal speed",             ". ,", "Frame forward / back",
                "PgUp PgDn", "Next / previous chapter",  "! @", "Previous / next chapter",
                "J", "Next subtitle track",              "#", "Next audio track",
                "V", "Subtitles on / off",               "Z X", "Subtitle delay −/+",
                "Ctrl+ +/−", "Audio delay",              "Shift+G Shift+F", "Subtitles bigger / smaller",
                "R T", "Subtitles up / down",            "S", "Screenshot (Pictures › Corporal Jackie)",
                "L", "A-B repeat",                       "Shift+L", "Loop this file",
                "Shift+A", "Aspect ratio",               "W E", "Zoom to fill (pan and scan)",
                "1–8", "Contrast, brightness, gamma, saturation", "Ctrl+H", "Hardware decoding on/off",
                "O", "Show position",                    "I", "Stats",
                "F9", "List of audio and subtitle tracks", "Q", "Close the video",
            ]
            Text {
                textFormat: Text.PlainText
                required property string modelData
                required property int index
                text: modelData
                color: index % 2 === 0 ? Theme.accent : Theme.text
                font.pixelSize: 14
                font.weight: index % 2 === 0 ? Font.DemiBold : Font.Normal
            }
        }
    }
}
