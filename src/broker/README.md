# broker — Stream broker

Turns one file inside a torrent into seekable bytes: piece deadlines per reader, read-ahead sized from bitrate and swarm speed, and a disk cache with a cap. The only code allowed to call libtorrent, and it never knows who is reading. The contract is in [docs/BROKER_CONTRACT.md](../../docs/BROKER_CONTRACT.md).

- `BrokerTypes.h`: settings, errors, stats and the read-ahead formula. No libtorrent.
- `StreamBroker.h/.cpp`: the public side. `StreamBroker` opens torrents and hands out `StreamHandle`s.
- `BrokerEngine.h`: the inside, shared by the parts below: tuning constants, the `Torrent` and `Reader` records, and the `Engine` that owns the libtorrent session.
- `BrokerEngine.cpp`: the session, adding torrents, metadata, and the one thread that handles libtorrent's alerts.
- `BrokerScheduling.cpp`: which pieces to ask for and by when as readers move; in RAM-only mode, which pieces to drop.
- `BrokerUpkeep.cpp`: reloads, resume data, the disk cache cap, and the numbers the UI shows.
- `MemoryStorage.h/.cpp`: RAM-only piece storage.

libtorrent throws when called on a torrent removed a moment ago; every such call is guarded so it can never crash the app. The code builds against libtorrent 2.0 and 2.1 without deprecated calls; differences are behind `LIBTORRENT_VERSION_NUM`. Releases use libtorrent 2.1.2 with the patch in `third_party/libtorrent` ([docs/ENGINE.md](../../docs/ENGINE.md)).
