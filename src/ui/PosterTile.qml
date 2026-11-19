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
    property color cornerColor: Theme.canvas   // what is behind the tile
    property real progress: -1          // 0-1 draws how far in, under the poster
    signal activated()
    width: posterWidth
    height: posterWidth * 1.5 + 58
    Accessible.role: Accessible.Button
    Accessible.name: tile.item.name || ""

    // The chosen tile (keyboard or pointer) glows: soft accent light around
    // the poster (a blurred shadow drawn once by a Canvas, which works on
    // every Qt version) and a thin outline on its edge.
    readonly property bool lit: tile.current || tileHover.hovered
    Canvas {
        id: glow
        readonly property real spread: 14
        anchors { fill: art; margins: -spread }
        opacity: tile.lit ? 1 : 0
        visible: opacity > 0
        Behavior on opacity { NumberAnimation { duration: 140 } }
        onVisibleChanged: if (visible) requestPaint()
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
        Connections { target: Theme; function onAccentChanged() { glow.requestPaint() } }
        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            const r = art.radius, x = spread, y = spread, w = width - 2 * spread, h = height - 2 * spread
            const shape = () => { ctx.beginPath(); ctx.roundedRect(x, y, w, h, r, r); ctx.closePath() }
            ctx.shadowColor = Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.55)
            ctx.shadowBlur = 10
            ctx.fillStyle = Theme.accent
            shape(); ctx.fill()
            // Only the light outside the poster stays.
            ctx.shadowColor = "transparent"
            ctx.globalCompositeOperation = "destination-out"
            shape(); ctx.fill()
        }
    }
    Rectangle {
        id: art
        width: parent.width
        height: parent.width * 1.5
        radius: Theme.posterRadius
        color: Theme.raised
        clip: true
        Text {
            textFormat: Text.PlainText
            anchors { fill: parent; margins: 12 }
            visible: poster.status !== Image.Ready
            text: tile.item.name || ""
            color: Theme.text
            font.pixelSize: Theme.bodySize
            font.weight: Font.DemiBold
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        Image {
            id: poster
            anchors.fill: parent
            source: tile.item.poster || ""
            fillMode: Image.PreserveAspectCrop
            // Decoded at the size it is drawn: Qt keeps every shown poster in
            // memory at this size (twice the tile was 4 times the bytes at 100 %).
            sourceSize.width: Math.ceil(tile.posterWidth * Screen.devicePixelRatio)
            asynchronous: true
            onStatusChanged: if (status === Image.Error && tile.attempt < 4) retry.start()
        }
        Rectangle {
            anchors.fill: parent
            radius: art.radius
            color: "white"
            opacity: tileHover.hovered ? 0.08 : 0
            Behavior on opacity { NumberAnimation { duration: 120 } }
        }
        // How far in
        Rectangle {
            visible: tile.progress > 0
            anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
            height: 4
            color: "#a0000000"
            Rectangle { width: parent.width * Math.min(1, tile.progress); height: parent.height; color: Theme.accent }
        }
        CornerMask { anchors.fill: parent; radius: art.radius; color: tile.cornerColor }
        // The outline sits on the poster's edge (it also hides the mask's seam).
        Rectangle {
            anchors.fill: parent
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
                poster.cache = false
                poster.source = ""
                poster.source = Qt.binding(() => tile.item.poster || "")
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
