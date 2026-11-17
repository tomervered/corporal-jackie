import QtQuick
import QtQuick.Controls.Basic

// The app's button: Basic style's, with rounded corners and a height that
// sits well beside a text field. `primary` fills it with the accent colour,
// for the one main action of a page or panel. Its label is plain text, like
// every label that can show outside words (see README).
Button {
    id: button
    property bool primary: false
    implicitHeight: 36
    implicitWidth: Math.max(64, implicitContentWidth + leftPadding + rightPadding)
    leftPadding: 14
    rightPadding: 14
    font.pixelSize: 13
    readonly property bool lit: primary || checked || highlighted
    contentItem: Text {
        textFormat: Text.PlainText
        text: button.text
        font: button.font
        color: !button.enabled ? Theme.faint : button.lit ? Theme.background : Theme.text
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
    background: Rectangle {
        radius: Theme.radius
        color: !button.enabled ? "#1e1e25"
             : button.lit ? (button.down ? Qt.darker(Theme.accent, 1.25) : button.hovered ? Qt.lighter(Theme.accent, 1.08) : Theme.accent)
             : button.down ? "#3c3c48" : button.hovered ? "#34343e" : "#2a2a33"
        border.width: button.visualFocus ? 2 : 0
        border.color: button.lit ? Theme.text : Theme.accent
    }
}
