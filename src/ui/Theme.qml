pragma Singleton
import QtQuick

// The app's colours and sizes, in one place. Qt Quick Controls get the same
// colours from the application palette (AppCore::useDarkPalette).
QtObject {
    readonly property color background: "#101014"
    readonly property color panel: "#16161b"
    readonly property color sideBar: "#0c0c0f"
    readonly property color card: "#1a1a20"        // add-on and folder rows, empty tiles
    readonly property color cardHover: "#24242c"
    readonly property color line: "#26262e"        // dividers and panel borders
    readonly property color text: "#f2f2f5"
    readonly property color muted: "#9a9aa6"
    readonly property color faint: "#5c5c66"
    readonly property color accent: "#e8b04b"
    readonly property color accentSoft: "#26e8b04b" // the selected side bar entry
    readonly property color error: "#e09080"
    readonly property color overlay: "#f01a1a20"   // pop-ups over the video
    readonly property int posterWidth: 132
    readonly property int radius: 8
}
