# What Corporal Jackie sends and keeps

Corporal Jackie has no server and no account. It sends nothing to its authors: there is no telemetry, update check or crash upload (a crash leaves a note in the local log, nothing more). Everything below is traffic to services the user chose (add-ons, links, torrents) and files on the user's own disk.

## The first-run choices

On first start, and on an install that predates the screen, a window asks about four optional connections before the app makes any of them. All four start off. "Continue with these choices" saves the switches as set; "Allow all" turns them all on. Settings (key P) has the same four switches, and each applies at once, with no restart:

1. **Cinemeta** (Stremio's catalog server). Off: it is not installed, and an install that had it loses it until the person says yes. Turning the switch on installs it; turning it off removes it from Add-ons.
2. **DHT and local-network discovery**, one switch.
3. **The app's eight public trackers.** Off: they are not added to any torrent, and they are taken off torrents already open.
4. **Router port mapping** (UPnP and NAT-PMP).

What the screen does not offer: whether to upload while a torrent plays, and whether a torrent's own trackers and peers are used. Both are part of playing a torrent (see below). A link or file named on the command line, and anything dropped on the window, waits for the answer. With all four off, only the peers and trackers in a magnet link are used, so some torrents will not start.

The answers are stored in the local database (`privacy/*`). A missing answer counts as no.

## Who receives what

| Recipient | What it receives | When |
|---|---|---|
| Cinemeta (`v3-cinemeta.strem.io`, run by Stremio), installed only if the user allows it | The IMDb-style id of each title opened, catalog and search requests (search words included), the user's IP address | Browsing Home and Search, opening a title |
| Each installed add-on | The same requests, for the types and id prefixes its manifest claims; the add-on's own install link, which may hold settings or a key the user pasted | Browsing, opening a title, asking for streams or subtitles |
| Image hosts named by add-ons | Requests for poster and background images, the IP address | Showing lists and details |
| The server of a web stream or subtitle | The request, the IP address, and the headers or login the add-on attached to that stream. Those go only to the stream's own origin (scheme, host and port), at every redirect; a hop to another server gets none of them | Playing |
| Torrent peers | The IP address, the torrent's info hash, which pieces are wanted and held, and the uploaded pieces themselves | While a torrent is open, and for 2 minutes after |
| The DHT and LAN discovery | The info hash and the IP address and port | While a torrent is open, only if the user allows it |
| Trackers | The info hash, IP, port and amounts transferred, with user agent `CorporalJackie/0.1 libtorrent/<version>` | A torrent's own trackers always. Eight public trackers (`kPublicTrackers` in `BrokerEngine.h`) are added only if the user allows it, and only to torrents whose file list shows they are not private (or a .torrent file that already says so); never to a magnet link before its file list is here |
| The router | UPnP and NAT-PMP port mapping requests | While the torrent engine runs, only if the user allows it |

Add-ons never receive local file paths: a local video's id stays with the local folder adapter (`tst_sources localPathsStayOnThisComputer`).

### Peer IP visibility and uploading

Torrenting is public. Every peer in a swarm sees the user's IP address and which torrent it is in, and anyone can join a swarm to collect those addresses. Corporal Jackie does not hide the IP address; a VPN does that.

While a torrent is open the app uploads to other peers, because BitTorrent peers trade pieces and a peer that never uploads gets slow downloads. There is no upload limit by default (`uploadLimit = 0`). Two minutes after the last reader of a torrent closes, the torrent is paused: it stops uploading and drops its peers. The app does not seed after that.

## What stays on this computer

The data folder is `QStandardPaths::AppDataLocation` and the cache folder `QStandardPaths::CacheLocation` (on Windows, both under `%LOCALAPPDATA%`). Settings can move the torrent cache.

| Data | Where | Kept until |
|---|---|---|
| Watch history: titles, add-on item ids, episode, position, the stream link and any headers the add-on gave it | `corporal-jackie.sqlite` in the data folder (plus `-wal` and `-shm` while running) | The newest 2000 entries are kept. Forget (Continue Watching) or Clear (Settings) removes them |
| Library, add-on list with install links, local folders, settings | Same database | Removed by the user |
| Logs: what played (file names, and servers as `https://host/…`, never paths, queries or logins), errors | `logs/corporal-jackie.log` in the data folder, and the previous run's `corporal-jackie.1.log` | Two runs; Clear history empties both |
| Poster images | `images/` in the cache folder | The size cap in Settings (100 MB to 1 GB); Clear in Settings |
| Torrent pieces (disk mode) | `torrents/` in the cache folder, or the folder chosen in Settings | The cache cap (10 GB default), oldest torrent first; Clear in Settings |
| Torrent pieces (RAM mode, default) | Memory only; each torrent's file list is written to the torrent cache | Closing the torrent |
| Exports | A file the user picks | The user deletes it |

### Turning history off

"Remember what I watch" in Settings stops new history entries. Nothing that plays is recorded, Continue Watching does not change, and resume positions are not saved. Entries already saved stay until the user forgets or clears them. Logs are still written; Clear history empties them.

### Deleting

Forget and Clear delete the rows, overwrite them on disk (`PRAGMA secure_delete`), and truncate the write-ahead log, so the titles are not left readable in the database files (`tst_storage clearedHistoryLeavesNoTrace`). Clear history also empties the logs.

Copies the app does not manage are out of its reach: export files, the operating system's backups (Time Machine, File History, cloud-synced folders), and the poster images in the image cache, which show what was browsed until that cache is cleared.

### Exports

The default export is meant to be shareable. It leaves out request headers, add-on stream links, signed links (any link with a query or a login), magnet trackers, and the install links of add-ons that hold settings, which it lists by name instead. History entries keep their item ids, so Continue Watching still resumes after an import: the add-on is asked for a stream again. Export in Settings always writes this shareable form. The storage code also has a full mode that keeps all of it (`ExportMode::Full`), but the app does not offer it. `src/storage/README.md` has the details.

## What is not covered

- Add-ons are third-party services with their own logging; what they keep is up to them.
- The credentials an add-on attaches to a stream are stored in the history so that the stream can resume. They are kept in the database, not in the operating system's credential store.
