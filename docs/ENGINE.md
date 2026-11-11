# The torrent engine

How the broker streams a file while it downloads, why it ships a patched libtorrent, and how to keep that patch alive. The broker's contract is in [BROKER_CONTRACT.md](BROKER_CONTRACT.md); this page is about what happens inside and what was measured.

## What the engine does

- The file being watched is downloaded in order (`sequential_download`). The piece the player is waiting for gets a deadline of zero; that happens when a torrent is opened, and when the player jumps to a different position.
- A read that crosses a piece boundary asks for the next piece too, so the player never waits for it from a cold start.
- The next seconds of video (sized from the bitrate and swarm speed, see `readAheadBytes`) are wanted at normal priority and arrive in order. Further ahead, up to ten minutes of video downloads at low priority, so spare bandwidth builds a buffer.
- Only the file being read is downloaded. Pieces of other files are skipped.

These choices live in `Engine::schedule` (`src/broker/BrokerScheduling.cpp`) and `StreamHandle::read`.

## Why libtorrent is patched

Four rate-limited seeders, one leaves four seconds in, the player reads at 8 MiB/s. Stock libtorrent 2.0 and 2.1 stall for a while after the departure, WebTorrent does not. The cause, found by tracing libtorrent's request queues and peer logs:

1. The number of block requests libtorrent keeps outstanding per peer starts small and grows by one for every block received ("slow start"). On a peer limited to a few MiB/s it never stops growing, so it reaches the remote peer's advertised request-queue length, usually 2000 blocks (about 30 MiB).
2. The setting that should cap this, `max_out_request_queue`, is overwritten by the remote peer's value from its extension handshake. A plugin cannot change it, because the setter is internal. `request_queue_time` only applies after slow start.
3. The peer's queue is a FIFO. When that peer leaves, libtorrent requests its blocks again from the survivors, at the back of their deep queues. The block the player needs next arrives about three seconds later.

Changing settings does not fix this. Tried without effect or with worse medians: smaller or larger `request_queue_time`, lower `max_out_request_queue`, wanting only the next N pieces, boosting then shrinking that window, no tail prefetch.

The patch (`third_party/libtorrent/0001-sequential-peer-departure.patch`) has two parts, both only for sequential downloads with more than one seed connected:

1. **Queue cap.** A peer that downloads slower than 8 MiB/s is asked for at most 24 blocks (384 KiB) at a time. This is applied where libtorrent computes the target, so the handshake value cannot raise it. A fast peer keeps libtorrent's own sizing, because its queue drains quickly and a cap would only limit its throughput. With one seed, nothing changes.
2. **Hole repair.** If a piece within eight positions after the first missing piece has already verified, the first missing piece is a hole in playback. A peer with room in its queue then asks for one backup copy of each block of that piece that has exactly one outstanding request. libtorrent's own duplicate handling cancels the loser.

The scheduling changes in the app go with the patch. Three deadline pieces ahead of the reader (the earlier design) cost about 10% of download speed, so the buffer never built up; one deadline plus in-order requests keeps the speed.

## Limits to know

- The cap bounds the throughput of a rate-limited peer to `24 × 16 KiB / round-trip time`: 7.5 MB/s at 50 ms and 3.7 MB/s at 100 ms. With several peers they add up, and a peer faster than 8 MiB/s is not capped.
- Only the first missing piece is repaired, and only after a later piece has finished. A departure that leaves no later piece complete is handled by libtorrent's normal timeouts.
- Everything is measured on loopback, and on loopback with 20 ms delay each way and 0.5% loss, with rate-limited libtorrent seeders. Other clients as seeders have not been measured.

## Building and updating the patch

`third_party/libtorrent/build.sh <prefix>` downloads the pinned release tarball (checksum checked), applies the patch with `git apply` and installs a static libtorrent into `<prefix>`. It records the version and the patch hash in `<prefix>/share/corporal-jackie/`. CMake looks for libtorrent in `CMAKE_PREFIX_PATH`, tells the build whether it is the patched one, and fails when `CJ_REQUIRE_PATCHED_LIBTORRENT` is on (CI sets it everywhere). The Windows and macOS packages link it statically and ship its license and the patch (`licenses/libtorrent`).

To move to a newer libtorrent:

1. Change `VERSION` and `SHA256` in `build.sh`.
2. Run the script. If `git apply` fails, check out the new tag, redo the two edits by hand (both are in `peer_connection.cpp` and `request_blocks.cpp`) and write the diff back to the patch file. Keep its header.
3. Run the tests and the benchmark below. A new version can change slow start or the picker; compare the stalled time before and after.

## Measuring

`tests/tst_bench.cpp` plays a 96 MiB file in a separate process (`bench_player`) from local seeders and records stalls, seeks, CPU, memory and bytes fetched. The "swarm" rows are the departure case. `tests/bench/competitor.cjs` runs the same rows against WebTorrent and other clients, and `tests/bench/summarize.py` makes the tables. CI runs the comparison on every push (`bench-compare`), and the ordinary `Benchmark` steps fail when the app itself stalls for more than 2.5 s after a seeder leaves. The measurements behind the patch, and the commands to repeat them, are in [tests/bench/peer-departure](../tests/bench/peer-departure/README.md).
