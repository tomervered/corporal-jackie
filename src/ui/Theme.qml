pragma Singleton
import QtQuick

// The app's colours and the poster size, in one place.
QtObject {
    readonly property color background: "#101014"
    readonly property color panel: "#141418"
    readonly property color text: "#f2f2f5"
    readonly property color muted: "#9a9aa6"
    readonly property color faint: "#5c5c66"
    readonly property color accent: "#e8b04b"
    readonly property color error: "#e09080"
    readonly property color overlay: "#e01a1a20"   // pop-ups over the video
    readonly property int posterWidth: 132
}
