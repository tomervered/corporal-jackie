import QtQuick

// The keyboard shortcuts (press ?).
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
                "Space", "Play / pause",               "← →", "Seek (hold to go faster)",
                "↑ ↓", "Volume",                        "M", "Mute",
                "F", "Full screen",                     "O", "Open a file",
                "U", "Open a link (magnet, web)",       "Ctrl+V", "Open the copied link",
                "[ ]", "Slower / faster",               "Backspace", "Normal speed",
                "J", "Next subtitle track",             "Y", "Next audio track",
                "Z X", "Subtitle delay −/+",            "Shift+Z X", "Audio delay −/+",
                "G", "Bigger subtitles (Shift: smaller)", "PgUp PgDn", "Previous / next chapter",
                ", .", "Frame back / forward",          "S", "Screenshot",
                "L", "A-B repeat",                      "A", "Aspect ratio",
                "C", "Crop",                            "1–0", "Contrast, brightness, gamma, saturation, hue",
                "R", "Reset picture",                   "I", "Debug overlay",
                "H", "Hardware decoding on/off",        "Esc", "Close / leave full screen",
                "B", "Browse add-ons and your folders", "P", "Settings",
            ]
            Text {
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
