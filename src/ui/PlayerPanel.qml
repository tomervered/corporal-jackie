import QtQuick
import QtQuick.Controls.Basic

// A panel that opens above the player bar (chapters, audio and subtitles,
// playback settings): a title, then its rows, which scroll inside it when
// they are too many. Clicks on it stay on it.
Rectangle {
    id: panel
    property string title
    property real maxHeight: 535
    // Extra parts under the scrolling rows (the subtitle timing).
    property alias footer: footerItem.data
    default property alias rows: body.data

    width: 365
    height: Math.min(maxHeight, header.height + body.implicitHeight + 6 + footerItem.height + (footerItem.height > 0 ? 12 : 6))
    radius: 15
    color: Theme.panel
    border.color: Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.17)
    Accessible.role: Accessible.Pane
    Accessible.name: title

    MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons; onWheel: wheel => wheel.accepted = true }

    Text {
        id: header
        textFormat: Text.PlainText
        x: 17
        topPadding: 16
        bottomPadding: 10
        text: panel.title
        color: Theme.text
        font.pixelSize: 16
        font.weight: Font.DemiBold
    }
    Flickable {
        id: flick
        anchors { left: parent.left; right: parent.right; top: header.bottom; bottom: footerItem.top; bottomMargin: 6 }
        contentHeight: body.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { policy: flick.contentHeight > flick.height + 1 ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff }
        Column {
            id: body
            x: 7
            width: flick.width - 14 - (flick.contentHeight > flick.height + 1 ? 8 : 0)
        }
    }
    Item {
        id: footerItem
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom; bottomMargin: height > 0 ? 12 : 6 }
        height: childrenRect.height
    }
}
