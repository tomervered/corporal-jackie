import QtQuick
import QtQuick.Controls.Basic

// A 2:3 poster with its name (up to two lines) and one line of detail
// under it. Shows the name on a plain tile when there is no poster. A
// poster that fails to download (a busy image server answers some of a
// burst of requests with an error) is asked for again a few times, further
// apart each time. Keyboard focus and the pointer draw a soft accent
// glow around it.
Item {
    id: tile
    property var item: ({})
    property int attempt: 0
    onItemChanged: { attempt = 0; retry.stop() }
    property bool current: false
    property real posterWidth: Theme.posterWidth
    property string detail: tile.item.releaseInfo || ""
    property real progress: -1          // 0-1 draws how far in, under the poster
    signal activated()
    width: posterWidth
    height: posterWidth * 1.5 + 58
    Accessible.role: Accessible.Button
    Accessible.name: tile.item.name || ""

    // The chosen tile (keyboard or pointer) gets a soft glow and an outline.
    readonly property bool lit: tile.current || tileHover.hovered
    Loader {
        id: glow
        readonly property real spread: 14
        anchors { fill: art; margins: -spread }
        opacity: tile.lit ? 1 : 0
        active: !art.software && (tile.lit || opacity > 0)
        Behavior on opacity { NumberAnimation { duration: 140 } }
        sourceComponent: ShaderEffect {
            property size artSize: Qt.size(art.width, art.height)
            property real cornerRadius: art.radius
            property real spread: glow.spread
            property color accent: Theme.accent
            fragmentShader: "shaders/poster-glow.frag.qsb"
        }
    }
    RoundedArtwork {
        id: art
        width: parent.width
        height: parent.width * 1.5
        color: Theme.raised
        backdrop: Theme.canvas
        source: tile.item.poster || ""
        progress: tile.progress
        highlight: tileHover.hovered ? 0.08 : 0
        Behavior on highlight { NumberAnimation { duration: 120 } }
        onStatusChanged: if (status === Image.Error && tile.attempt < 4) retry.start()
        Text {
            textFormat: Text.PlainText
            anchors { fill: parent; margins: 12 }
            visible: art.status !== Image.Ready
            text: tile.item.name || ""
            color: Theme.text
            font.pixelSize: Theme.bodySize
            font.weight: Font.DemiBold
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        // The outline shows focus or hover on the same rounded edge.
        Rectangle {
            anchors.fill: parent
            z: 2
            radius: art.radius
            color: "transparent"
            border.width: 2
            border.color: Theme.accent
            opacity: tile.lit ? 1 : 0
            Behavior on opacity { NumberAnimation { duration: 140 } }
        }
        Timer {
            id: retry
            interval: 2000 * Math.pow(2, tile.attempt) + Math.random() * 1000
            onTriggered: {
                tile.attempt++
                // Not from Qt's image cache, which may keep the failure.
                art.cache = false
                art.source = ""
                art.source = Qt.binding(() => tile.item.poster || "")
            }
        }
        HoverHandler { id: tileHover; cursorShape: Qt.PointingHandCursor }
        TapHandler { onTapped: tile.activated() }
    }
    Text {
        textFormat: Text.PlainText
        id: tileName
        anchors { top: art.bottom; topMargin: 8 }
        width: parent.width
        text: tile.item.name || ""
        color: Theme.text
        font.pixelSize: Theme.bodySize - 1
        font.weight: Font.DemiBold
        wrapMode: Text.Wrap
        maximumLineCount: 2
        elide: Text.ElideRight
        lineHeight: 0.95
    }
    Text {
        textFormat: Text.PlainText
        anchors { top: tileName.bottom; topMargin: 2 }
        width: parent.width
        text: tile.detail
        color: Theme.muted
        font.pixelSize: Theme.smallSize
        elide: Text.ElideRight
    }
    ToolTip.visible: tileHover.hovered && tileName.truncated
    ToolTip.delay: 600
    ToolTip.text: tile.item.name || ""
}
