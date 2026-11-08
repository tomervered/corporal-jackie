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
