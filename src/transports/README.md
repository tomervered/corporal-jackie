# transports — Transports

Gets a stream's bytes to the player. No torrent logic here: that is the broker's job.

## TorrentTransport (Step 4)

Plays torrents in mpv with no local HTTP server. It registers a `torrent://` protocol with mpv's stream callback API (`mpv_stream_cb_add_ro`) and maps mpv's callbacks one to one onto a broker `StreamHandle`:

| mpv callback | Broker |
|---|---|
| open | `StreamBroker::open` (blocks until the file list is here) |
| read | `StreamHandle::read` at the stream's position |
| seek | sets the position; the next read moves the download window |
| size | `StreamHandle::size` |
| cancel | `StreamHandle::cancel`, so a waiting read returns at once |
| close | closes the handle |

URLs are `torrent://<info hash>/<file index>`, or `torrent://<info hash>` for the largest file. The two parts are the same as the future HTTP path `/stream/<info hash>/<file index>`, so the Cast transport can reuse stream IDs unchanged.

Rules:

- The callbacks run on mpv's threads and never call libmpv, which would deadlock.
- `resolve()` turns a magnet link, an info hash, a `.torrent` file or a `torrent://` URL into a playable source, and remembers the magnet's trackers and peers for the open. The app registers it with the Session as a source resolver.
- `setCurrent()` follows the Session's item. An open or read for any other torrent gives up at once, so switching items or stopping mid-buffer never waits on a torrent nobody watches any more.
- `abort()` runs when the app quits, before mpv is destroyed: mpv waits for its streams, and those may still be looking for peers.
- Twice a second while a torrent is current, `statusChanged` reports finding peers, getting the file list, buffering (with a percentage), stalled or ready, plus peers, speed and seconds buffered ahead. The Session exposes these to the UI. The polling runs on its own thread, because libtorrent's status calls wait on its network thread and must not stall the UI.
- `openFailed` gives the reason when a torrent cannot play: no peers found in time (`NoPeers`) or a torrent error such as a full disk (`StreamFailed`), at open or mid-play. The Session shows it instead of mpv's vaguer "could not open".
- The broker starts on first use, so someone who never opens a torrent gets no firewall prompt and no open port.

Tests: `tests/tst_torrentstream.cpp` plays a real clip from local seeders through the Session and a headless mpv: magnet to the end, seeking after a long pause, switching away from a torrent with no peers, a stall, and switching or stopping while mpv waits for data.
