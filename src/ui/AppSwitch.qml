import QtQuick
import QtQuick.Controls.Basic

// An on/off switch: a light handle on a grey track, gold when on.
Switch {
    id: control
    // Basic style counts the indicator through its label; this one has none.
    implicitWidth: implicitIndicatorWidth + leftPadding + rightPadding
    implicitHeight: implicitIndicatorHeight + topPadding + bottomPadding
    indicator: Rectangle {
        implicitWidth: 44
        implicitHeight: 24
        x: control.leftPadding + (control.availableWidth - width) / 2
        y: control.topPadding + (control.availableHeight - height) / 2
        radius: height / 2
        color: control.checked ? Theme.accent : "#3a3a44"
        opacity: control.enabled ? 1 : 0.45
        border.width: control.visualFocus ? 2 : 0
        border.color: control.checked ? Theme.text : Theme.accent
        Rectangle {
            x: control.checked ? parent.width - width - 3 : 3
            anchors.verticalCenter: parent.verticalCenter
            width: 18; height: 18; radius: 9
            color: control.down ? "#d0d0d8" : Theme.text
            Behavior on x { NumberAnimation { duration: 120; easing.type: Easing.OutQuad } }
        }
    }
    contentItem: Item {}
}
