import QtQuick

Rectangle {
    objectName: "subtitleSample"
    width: Math.min(parent.width, 560)
    height: Math.round(width * 9 / 16)
    radius: 6
    color: Theme.canvas
    Image {
        anchors.fill: parent
        source: "images/subtitle-sample.jpg"
        sourceSize: Qt.size(540, 365)
        fillMode: Image.PreserveAspectCrop
        smooth: true
        asynchronous: true
    }
    CornerMask { anchors.fill: parent; radius: parent.radius; color: Theme.panel }
    // Sized the way mpv sizes them: letters 55 and the outline
    // its width in units of a 720-line picture, scaled to this
    // one (Qt's own text outline is a fixed hairline).
    readonly property real unit: height / 720
    readonly property real outlineWidth: session.subtitleStyle.outline * unit
    Rectangle {
        anchors.centerIn: sample
        width: sample.contentWidth + 2 * Math.max(4, parent.outlineWidth * 2)
        height: sample.contentHeight + 2 * Math.max(2, parent.outlineWidth)
        color: Qt.rgba(0, 0, 0, session.subtitleStyle.background / 100)
    }
    // The outline: copies of the line in black, all around it.
    Repeater {
        // Two rings of 16, so a thick outline has no gaps.
        model: parent.outlineWidth > 0 ? 32 : 0
        Text {
            required property int index
            readonly property real reach: sample.parent.outlineWidth * (index < 16 ? 1 : 0.5)
            textFormat: Text.PlainText
            x: sample.x + Math.cos((index % 16) * Math.PI / 8) * reach
            y: sample.y + Math.sin((index % 16) * Math.PI / 8) * reach
            text: sample.text
            font: sample.font
            color: "black"
        }
    }
    Text {
        textFormat: Text.PlainText
        id: sample
        anchors { horizontalCenter: parent.horizontalCenter; bottom: parent.bottom; bottomMargin: Math.round(parent.height * 0.05) }
        text: "This is how subtitles look"
        font.family: session.subtitleStyle.font !== "" ? session.subtitleStyle.font : "sans-serif"
        font.pixelSize: Math.max(8, Math.round(55 * parent.unit * session.subtitleScale))
        color: session.subtitleStyle.color
    }
}
