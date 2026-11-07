#pragma once
// Plain types shared by the stream broker and its callers. No libtorrent here,
// so the transports layer can include this without pulling libtorrent in.

#include <QString>
#include <QtGlobal>
#include <algorithm>

namespace cj {

struct BrokerSettings
{
    QString cacheDir;                        // one subfolder per torrent
    qint64 cacheCapBytes = 10LL * 1024 * 1024 * 1024;
    int listenPort = 0;                      // 0 = let the system pick
    bool dht = true;                         // distributed hash table
    bool localDiscovery = true;              // find peers on the LAN
    bool portMapping = true;                 // UPnP and NAT-PMP
    bool utp = true;                         // uTP as well as TCP
    int connectionsLimit = 200;
    int downloadLimit = 0;                   // bytes/s, 0 = unlimited
    int uploadLimit = 0;                     // bytes/s, 0 = unlimited
    int metadataTimeoutMs = 60000;
    // Tests only: listen on 127.0.0.1 and use no DHT, LAN discovery or port mapping.
    bool loopbackOnly = false;
};

enum class BrokerError {
    None,
    InvalidUri,        // not a magnet link, info hash or .torrent file
    MetadataTimeout,   // no peer sent the file list in time
    NoSuchFile,        // the file index is not in this torrent
    TorrentError,      // libtorrent reported an error (disk full, ...)
    Cancelled,
    Closed,            // the broker is shutting down
};

enum class StreamState {
    FindingPeers,      // no metadata and no peers yet
    FetchingMetadata,  // peers found, waiting for the file list
    Buffering,         // the bytes at the read position are not here yet
    Ready,             // the bytes at the read position are here
};

struct StreamStats
{
    StreamState state = StreamState::FindingPeers;
    int peers = 0;
    int seeds = 0;
    qint64 downloadRate = 0;      // bytes/s
    qint64 uploadRate = 0;        // bytes/s
    qint64 bufferedBytes = 0;     // verified bytes in a row from the read position
    double bufferedSeconds = 0;   // the same, at the file's bitrate
};

// Bytes per second assumed when the duration is not known yet (about 12 Mbit/s).
constexpr double kDefaultBitrate = 1.5 * 1024 * 1024;

// How far ahead of a reader to set piece deadlines. Based on how many bytes
// per second the video needs (file size / duration) and how fast the swarm is:
// a fast swarm can look a minute ahead; a slow one keeps its bandwidth on the
// next 20 seconds. Never less than 4 pieces, never more than 512 MB.
inline qint64 readAheadBytes(double bitrate, double downloadRate, qint64 pieceLength)
{
    if (bitrate <= 0)
        bitrate = kDefaultBitrate;
    double seconds = 30;
    if (downloadRate > 3 * bitrate)
        seconds = 60;
    else if (downloadRate > 0 && downloadRate < bitrate)
        seconds = 20;
    const qint64 bytes = qint64(bitrate * seconds);
    return std::clamp(bytes, 4 * pieceLength, qMax(4 * pieceLength, 512LL * 1024 * 1024));
}

} // namespace cj
