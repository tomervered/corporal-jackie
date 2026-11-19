import QtQuick

// Clip the composite so artwork and overlays share transparent corners.
Rectangle {
    id: art
    property color backdrop: "transparent" // software fallback on a solid background
    readonly property bool software: GraphicsInfo.api === GraphicsInfo.Software

    radius: Theme.posterRadius
    color: "transparent"
    clip: true
    layer.enabled: !software
    layer.effect: ShaderEffect {
        property size artSize: Qt.size(art.width, art.height)
        property real cornerRadius: art.radius
        fragmentShader: "shaders/rounded-clip.frag.qsb"
    }

    Loader {
        anchors.fill: parent
        z: 1
        active: art.software && art.backdrop.a === 1
        sourceComponent: CornerMask { radius: art.radius; color: art.backdrop }
    }
}
