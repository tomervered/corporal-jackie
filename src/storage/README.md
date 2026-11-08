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
- **Limits.** The history keeps the newest 2000 entries; the library 5000 items.
- **Old files.** Before this, settings were in QSettings and the lists in `addons.json` and `local-folders.json`. The app copies them over once, at the first start, and leaves the old files where they are.

## History keys

An entry's key says what was watched, so the same episode found through another stream resumes at the same spot:

- `<adapter>:<item>:<video>` for anything picked in Browse, for example `stremio:tt0903747:tt0903747:2:3`.
- `url:<source>` for a file or link opened directly. For a magnet link, the source is the magnet link itself, not the stream URL it turns into, which changes every run (`Storage::sourceKey`).

A video counts as finished when less than 5% of it is left, and at least 20 seconds. Continue Watching shows one entry per show or movie: the newest, if it is not finished and is past 30 seconds.

## Library

`Library` (QML `library`) sits between the Session, the browse panel and the Storage:

- It records the Session's resume points (every 5 seconds while playing, on pause, on switching items, and on quit).
- It tells the Session where to start an item opened without a start time.
- It gives the UI Continue Watching, the saved items, and export and import.

## Export and import

Export writes one JSON file with the library, history, add-ons and folders (`"format": "corporal-jackie-export"`, `"version": 1`). Import merges: newer history wins, and missing library items, add-ons and folders are added.
