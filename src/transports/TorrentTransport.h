#pragma once
// Plays torrents in mpv with no local HTTP server: registers a "torrent://"
// protocol through mpv's stream callback API and answers mpv's open, read,
// seek, size, cancel and close calls from the stream broker.
//
// Stream URLs are torrent://<info hash>/<file index>, or torrent://<info hash>
// for the largest file. The two parts match the future HTTP path
// /stream/<info hash>/<file index>, so the Cast transport can reuse them.

#include "BrokerTypes.h"
#include "PlaybackTypes.h"

#include <QHash>
#include <QObject>
#include <QTimer>
#include <QUrl>

#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

struct mpv_handle;
struct mpv_stream_cb_info;

namespace cj {

class StreamBroker;
class StreamHandle;

class TorrentTransport : public QObject
{
    Q_OBJECT

public:
    explicit TorrentTransport(const BrokerSettings& settings, QObject* parent = nullptr);
    ~TorrentTransport() override;

    // Adds the torrent:// protocol to an initialized mpv instance. The
    // transport must outlive that instance.
    bool registerWith(mpv_handle* mpv);

    // A magnet link, a 40-character info hash, a .torrent file or a
    // torrent:// URL becomes a playable torrent:// source. Anything else:
    // nothing. Remembers the magnet's trackers and peers for the open.
    std::optional<PlayableSource> resolve(const QString& input);

    // The item the player is on now. Opens and reads for any other torrent
    // stop waiting at once (an item switch or stop mid-buffer).
    void setCurrent(const QUrl& url);

    // Makes every open and read give up now. Call before mpv is destroyed:
    // mpv waits for its open streams, and those may be waiting for peers.
    void abort();

    // The video's real length, once the player knows it: sharpens the read-ahead.
    void setDuration(double seconds);

    // Started on first use, so people who never open a torrent never get a
    // firewall prompt or an open port.
    StreamBroker& broker();

    static QString infoHashOf(const QUrl& torrentUrl);

signals:
    // Twice a second while a torrent is the current item.
    void statusChanged(const cj::StreamStatus& status);

private:
    struct Stream;
    static int openCallback(void* self, char* uri, mpv_stream_cb_info* info);
    int open(const char* uri, mpv_stream_cb_info* info);
    void pollStatus();

    BrokerSettings m_settings;
    std::mutex m_brokerMutex;
    std::shared_ptr<StreamBroker> m_broker;
    QTimer m_statusTimer;

    mutable std::mutex m_mutex;  // guards everything below
    QHash<QString, QString> m_requests;               // info hash -> what the user gave (magnet...)
    QString m_currentHash;
    double m_duration = 0;
    std::vector<std::weak_ptr<StreamHandle>> m_handles;
    QHash<QString, int> m_opening;                     // info hash -> opens in progress
    std::atomic<quint64> m_generation{0};
    std::atomic<bool> m_closing{false};
};

} // namespace cj
