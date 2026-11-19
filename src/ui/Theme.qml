pragma Singleton
import QtQuick

// The app's colours, type sizes and spacing, in one place. Four looks:
// olive dark (the default), OLED black, bright and custom (the person's
// own palette, which can go to a file and come back). Each starts from four
// colours (accent, canvas, panel, text) that Settings > Appearance can
// change; the rest are mixed from those four. Main.qml sets `mode` and
// `edits` from the saved preferences.
QtObject {
    id: theme

    property string mode: "dark"   // dark, oled, bright, custom
    property var edits: ({})       // {mode: {accent, canvas, panel, text}}

    readonly property var presets: ({
        dark:   { canvas: "#171b16", panel: "#20251e", text: "#eaeee5", accent: "#a0b583" },
        oled:   { canvas: "#000000", panel: "#000000", text: "#e1e6dc", accent: "#96ad78" },
        bright: { canvas: "#f7f8f3", panel: "#ffffff", text: "#222a20", accent: "#526b3d" },
        custom: { canvas: "#171b16", panel: "#20251e", text: "#eaeee5", accent: "#a0b583" }
    })
    // Ready-made palettes for the Custom mode: [name, {canvas, panel, text, accent}].
    readonly property var palettes: [
        ["Olive",     { canvas: "#171b16", panel: "#20251e", text: "#eaeee5", accent: "#a0b583" }],
        ["Midnight",  { canvas: "#11151d", panel: "#1a2030", text: "#e4e9f2", accent: "#7aa2f7" }],
        ["Graphite",  { canvas: "#161616", panel: "#212121", text: "#ececec", accent: "#b0b0b0" }],
        ["Teal",      { canvas: "#0f1a1a", panel: "#172526", text: "#e2efed", accent: "#5cc8b8" }],
        ["Plum",      { canvas: "#1a1420", panel: "#251c2d", text: "#efe6f3", accent: "#c59be0" }],
        ["Ember",     { canvas: "#1b1412", panel: "#271d19", text: "#f2e8e2", accent: "#e8915a" }],
        ["Rose",      { canvas: "#1c1417", panel: "#291d22", text: "#f4e7eb", accent: "#e88aa6" }],
        ["Paper",     { canvas: "#f6f4ef", panel: "#ffffff", text: "#2a2723", accent: "#8a5a2b" }],
        ["Sky",       { canvas: "#f2f6fa", panel: "#ffffff", text: "#1f2a36", accent: "#2f6db3" }]
    ]
    // The four colours of the current look, with the person's changes.
    function base(key) {
        const own = (edits || {})[mode] || {}
        if (mode === "oled" && (key === "canvas" || key === "panel")) return "#000000"
        return own[key] || (presets[mode] || presets.dark)[key]
    }
    // `amount` of a, the rest b.
    function mix(a, b, amount) {
        return Qt.rgba(a.r * amount + b.r * (1 - amount), a.g * amount + b.g * (1 - amount),
                       a.b * amount + b.b * (1 - amount), 1)
    }

    // Light looks get darker error text. Custom is light when its background is.
    readonly property bool light: mode === "bright"
        || (mode === "custom" && 0.2126 * canvas.r + 0.7152 * canvas.g + 0.0722 * canvas.b > 0.5)
    readonly property color canvas: base("canvas")
    readonly property color panel: base("panel")
    readonly property color text: base("text")
    readonly property color accent: base("accent")
    // Black or white, whichever reads better on the accent. (Not named
    // "onAccent": QML takes on<Name> for a handler of a change of <name>.)
    readonly property real accentLuminance: 0.2126 * Math.pow((accent.r + 0.055) / 1.055, 2.4)
                                          + 0.7152 * Math.pow((accent.g + 0.055) / 1.055, 2.4)
                                          + 0.0722 * Math.pow((accent.b + 0.055) / 1.055, 2.4)
    readonly property color accentText: accentLuminance > 0.179 ? "#000000" : "#ffffff"
    readonly property color line: mix(text, panel, 0.41)
    readonly property color muted: mix(text, panel, 0.78)
    readonly property color faint: mix(text, panel, 0.5)
    readonly property color raised: mix(accent, panel, 0.15)
    readonly property color hover: mix(text, panel, 0.08)
    readonly property color pressed: mix(text, panel, 0.16)
    readonly property color divider: mix(text, panel, 0.16)
    readonly property color error: light ? "#a12d22" : "#e09080"
    // Boxes over the video (pop-ups, the episodes list, messages).
    readonly property color overlay: Qt.rgba(panel.r, panel.g, panel.b, 0.94)
    readonly property color scrim: "#a0000000"
    // Old names, kept for the pieces that use them.
    readonly property color background: canvas

    // Type
    readonly property int pageTitleSize: 30
    readonly property int headingSize: 19
    readonly property int bodySize: 15
    readonly property int smallSize: 13
    // Shapes and spacing (a 4 px grid)
    readonly property int radius: 6
    readonly property int posterRadius: 10
    readonly property int controlHeight: 38
    readonly property int sideBarWidth: 184
    readonly property int posterWidth: 150
    readonly property int posterGap: 15
}
