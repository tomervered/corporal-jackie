import QtQuick

// Sample the decoded image directly. Each card needs only its image texture.
Item {
    id: art
    property alias source: image.source
    property alias fillMode: image.fillMode
    property alias cache: image.cache
    readonly property alias status: image.status
    property real radius: Theme.posterRadius
    property color color: "transparent"
    property color backdrop: "transparent"
    property real progress: -1
    property real highlight: 0
    readonly property bool software: GraphicsInfo.api === GraphicsInfo.Software

    Rectangle {
        anchors.fill: parent
        visible: art.software
        radius: art.radius
        color: art.color
    }
    Image {
        id: image
        anchors.fill: parent
        visible: art.software
        fillMode: Image.PreserveAspectCrop
        sourceSize.width: Math.ceil(art.width * Screen.devicePixelRatio)
        asynchronous: true
    }

    ShaderEffect {
        anchors.fill: parent
        visible: !art.software
        property Item source: image
        property size artSize: Qt.size(art.width, art.height)
        property size imageScale: Qt.size(art.width / Math.max(1, image.paintedWidth),
                                          art.height / Math.max(1, image.paintedHeight))
        property real cornerRadius: art.radius
        property color background: art.color
        property color accent: art.progress > 0 ? Theme.accent : "transparent"
        property real progress: Math.min(1, art.progress)
        property real highlight: art.highlight
        property real imageReady: image.status === Image.Ready ? 1 : 0
        fragmentShader: "shaders/rounded-clip.frag.qsb"
    }

    Loader {
        anchors.fill: parent
        active: art.software
        sourceComponent: Item {
            Rectangle {
                anchors.fill: parent
                radius: art.radius
                color: "white"
                opacity: art.highlight
            }
            Rectangle {
                visible: art.progress > 0
                anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                height: 4
                color: "#a0000000"
                Rectangle { width: parent.width * Math.min(1, art.progress); height: parent.height; color: Theme.accent }
            }
            CornerMask { anchors.fill: parent; visible: art.backdrop.a === 1; radius: art.radius; color: art.backdrop }
        }
    }
}
