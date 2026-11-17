# Security

Corporal Jackie plays files that strangers made: videos and subtitles from torrents, links and images from add-ons, export files someone shared. This page lists what such a file could try, the rule that stops it, and where that rule lives in the code. It came out of the security review of 2026-11-15.

The two outcomes we guard against are code running on the user's computer and the user's data leaving it. On Windows the second has a quiet form: opening a path such as `\\203.0.113.5\share\x` (or `file://203.0.113.5/share/x`) makes Windows sign in to that server with the user's account and send an NTLMv2 password hash, which can be cracked offline. So no link from outside may become a network-share path unless the user typed or picked it.

`src/common/LinkRules.h` holds the shared checks (`isWebLink`, `isNetworkPath`, `imageLinkOrEmpty`, `looksLikeFileList`), so the player, the add-on code and import all apply the same rules. `tst_common` covers them.

## Files that name other files

An m3u playlist, an mpv EDL list or a CUE sheet tells mpv to open further files. mpv checks those entries against where the list came from, but it trusts our own `torrent://` and `cjhttp://` streams as if the user had typed them. Without a rule, a "video" in a torrent that is really a playlist could open any local file or network share.

`MpvTarget::applyReferencePolicy()` sets mpv's `access-references` before each load:

- Torrents and local videos: off. A file plays itself and nothing else.
- A local file whose name ends in `.m3u`, `.m3u8`, `.pls`, `.cue` or `.edl`: on. The user picked a list, so it plays as a list.
- Web links: on, because HLS needs it. `MpvTarget::onLoadHook()` (mpv's `on_load` hook) then refuses any entry that is not a web link, and `ScopedHttp` refuses an answer that starts like an EDL list or a CUE sheet, before mpv reads it.

The cost is one feature: a local Matroska file with ordered chapters that live in other files plays only its own part.

mpv 0.37 takes only the file name from a CUE sheet's FILE line and looks for it next to the sheet, so a CUE sheet from a server can only name files on that server. The CUE check in `looksLikeFileList` is a second guard in case a later mpv changes that.

Tests: `tst_mpvtarget` `listDisguisedAsAVideoOpensNothing`, `userPickedPlaylistPlays`, `webPlaylistCannotNameALocalFile`, `webFileListIsRefused`, `webHlsCannotNameALocalFile`.

## FFmpeg's own links

FFmpeg's HLS and DASH readers, and MP4 external references, open links by themselves. FFmpeg's HLS reader accepts `file:` segments that end in a media extension. The player sets `demuxer-lavf-o=protocol_whitelist=[http,https,tls,tcp,crypto,data]` (`MpvTarget.cpp`), so FFmpeg can open only network links that way. FFmpeg's own http reader already keeps a whitelist of this kind; ours also covers lists read through `cjhttp://`.

## HTTPS certificates

mpv does not check HTTPS certificates unless told to. The player sets `tls-verify=yes`, so someone on the network path cannot swap a stream for a crafted file. Links that carry credentials go through `ScopedHttp`, which uses Qt's checks.

## Images from add-ons

Posters, logos and backgrounds come from add-on servers and from export files. `imageLinkOrEmpty` keeps `http(s)` links with a server and inline `data:image/` links and drops the rest (`file:`, network shares, `qrc:`, `image:`), so the image is not shown. `StremioProtocol.cpp` applies it to add-on answers, and `Storage.cpp` applies it to imported entries and to rows already in the database.

## Text from files and add-ons

Track names, chapter names, titles and descriptions come from files and add-ons. A Qt Quick `Text` guesses whether text is rich text, and rich text can load images from any link. Every `Text` in `src/ui` sets `textFormat: Text.PlainText`. Menu items use `PlainMenuItem.qml` and chips draw a plain `Text`. `tst_ui` fails if any visible text in the window is not plain (`allTextIsPlain`). `src/ui/README.md` has the rule for new QML.

## Import

An export file may come from someone else. Import:

- leaves out history entries whose video, origin or subtitles are on a network share;
- filters poster links as above;
- adds add-ons and folders only after the user says yes. Settings lists the ones the file would add (`Storage::importExtras`) and offers "Add them too", "Import without them" and "Cancel". An add-on is a server the app will query on every browse, and a folder is a place the app will list and play from.

Tests: `tst_storage` `exportImportMerges`.

## Image decoders

Each Qt image plugin is a parser for files from the internet. Both packages keep JPEG, GIF, ICO and WebP (many add-ons' poster servers send WebP) and delete the rest. The Package steps in `.github/workflows/ci.yml` do this and fail if JPEG or WebP is missing.

## FFmpeg version

The Windows package builds FFmpeg n7.1.5 in `packaging/windows/build-mpv.sh`. That tag still has CVE-2026-70628, an overflow in the DVB subtitle parser (`dvbsub_parser.c`) that a crafted subtitle stream in a torrent can reach. The script applies the upstream one-line fix after cloning and stops the build if the line it patches has changed, so a version bump cannot drop the fix unnoticed. Remove the patch when FFmpeg moves past the fix.

The macOS package bundles Homebrew's current mpv and FFmpeg. The player logs `mpv-version` and `ffmpeg-version` when it starts, and `SOURCE.txt` in the package lists the Homebrew versions. Building mpv and FFmpeg ourselves on macOS, as on Windows, is still to do.

## Known gaps

These were rated low in the review and are not fixed yet:

- libtorrent listens for peers on all interfaces while the engine runs. Every piece is hash-checked; the risk left is a bug in libtorrent's own parsers.
- Trackers from add-ons and web seeds from pasted magnets are not checked, so a tracker on the home network can get a blind GET request.
- In RAM mode the first block of a piece allocates the whole piece. A torrent with 512 MiB pieces can use gigabytes of memory and crash the app.
- The update signature covers `build.txt` and `manifest.txt` joined with no separator, and `apply-update.cmd` passes `%1` unquoted.

## The local relay for links with credentials

Since 2026-11-17 a video link that carries credentials (an add-on's headers, or a user:password) plays through a relay on 127.0.0.1 (`HttpRelay` in `ScopedHttp.cpp`), not through `cjhttp://`. mpv 0.42 reads HLS with FFmpeg, which cannot open `cjhttp://` segments; through the relay, segments named relative to the playlist come back to it and get the same credential rule. The relay:

- listens on the loopback interface only, on a new port for each video, and stops when the next item starts;
- answers only GET and HEAD whose Host header names that address, so a web page cannot reach it through DNS rebinding (a page can still send it a blind request, as to any local port, and cannot read the answer);
- turns each request into a request for the same path on the video's own origin, with the rules above: credentials only on that origin, redirects followed by hand, and an EDL list or CUE sheet refused before mpv sees a byte.

Another program on the same computer could ask the relay for files on the video's server while it plays. The relay adds nothing that program could not already read: the add-on links and their settings are in the app's database. Subtitles still use `cjhttp://`, with the size cap.

## Checked and fine

Add-on links and subtitle links are checked for scheme. `ScopedHttp` sends credentials only to the server they belong to, also across redirects. libtorrent cleans file paths in torrents and refuses symlinks that point outside the download.
