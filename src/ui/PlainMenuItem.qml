import QtQuick
import QtQuick.Controls.Basic

// A menu item whose text is shown as plain text. Track and chapter names come
// from the video file, and Basic style's own label would render markup in
// them, <img src="https://..."> included (see README).
MenuItem {
    id: item
    contentItem: Text {
        readonly property real arrowPadding: item.subMenu && item.arrow ? item.arrow.width + item.spacing : 0
        readonly property real indicatorPadding: item.checkable && item.indicator ? item.indicator.width + item.spacing : 0
        leftPadding: !item.mirrored ? indicatorPadding : arrowPadding
        rightPadding: item.mirrored ? indicatorPadding : arrowPadding
        textFormat: Text.PlainText
        text: item.text
        font: item.font
        color: item.palette.windowText
        elide: Text.ElideRight
        verticalAlignment: Text.AlignVCenter
    }
}
