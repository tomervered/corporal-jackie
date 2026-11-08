# Stream broker contract

The broker turns one file inside a torrent into bytes that a reader can read at any offset. It is the only code that calls libtorrent. It does not know who reads: mpv through a custom stream protocol in Step 4, an HTTP server for Cast later. Code: `src/broker/StreamBroker.h`.

## Opening

`open(request)` takes a magnet link, a 40-character info hash or a path to a `.torrent` file, plus an optional file index and the video's duration if known.

- If the torrent is new, it is added. If it is already loaded, the same torrent is reused and any peers and trackers in the new magnet link are added to it. A .torrent file for a torrent still waiting for metadata hands its metadata over. A torrent that had an error (a full disk, say) clears it and tries again.
- Torrents are named by their v1 info hash when they have one (the hash in a magnet link's `btih`), so callers can find them by it.
- The call blocks until the file list (metadata) arrives, the metadata timeout passes (60 s by default), or the caller's cancel check returns true. The file list is saved in the cache, so a torrent watched before opens without waiting for peers.
- File index -1 picks the largest file.
- The result is a handle. Each handle is one reader with its own position. Several handles can be open on one file, or on different files of one torrent.

Errors: `InvalidUri`, `MetadataTimeout`, `NoSuchFile`, `TorrentError`, `Cancelled`, `Closed`.

## Reading

`read(offset, buffer, length)` waits until the piece holding `offset` is downloaded and its hash checked, then copies bytes from it. It returns the number of bytes copied (never across a piece boundary), 0 at the end of the file, -1 if the handle was cancelled or closed, and -2 on an error.

A seek is just a read at a new offset. There is no separate seek call.

`cancel()` makes the waiting read and every later read return -1. A reader that goes away calls it, then `close()`.

## Which pieces download

Nothing downloads unless a reader needs it. When a torrent's file list arrives, every piece is set to "don't download". Then, for every open handle:

1. The first 4 MB and the last 4 MB of its file get a deadline of now. Players read the container's header and index (MP4 moov, MKV cues) before the first frame.
2. The pieces from the reader's position to the end of its read-ahead window get deadlines spaced by the file's bitrate: the piece the player needs in 10 seconds is due in 10 seconds.
3. After the window, the rest of the next ten minutes of video (at most 2 GB, and at most half the cache cap) downloads at low priority with no deadline, so spare bandwidth builds a buffer that rides out a slow patch.
4. Pieces that leave every window (after a seek, or when a handle closes) lose their deadline and go back to "don't download".

The read-ahead window is `readAheadBytes()` in `src/broker/BrokerTypes.h`: 30 seconds of video at the file's bitrate (size divided by duration, about 12 Mbit/s until the duration is known). It grows to 60 seconds when the swarm is more than 3 times faster than the video, and shrinks to 20 seconds when the swarm is slower than the video. It is never less than 4 pieces or more than 512 MB.

Deadlines are refreshed every second, since libtorrent counts them from the moment they are set. Sequential download mode is never used.

## Cache

Each torrent lives in its own folder under the cache folder, named by its info hash. After every open and close, and once a minute while anything plays, the broker adds up the disk space the cache really uses (sparse files count only the parts written). While that is over the cap, it deletes whole torrents, least recently used first. A torrent with an open handle is never deleted, and neither is the one being opened.

The cap is 10 GB by default, and smaller when the disk is short of space: the cache always leaves 2 GB free.

When the last handle on a torrent closes, and when the broker shuts down (waiting at most 3 s), libtorrent's resume data is saved as `resume.dat` in the torrent's folder. It records the file list and which pieces are verified, so a torrent watched before reopens at once: no wait for peers and no re-check of the files on disk.

The plan asked for least recently used pieces. libtorrent cannot forget single pieces it has verified, so the unit of eviction is the torrent. Because only watched parts of a file download, a torrent's folder holds roughly what was watched plus up to ten minutes ahead.

### RAM only

With `BrokerSettings::memoryOnly`, pieces are kept in memory and nothing from the video is written to disk (only each torrent's file list, a few KB, and its last-used time). The app turns it on by default to spare SSDs. It works through a libtorrent disk interface of our own (`MemoryStorage.h`).

Memory has its own cap (`memoryCapBytes`, 1 GB by default). Once a second, and whenever it is over, the broker drops verified pieces until memory is at 85% of the cap: first every piece of torrents nobody reads, then the pieces furthest from each reader, behind a reader before ahead of it. It never drops the pieces just behind a reader (an eighth of the cap, at most 64 MB), its read-ahead, or the first and last 4 MB of the file. The read-ahead and the background fill are limited to half the cap.

libtorrent cannot forget a piece it has verified. So when a reader needs a dropped piece again (a seek back), the broker removes the torrent and adds it again at once, telling libtorrent only the pieces still held, at most once every 2 s. Peers we connected to and the link's own peers carry over. In the harness, seeking back to a dropped part takes under 2 s; with a real swarm, add the time peers take to accept the new connection. Nothing is kept for the next run: no resume data is written.

## Stats

`stats()` on a handle gives the state (finding peers, fetching the file list, buffering, stalled, ready), peers, seeds, download and upload speed, how many bytes and seconds are verified in a row from the reader's position, and while buffering, how much of the piece the reader waits for has arrived.

Stalled means the piece the reader waits for got no new data for 10 seconds; data arriving for other pieces does not count. The first time a reader stalls, the log says which piece, how many blocks of it arrived, and how many connected peers have it. Every public torrent also announces to a short list of open trackers (`kPublicTrackers`), to every tracker in every tier. They are added only once the torrent's metadata says it is public, so a private torrent (BEP 27) is never announced to them, not even while its metadata is on its way. The broker keeps trying: when a torrent someone is reading has had no peers for 15 seconds, it asks the trackers and the DHT again.

`torrentStats(infoHash)` gives the same for a torrent that is still opening, before there is a handle.

Piece data handed to readers is kept in memory up to 64 MB per torrent, so pieces of 16 MB and more (common in 4K torrents) do not use hundreds of MB.

## Settings

`BrokerSettings`: cache folder and cap, RAM only and its cap, listen port, DHT, local peer discovery, UPnP/NAT-PMP, uTP, connection limit, download and upload limits, metadata timeout. Peer exchange is always on. The app's Settings panel (P) sets the storage options: RAM only and its cap, or the disk cap and cache folder. They apply the next time the app opens, since the engine reads them once.

## Testing

`tests/tst_broker.cpp` runs against a local swarm from `tests/TorrentHarness.h`: seeders on 127.0.0.1, found through `x.pe` peers in the magnet link. Seeders can be slowed down (a slow swarm) or hold only part of the file (a sparse swarm). The tests cover opening by magnet link and by cached info hash, reading whole files, a 96 MB file at about 50 Mbit/s with random seeks, two readers at once, downloading only the chosen file, cancelling a waiting read, metadata timeout, and the cache cap.

`CJ_TORRENT_LOG=1` with `QT_LOGGING_RULES="cj.broker.debug=true"` logs every libtorrent alert, including peer connections.
