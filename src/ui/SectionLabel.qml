import QtQuick

// The heading of a group in a list (a season, a quality).
Text {
    textFormat: Text.PlainText
    required property string section
    text: section
    color: Theme.link
    font.pixelSize: Theme.bodySize
    font.weight: Font.DemiBold
    topPadding: 14
    bottomPadding: 6
}
