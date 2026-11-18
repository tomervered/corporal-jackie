import QtQuick
import QtQuick.Controls.Basic

// A choice in a row of choices, such as Movies or Series: a rounded pill,
// outlined in the accent when picked.
Button {
    id: chip
    property bool selected: false
    checkable: true
    checked: selected
    // A click flips `checked` and drops the binding; put it back so the
    // chip always shows `selected`.
    onToggled: checked = Qt.binding(() => chip.selected)
    focusPolicy: Qt.StrongFocus
    font.pixelSize: Theme.bodySize - 1
    implicitHeight: 34
    leftPadding: 14
    rightPadding: 14
    Accessible.name: text
    Accessible.checkable: true
    Accessible.checked: selected
    // Our own label, as plain text: chips show add-on names (see README).
    contentItem: Text {
        textFormat: Text.PlainText
        text: chip.text
        font: chip.font
        color: Theme.text
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
    background: Rectangle {
        radius: height / 2
        color: chip.checked ? Theme.raised : chip.down ? Theme.pressed : chip.hovered ? Theme.hover : Theme.panel
        border.width: chip.visualFocus ? 2 : 1
        border.color: chip.checked || chip.visualFocus ? Theme.accent : Theme.line
    }
}
