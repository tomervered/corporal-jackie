# sources — Source adapters

Turns something to watch into one `PlayableSource` (see `src/session/PlaybackTypes.h`): a local file, a web link with its request headers, or a magnet link, plus a title and subtitle files. The Session plays it without knowing where it came from. Nothing here talks to a playback target.

## The adapter list

Every adapter implements `SourceAdapter`: catalogs, items in a catalog, one item's details and episodes, stream choices for one video, and extra subtitles. `Browser` asks every adapter in its list, in order, and merges the answers. v1 has two adapters. A YouTube adapter is one more `browser.addAdapter()` call in `main.cpp`; nothing else changes.

| Adapter | Id | Catalogs | Streams |
|---|---|---|---|
| `StremioAdapter` | `stremio` | every installed add-on's catalogs | from each add-on that serves streams for the id |
| `LocalLibrary` | `local` | "This computer · Movies" and "· Shows" | one per file, with the subtitle files that belong to it |

## Stremio add-ons

`StremioProtocol` holds the protocol as plain functions, so every rule has a unit test: manifest parsing (both the current `extra` and the older `extraSupported` form), `stremio://` links, request URLs (`{base}/{resource}/{type}/{id}[/{extras}].json`, encoded like `encodeURIComponent`), and responses.

- A stream with `infoHash` becomes a magnet link: `fileIdx` goes into `so=` (the file to play), `tracker:` sources into `tr=`, and the file name into `dn=`. The torrent transport resolves it like any pasted magnet link.
- A stream with an http(s) `url` stays a web link. `behaviorHints.proxyHeaders.request` becomes the source's headers, which mpv sends with every request.
- `ytId` and `externalUrl` streams are skipped: nothing in v1 plays them.
- Requests go only to add-ons whose manifest serves that resource, type and id prefix.

`StremioAdapter` keeps the installed list in `addons.json` in the data folder, with each manifest, so catalogs show without network. Step 6 moves it into SQLite. A first run starts with Cinemeta (catalogs and details, no streams), from a built-in copy of its manifest that the next refresh replaces. Add-ons that say `configurationRequired` are refused with a message to set them up on their website first.

Every request is bounded (`JsonFetcher`): 15 s timeout, 8 MB answer limit, JSON parsed on a pool thread. Each add-on remembers its last error, shown in the Add-ons tab. Streams arrive per add-on as each one answers, so a slow add-on shows as "Slow: timed out" without holding up the rest.

`StreamRanking` reads quality, size and seeders from add-on labels ("4k", "💾 15.2 GB", "👤 41") and sorts best first: higher quality, then more seeders, then the bigger file. The panel groups streams by quality.

## Local folders

`LocalLibrary` keeps its folder list in `local-folders.json`. Scans run on a pool thread, skip hidden folders, samples and trailers, and stop at 50,000 videos and 8 levels deep. A `QFileSystemWatcher` on the scanned folders (at most 2,000) starts a new scan 2 s after a change.

`LocalNames` reads the common naming styles: `Nosferatu.1922.1080p.BluRay.mkv`, `Show.S01E02.Title.mkv`, `Show 2x05`, `Show/Season 2/03 - Title.mkv`. Episodes are grouped into one show per title. Subtitle files belong to a video when they sit next to it and their name starts with the video's name (`Movie.en.srt`), or sit in a `Subs/<video name>/` folder, or loose in `Subs/` when the folder has one video. The text after the video's name gives the language.

A local stream id is `local:<path>`, and only paths from the last scan are played.

## Browser

The object the browse panel binds to (`browser` in QML). It lists catalogs, items (100 at a time, more on scroll), details with episodes, and ranked stream choices. A newer request makes older answers stale; they are dropped when they arrive. `play()` hands the choice to `Session::playSource`, then asks subtitle add-ons and adds their files as tracks to that item.

## Tests

- `tests/tst_sources.cpp`: protocol, ranking, file names, subtitle matching, the local library (scan, watch, restart), and the add-on adapter and `Browser` against a fake add-on server: install errors, a timeout, an HTTP 500, an add-on that is not asked, stale answers and the size limit.
- `tests/tst_browseplay.cpp`: an add-on movie and a series episode each play from a web stream (with the add-on's header) and a torrent (local seeder, found through the add-on's tracker) in a headless mpv, and a local file plays with its subtitle.
