# storage — the app's database

One SQLite file, `corporal-jackie.sqlite` in the app's data folder, holds:

- settings (hardware decoding, torrent storage, image cache size)
- installed add-ons and the user's video folders
- the library (items saved from Browse)
- the watch history: where the user got to in each video

## Storage

`Storage` owns the database. Every query runs on one worker thread. The UI reads from an in-memory copy, loaded once at start, so a read never waits on the disk. Changes update the copy at once and are written behind, in order.

- **Migrations.** `PRAGMA user_version` holds the schema version. Each entry in `kMigrations` moves it up one, inside a transaction. A shipped migration is never edited: the next one is added. A database from a newer build is refused, not changed.
- **Durability.** WAL journal with `synchronous=NORMAL`: an app crash loses nothing that was written, a power cut at most the last moments.
- **Failed writes.** Every write reports whether it landed. A failure (a full disk, a broken file) sets `Storage::error()`, emits `errorChanged`, and Settings shows the message. The in-memory copy keeps the change, so the app goes on working, but the change is lost at exit.
- **Limits.** The history keeps the newest 2000 entries; the library 5000 items.
- **Old files.** Before this, settings were in QSettings and the lists in `addons.json` and `local-folders.json`. The app copies them over once, at the first start, and leaves the old files where they are.

## History keys

An entry's key says what was watched, so the same episode found through another stream resumes at the same spot:

- `<adapter>:<item>:<video>` for anything picked in Browse, for example `stremio:tt0903747:tt0903747:2:3`.
- `url:<source>` for a file or link opened directly. For a magnet link, the source is the magnet's info hash and file number (`magnet:?xt=urn:btih:<hash>&so=<file>`), not the stream URL it turns into, which changes every run, and not its trackers, which can hold a private site's passkey (`Storage::sourceKey`). Keys from before this rule are moved over when the database opens.

A video counts as finished when less than 5% of it is left, and at least 20 seconds. Continue Watching shows one entry per show or movie: the newest, if it is not finished and is past 30 seconds.

## Library

`Library` (QML `library`) sits between the Session, the browse panel and the Storage:

- It records the Session's resume points (every 5 seconds while playing, on pause, on switching items, and on quit).
- It tells the Session where to start an item opened without a start time.
- It gives the UI Continue Watching, the saved items, and export and import.

## Export and import

Export writes one JSON file with the library, history, add-ons and folders (`"format": "corporal-jackie-export"`, `"version": 1`). Import merges: newer history wins, and missing library items, add-ons and folders are added.

The export from Settings is shareable (`"contents": "shareable"`). It leaves out what lets someone else in:

- request headers (tokens, cookies) and add-on subtitle links;
- web stream links of add-on items: the entry keeps the item and episode ids, and Continue Watching opens the item so the add-on finds a stream again, which then resumes at the saved spot;
- history of web links opened directly that have a user:password, a query or headers (there is nothing to find them again by);
- magnet trackers, peers and names: only the info hash and file stay;
- poster links with a query;
- add-ons whose link has anything before `/manifest.json` or a query, which is where add-ons keep their settings and API keys. They are listed by id and name under `addonsWithSettings`, so they can be set up again.

`Storage::ExportMode::Full` keeps everything, for a private backup; Settings does not offer it, so the app only writes the shareable form. A secret written into a link's path (some debrid links) cannot be told apart from an ordinary path; add-on stream links are left out for that reason, and a pasted link without a query is kept.

Import checks the whole file first (types, finite positions, link schemes: web, file, torrent, magnet) and refuses a damaged file before writing anything. It then writes every change in one transaction and waits for it: if it fails, nothing changes on disk or in memory, and the error says so.
