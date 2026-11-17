import QtQuick

// A small drawn icon for the control bar, so it looks the same on every
// system (symbol characters come from whatever font has them, and some
// systems draw them as coloured emoji).
// Names: play, pause, next, volume, muted, fullscreen, windowed.
Canvas {
    id: icon
    property string name
    property color color: Theme.text
    implicitWidth: 18
    implicitHeight: 18
    onNameChanged: requestPaint()
    onColorChanged: requestPaint()
    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()

    onPaint: {
        const ctx = getContext("2d")
        const w = width, h = height
        ctx.reset()
        ctx.fillStyle = icon.color
        ctx.strokeStyle = icon.color
        ctx.lineWidth = Math.max(1.5, w / 10)
        ctx.lineCap = "round"
        ctx.lineJoin = "round"
        const speaker = () => {
            ctx.beginPath()
            ctx.moveTo(w * 0.08, h * 0.36); ctx.lineTo(w * 0.28, h * 0.36); ctx.lineTo(w * 0.52, h * 0.14)
            ctx.lineTo(w * 0.52, h * 0.86); ctx.lineTo(w * 0.28, h * 0.64); ctx.lineTo(w * 0.08, h * 0.64)
            ctx.closePath(); ctx.fill()
        }
        switch (icon.name) {
        case "play":
            ctx.beginPath()
            ctx.moveTo(w * 0.22, h * 0.12); ctx.lineTo(w * 0.86, h * 0.5); ctx.lineTo(w * 0.22, h * 0.88)
            ctx.closePath(); ctx.fill()
            break
        case "pause":
            ctx.fillRect(w * 0.2, h * 0.14, w * 0.2, h * 0.72)
            ctx.fillRect(w * 0.6, h * 0.14, w * 0.2, h * 0.72)
            break
        case "next":
            ctx.beginPath()
            ctx.moveTo(w * 0.14, h * 0.14); ctx.lineTo(w * 0.64, h * 0.5); ctx.lineTo(w * 0.14, h * 0.86)
            ctx.closePath(); ctx.fill()
            ctx.fillRect(w * 0.68, h * 0.14, w * 0.16, h * 0.72)
            break
        case "volume":
            speaker()
            ctx.beginPath(); ctx.arc(w * 0.52, h * 0.5, w * 0.22, -Math.PI / 4, Math.PI / 4); ctx.stroke()
            ctx.beginPath(); ctx.arc(w * 0.52, h * 0.5, w * 0.4, -Math.PI / 4, Math.PI / 4); ctx.stroke()
            break
        case "muted":
            speaker()
            ctx.beginPath()
            ctx.moveTo(w * 0.66, h * 0.36); ctx.lineTo(w * 0.92, h * 0.64)
            ctx.moveTo(w * 0.92, h * 0.36); ctx.lineTo(w * 0.66, h * 0.64)
            ctx.stroke()
            break
        case "fullscreen":   // four corners pointing out
        case "windowed": {   // four corners pointing in
            const a = 0.12, b = 0.4, out = icon.name === "fullscreen"
            const corner = (x, y, dx, dy) => {
                ctx.beginPath()
                ctx.moveTo(x + dx * w * (b - a), y); ctx.lineTo(x, y); ctx.lineTo(x, y + dy * h * (b - a))
                ctx.stroke()
            }
            if (out) {
                corner(w * a, h * a, 1, 1); corner(w * (1 - a), h * a, -1, 1)
                corner(w * a, h * (1 - a), 1, -1); corner(w * (1 - a), h * (1 - a), -1, -1)
            } else {
                corner(w * b, h * b, -1, -1); corner(w * (1 - b), h * b, 1, -1)
                corner(w * b, h * (1 - b), -1, 1); corner(w * (1 - b), h * (1 - b), 1, 1)
            }
            break
        }
        }
    }
}
