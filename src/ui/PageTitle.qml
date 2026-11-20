import QtQuick
import QtQuick.Controls.Basic

// The title at the top of a page.
Text {
    textFormat: Text.PlainText
    color: Theme.text
    font.pixelSize: Theme.pageTitleSize
    font.weight: Font.Bold
    font.letterSpacing: -0.6
    elide: Text.ElideRight
}
