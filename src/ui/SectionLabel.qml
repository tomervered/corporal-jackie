import QtQuick

// The heading of a group in a list (a season, a quality).
Text {
    textFormat: Text.PlainText
    required property string section
    text: section
    color: Theme.accent
    font.pixelSize: 15
    font.weight: Font.DemiBold
    topPadding: 12
    bottomPadding: 4
}
