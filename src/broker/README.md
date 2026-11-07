# broker — Stream broker

Turns one file inside a torrent into seekable bytes: piece deadlines per reader, read-ahead sized from bitrate and swarm speed, and a disk cache with a cap. The only code allowed to call libtorrent, and it never knows who is reading. The contract is in [docs/BROKER_CONTRACT.md](../../docs/BROKER_CONTRACT.md).

- `BrokerTypes.h`: settings, errors, stats and the read-ahead formula. No libtorrent.
- `StreamBroker`: opens torrents and hands out `StreamHandle`s; one thread handles libtorrent's alerts. libtorrent throws when called on a torrent removed a moment ago; every such call is guarded so it can never crash the app.
