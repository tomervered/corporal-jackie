import QtQuick
import QtQuick.Controls.Basic

// A choice in a row of choices, such as Movies or Series.
Button {
    id: chip
    property bool selected: false
    checkable: true
    checked: selected
    // A click flips `checked` and drops the binding; put it back so the
    // chip always shows `selected`.
    onToggled: checked = Qt.binding(() => chip.selected)
    focusPolicy: Qt.StrongFocus
}
