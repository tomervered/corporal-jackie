import QtQuick
import "Format.js" as Format

// What the player is doing, in numbers (press I).
Rectangle {
    property bool streaming: false
    width: stats.implicitWidth + 24
    height: stats.implicitHeight + 16
    radius: 6
    color: "#b0000000"
    Text {
        id: stats
        anchors.centerIn: parent
        color: Theme.text
        font.family: "monospace"
        font.pixelSize: 13
        text: {
            let lines = ["Playing on: " + session.targetName]
            for (const key in session.stats)
                lines.push(key + ": " + session.stats[key])
            lines.push("Speed: " + session.speed + "×")
            if (parent.streaming) lines.push("Torrent: " + session.streamPeers + " peers, " + Format.rate(session.streamRate)
                                             + ", " + session.streamBufferedSeconds.toFixed(1) + " s buffered ahead")
            if (session.subtitleDelay !== 0) lines.push("Subtitle delay: " + Format.signedMs(session.subtitleDelay))
            if (session.audioDelay !== 0) lines.push("Audio delay: " + Format.signedMs(session.audioDelay))
            return lines.join("\n")
        }
    }
}
