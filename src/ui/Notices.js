.pragma library
// The copyright and consent texts, in one place: the first-run screen,
// Settings and the add-on page show the same words. docs/COPYRIGHT_POLICY.md
// and the README carry the general notice too; keep them in step.

var copyright = "Corporal Jackie is a media player. It does not provide, host or sell any movies or shows. "
    + "Add-ons and torrents come from third parties, and finding a video through them does not mean it is licensed. "
    + "Play only what you have the right to watch, and share only what you have the right to share."

var torrentsTitle = "Play torrents"
var torrentsDetail = "BitTorrent is peer-to-peer file sharing. When you play a torrent, the people sharing it can see "
    + "your IP address and which torrent you opened. Use it only for videos you have the right to download. "
    + "Off: torrents do not play, and the torrent engine never starts."

var uploadsTitle = "Upload to other peers"
var uploadsDetail = "Off: the app sends no part of any video to anyone; some torrents start slower. "
    + "On: while a video plays, the app shares pieces of it with others in the torrent. Sharing a video is a "
    + "separate right from watching it. Sharing stops when the video closes."

var torrentsOff = "Torrent playback is off. Turn it on in Settings (P) if you have the right to download this video."

function addonConsent(address) {
    return "Install the add-on at " + address + "?\n\n"
        + "Add-ons are run by third parties, not by Corporal Jackie. The app will send this one the titles you open "
        + "and search for, and it will see your IP address. Nobody has checked whether the streams it lists are "
        + "licensed: use only those you have the right to watch."
}
