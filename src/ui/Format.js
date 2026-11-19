.pragma library
// Times, rates and offsets in words, for the player's labels.

function time(seconds) {
    if (!isFinite(seconds) || seconds < 0) seconds = 0
    const s = Math.floor(seconds % 60), m = Math.floor(seconds / 60) % 60, h = Math.floor(seconds / 3600)
    const pad = n => (n < 10 ? "0" : "") + n
    return h > 0 ? h + ":" + pad(m) + ":" + pad(s) : m + ":" + pad(s)
}

function rate(bytesPerSecond) {
    const mb = bytesPerSecond / (1024 * 1024)
    return mb >= 1 ? mb.toFixed(1) + " MB/s" : Math.round(bytesPerSecond / 1024) + " KB/s"
}

function signedMs(seconds) {
    const ms = Math.round(seconds * 1000)
    return (ms > 0 ? "+" : "") + ms + " ms"
}

// The upscalers by their Session names, and the heights they can aim for.
function upscalerLabel(name) {
    return ({ off: "Off", sgsr: "Snapdragon GSR (light)", cas: "AMD CAS (light)", fsr: "AMD FSR",
        anime4k: "Anime4K (cartoons)", nvidia: "NVIDIA VSR", intel: "Intel VSR", metalfx: "Apple MetalFX" })[name] || name
}
var upscaleTargets = [[0, "Screen size"], [1080, "1080p"], [1440, "1440p"], [2160, "4K"]]

// Subtitle looks, the same in Settings and in the player's Style tab:
// [value, label] pairs.
var subtitleSizes = [[0.75, "Small"], [1.0, "Normal"], [1.25, "Large"], [1.5, "Larger"], [2.0, "Huge"]]
var subtitleColors = [["#FFFFFF", "White"], ["#FFE14D", "Yellow"], ["#C8C8C8", "Grey"]]
var subtitleOutlines = [[0, "None"], [1.5, "Thin"], [3, "Normal"], [5, "Thick"]]
var subtitleBackgrounds = [[0, "None"], [40, "Light"], [70, "Dark"], [100, "Solid"]]

// Named colors offered when a palette color is changed: [hex, name].
var colorPresets = [
    ["#A0B583", "Olive"], ["#5CC8B8", "Teal"], ["#7AA2F7", "Sky blue"], ["#2F6DB3", "Blue"],
    ["#C59BE0", "Lilac"], ["#9B6BD3", "Violet"], ["#E88AA6", "Rose"], ["#D94F6B", "Raspberry"],
    ["#E8915A", "Amber"], ["#E5B95C", "Gold"], ["#8A5A2B", "Brown"], ["#B0B0B0", "Silver"],
    ["#FFFFFF", "White"], ["#F6F4EF", "Paper"], ["#EAEEE5", "Mist"], ["#24262A", "Charcoal"],
    ["#171B16", "Forest night"], ["#000000", "Black"]
]
// The name of a preset color, or "" when the color is not one.
function colorName(hex) {
    const h = String(hex).toLowerCase()
    for (const p of colorPresets)
        if (p[0].toLowerCase() === h) return p[1]
    return ""
}
