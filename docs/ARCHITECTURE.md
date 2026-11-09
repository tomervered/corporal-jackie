# Architecture

Corporal Jackie is one Qt process. The window is QML; the video is libmpv drawing into a Qt Quick item; torrents are libtorrent, run by the app itself with no local HTTP server. This page is the map. Each folder under `src/` has a README with the details, and the two contracts in this folder describe the boundaries that tests hold the code to.

## Layers

```
ui (QML)  ──binds to──▶  app (AppCore)
                            │
     ┌──────────┬───────────┼────────────┬──────────┐
     ▼          ▼           ▼            ▼          ▼
  sources    storage     session      targets   transports
  (add-ons,  (SQLite)   (Session,     (mpv)      (torrent://)
  folders)                contract)                  │
                                                     ▼
                                                   broker (libtorrent)
  common: logging, folders, crash reports; used by every layer
```

An arrow means "links to and calls". Nothing calls upward, and the rules below are what keep the layers swappable:

- **ui** binds only to the context properties `AppCore::exposeTo` gives it: `session`, `browser`, `library`, `preferences`, `caches`. It never sees mpv, libtorrent or the network.
- **session** owns the playing item, position, tracks and errors, and talks to a `PlaybackTarget` through the interface in [TARGET_CONTRACT.md](TARGET_CONTRACT.md). It includes no mpv header. Cast would be a second target.
- **targets** has `MpvTarget`, the only code that calls libmpv for playback.
- **transports** has `TorrentTransport`, which registers `torrent://` with mpv and maps mpv's stream callbacks onto the broker.
- **broker** is the only code that calls libtorrent ([BROKER_CONTRACT.md](BROKER_CONTRACT.md)). It does not know that mpv is the reader.
- **sources** turns a catalog pick, a local file or a pasted link into one `PlayableSource`. Adapters (`StremioAdapter`, `LocalLibrary`) sit behind `SourceAdapter`, so a new kind of source is one more adapter.
- **storage** keeps settings, add-ons, folders, the library and watch history in one SQLite file.
- **app** builds all of the above (`AppCore`) and opens the window (`main.cpp`). Tests build the same `AppCore` around a fake target.

## From a click to a picture

1. The user picks a stream in Browse. `Browser::play` hands its `PlayableSource` to `Session::playSource`.
2. For a magnet link, the Session asks its resolver, `TorrentTransport::resolve`, which returns `torrent://<info hash>/<file index>` and keeps the link's trackers and peers for the open.
3. The Session tells `MpvTarget` to load that URL. mpv calls the transport's open callback on its own stream thread, and the transport calls `StreamBroker::open`, which adds the torrent and blocks until the file list arrives.
4. mpv reads the file's start to probe the container. Each `StreamHandle::read` blocks until its piece is downloaded and verified, then copies the bytes out.

## How a byte gets from a peer to mpv

1. libtorrent downloads blocks on its network thread and verifies each piece. On disk, the piece goes into the cache folder. In RAM-only mode, `MemoryStorage` keeps it in a map instead, so nothing touches the SSD.
2. libtorrent posts a `piece_finished_alert`. The broker's alert thread marks the piece as had and wakes every reader waiting on the torrent.
3. The waiting reader asks for the piece's bytes (`read_piece`). libtorrent answers with a `read_piece_alert`; the alert thread keeps a copy for readers (at most 64 MB per torrent) and wakes the reader again.
4. The reader copies its range into mpv's buffer and returns. mpv demuxes and decodes it (on the GPU where it can) and draws the frame into `MpvVideoItem`.

Every read also moves the reader's position, and `Engine::schedule` re-plans from there: deadlines on the next 3 pieces, the read-ahead in order at normal priority, the file's first and last 4 MB early (headers and index), and spare bandwidth into a low-priority buffer. A seek is a read somewhere else; the plan follows it.

## Threads

| Thread | Runs |
|---|---|
| UI (main) | QML, `Session`, `Browser`, `Library`; mpv's events are moved here before `MpvTarget` emits them |
| mpv's own threads | demuxing, decoding, rendering; the stream thread blocks in `StreamHandle::read` |
| libtorrent's network and disk threads | peers, hashing, disk or RAM storage |
| broker alert thread | libtorrent's alerts, once-a-second upkeep (rates, peers, idle pause), RAM trimming and reloads, the cache cap |
| transport status thread | polls the broker twice a second for the "buffering 40%, 12 peers" line |
| storage worker | every SQLite query; the UI reads an in-memory copy |
| Qt's thread pool | folder scans, JSON parsing of add-on answers |

A `Torrent` record in the broker has one mutex and one condition variable. Readers wait on it; the alert thread and `schedule` change it under the same lock.

## Tests and benchmarks

Each layer has its own test in `tests/`, against fakes where it can: `FakeTarget` for the Session, a fake add-on server for sources, local libtorrent seeders (`TorrentHarness.h`) for the broker and transport. `tst_ui` loads the real window against a fake player and fails on any QML warning. `tst_bench` and `bench_player` measure stalls, seeks and memory against local seeders; [BROKER_CONTRACT.md](BROKER_CONTRACT.md) describes them. CI builds and tests on Windows, macOS and Linux, and once more on Linux under ASan and UBSan.
