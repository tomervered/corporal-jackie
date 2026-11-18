import QtQuick
import QtQuick.Window
import QtQuick.Shapes
import "Icons.js" as Icons

// A line icon drawn from SVG path data on a 24 × 24 grid, so it stays sharp
// at any size and takes any colour. One family for the whole app: the side
// bar, the player bar and the buttons.
Item {
    id: icon
    property string name
    property color color: Theme.text
    property real size: 20
    implicitWidth: size
    implicitHeight: size
    // Smooth edges on the GPU (the software renderer smooths on its own),
    // drawn at the screen's pixel density.
    layer.enabled: true
    layer.samples: 4
    layer.smooth: true
    layer.textureSize: Qt.size(Math.ceil(size * Screen.devicePixelRatio), Math.ceil(size * Screen.devicePixelRatio))

    readonly property var path: Icons.paths[name] || ["", false]

    Shape {
        width: 24
        height: 24
        scale: icon.size / 24
        transformOrigin: Item.TopLeft
        ShapePath {
            strokeColor: icon.path[1] ? "transparent" : icon.color
            strokeWidth: icon.path[1] ? 0 : 1.8
            fillColor: icon.path[1] ? icon.color : "transparent"
            fillRule: ShapePath.OddEvenFill
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            PathSvg { path: icon.path[0] }
        }
    }
}
