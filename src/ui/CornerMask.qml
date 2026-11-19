import QtQuick
import QtQuick.Shapes

// Rounds the corners of a picture: paints the four corners outside a
// rounded rectangle in the colour behind it. (Qt 6.4 has no mask effect
// without the compatibility module.) Lay it over the picture, the same size.
Shape {
    id: mask
    property real radius: 10
    property color color: Theme.canvas
    layer.enabled: true
    layer.samples: 4
    ShapePath {
        strokeWidth: -1
        fillColor: mask.color
        fillRule: ShapePath.OddEvenFill
        PathSvg {
            readonly property real w: mask.width
            readonly property real h: mask.height
            readonly property real r: Math.min(mask.radius, w / 2, h / 2)
            path: "M0 0H" + w + "V" + h + "H0Z"
                + "M" + r + " 0H" + (w - r) + "A" + r + " " + r + " 0 0 1 " + w + " " + r
                + "V" + (h - r) + "A" + r + " " + r + " 0 0 1 " + (w - r) + " " + h
                + "H" + r + "A" + r + " " + r + " 0 0 1 0 " + (h - r)
                + "V" + r + "A" + r + " " + r + " 0 0 1 " + r + " 0Z"
        }
    }
}
