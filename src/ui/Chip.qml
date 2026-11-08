import QtQuick
import QtQuick.Controls.Basic

// A choice in a row of choices, such as Movies or Series.
Button {
    id: chip
    property bool selected: false
    checkable: true
    checked: selected
    focusPolicy: Qt.StrongFocus
}
