import QtQuick
import QtQuick.Controls.Basic

// A choice in a row of choices, such as Movies or Series: a pill that is
// filled in when picked. Shows add-on names as plain text (see README).
AppButton {
    id: chip
    property bool selected: false
    checkable: true
    checked: selected
    // A click flips `checked` and drops the binding; put it back so the
    // chip always shows `selected`.
    onToggled: checked = Qt.binding(() => chip.selected)
    focusPolicy: Qt.StrongFocus
    implicitHeight: 32
    implicitWidth: Math.max(48, implicitContentWidth + leftPadding + rightPadding)
    background: Rectangle {
        radius: height / 2
        color: chip.checked ? (chip.hovered ? Qt.lighter(Theme.accent, 1.08) : Theme.accent)
             : chip.down ? "#3c3c48" : chip.hovered ? "#34343e" : "#24242c"
        border.width: chip.visualFocus ? 2 : 1
        border.color: chip.visualFocus ? (chip.checked ? Theme.text : Theme.accent) : chip.checked ? "transparent" : "#3a3a44"
    }
}
