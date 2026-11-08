#pragma once
// A tiny local swarm for broker tests: makes test files and torrents, and runs
// seeders on 127.0.0.1. Seeders can be slowed down (a slow swarm) or hold only
// part of the file (a sparse swarm). Peers are passed to the broker in the
// magnet link (x.pe=127.0.0.1:port), so no tracker or DHT is needed.

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QString>

#include <libtorrent/create_torrent.hpp>
#include <libtorrent/load_torrent.hpp>
#include <libtorrent/ip_filter.hpp>
#include <libtorrent/magnet_uri.hpp>
#include <libtorrent/session.hpp>
#include <libtorrent/session_params.hpp>
#include <libtorrent/torrent_info.hpp>

#include <memory>
#include <random>
#include <thread>
#include <vector>

namespace harness {

// Deterministic bytes, so a test can check any offset without the file.
inline QByteArray testBytes(qint64 size, unsigned seed)
{
    QByteArray data(int(size), Qt::Uninitialized);
    std::mt19937 rng(seed);
    auto* words = reinterpret_cast<quint32*>(data.data());
    for (qint64 i = 0; i < size / 4; ++i)
        words[i] = rng();
    for (qint64 i = (size / 4) * 4; i < size; ++i)
        data[int(i)] = char(rng());
    return data;
}

inline void writeFile(const QString& path, const QByteArray& data)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate) || f.write(data) != data.size())
        qFatal("Could not write %s", qPrintable(path));
}

// A v1 torrent of `content` (a file or a folder) inside `root`, bencoded.
// `priv` marks it private (BEP 27); `tracker`, if given, is its own tracker.
inline std::vector<char> createTorrentFile(const QString& content, const QString& root, int pieceSize,
                                           bool priv = false, const std::string& tracker = {})
{
    const std::string path = QFile::encodeName(content).toStdString();
#if LIBTORRENT_VERSION_NUM >= 20100
    lt::create_torrent ct(lt::list_files(path), pieceSize, lt::create_torrent::v1_only);
#else
    lt::file_storage fs;
    lt::add_files(fs, path);
    lt::create_torrent ct(fs, pieceSize, lt::create_torrent::v1_only);
#endif
    ct.set_priv(priv);
    if (!tracker.empty())
        ct.add_tracker(tracker);
    lt::set_piece_hashes(ct, QFile::encodeName(root).toStdString());
    std::vector<char> buffer;
    lt::bencode(std::back_inserter(buffer), ct.generate());
    return buffer;
}

inline std::shared_ptr<lt::torrent_info> createTorrent(const QString& content, const QString& root, int pieceSize)
{
    return lt::load_torrent_buffer(createTorrentFile(content, root, pieceSize)).ti;
}

struct TestTorrent
{
    std::shared_ptr<lt::torrent_info> info;
    QString root;                 // the folder that holds the torrent's content
    std::vector<QByteArray> data; // each file's bytes, in the order of `sizes`

    // libtorrent orders files as the folder listing returns them, so look
    // files up by name.
    int indexOf(const QString& fileName) const
    {
#if LIBTORRENT_VERSION_NUM >= 20100
        const lt::file_storage& fs = info->layout();
#else
        const lt::file_storage& fs = info->files();
#endif
        for (int i = 0; i < fs.num_files(); ++i)
            if (QString::fromStdString(std::string(fs.file_name(lt::file_index_t(i)))) == fileName)
                return i;
        return -1;
    }
};

// Makes a torrent named `name` in `root`. One file: content is root/name.
// Several files: content is root/name/file0.bin, file1.bin, ...
inline TestTorrent makeTorrent(const QString& root, const QString& name, const std::vector<qint64>& sizes,
                               int pieceSize, unsigned seed)
{
    TestTorrent t;
    t.root = root;
    const QString content = QDir(root).filePath(name);
    for (std::size_t i = 0; i < sizes.size(); ++i) {
        t.data.push_back(testBytes(sizes[i], seed + unsigned(i)));
        const QString path = sizes.size() == 1 ? content
                                               : QDir(content).filePath(QStringLiteral("file%1.bin").arg(i));
        writeFile(path, t.data.back());
    }
    t.info = createTorrent(content, root, pieceSize);
    return t;
}

// Makes a one-file torrent of an existing file (a real video, so mpv can play
// it), copied to root/name.
inline TestTorrent makeTorrentOfFile(const QString& root, const QString& name, const QString& source,
                                     int pieceSize)
{
    TestTorrent t;
    t.root = root;
    QFile in(source);
    if (!in.open(QIODevice::ReadOnly))
        qFatal("Could not read %s", qPrintable(source));
    t.data.push_back(in.readAll());
    const QString content = QDir(root).filePath(name);
    writeFile(content, t.data.back());
    t.info = createTorrent(content, root, pieceSize);
    return t;
}

class Seeder
{
public:
    // `contentRoot` holds the files. With `checkFiles`, libtorrent hashes them
    // first, so a seeder whose files are partly zeroed has only some pieces.
    Seeder(const TestTorrent& torrent, const QString& contentRoot, int uploadLimit = 0, bool checkFiles = false)
    {
        lt::settings_pack pack;
        pack.set_str(lt::settings_pack::listen_interfaces, "127.0.0.1:0");
        pack.set_bool(lt::settings_pack::enable_dht, false);
        pack.set_bool(lt::settings_pack::enable_lsd, false);
        pack.set_bool(lt::settings_pack::enable_upnp, false);
        pack.set_bool(lt::settings_pack::enable_natpmp, false);
        pack.set_bool(lt::settings_pack::allow_multiple_connections_per_ip, true);
        pack.set_int(lt::settings_pack::alert_mask, 0);
        // Hand out upload bandwidth every 100 ms rather than 500 ms, closer to
        // a real link than bursts twice a second.
        pack.set_int(lt::settings_pack::tick_interval, 100);
        m_session = std::make_unique<lt::session>(lt::session_params(std::move(pack)));

        if (uploadLimit > 0) {
            // Local peers skip rate limits by default; put every address in a
            // class with its own limit instead.
            const lt::peer_class_t cls = m_session->create_peer_class("test-limit");
            lt::peer_class_info info = m_session->get_peer_class(cls);
            info.upload_limit = uploadLimit;
            m_session->set_peer_class(cls, info);
            lt::ip_filter filter;
            filter.add_rule(lt::make_address("0.0.0.0"), lt::make_address("255.255.255.255"),
                            1u << static_cast<std::uint32_t>(cls));
            m_session->set_peer_class_filter(filter);
        }

        lt::add_torrent_params atp;
        atp.ti = std::make_shared<lt::torrent_info>(*torrent.info);
        atp.save_path = QFile::encodeName(contentRoot).toStdString();
        if (!checkFiles)
            atp.flags |= lt::torrent_flags::seed_mode;
        atp.flags &= ~(lt::torrent_flags::auto_managed | lt::torrent_flags::paused);
        m_handle = m_session->add_torrent(std::move(atp));
    }

    // The listen socket opens in the background; wait for it.
    int port() const
    {
        for (int i = 0; i < 100 && m_session->listen_port() == 0; ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        return m_session->listen_port();
    }
    lt::torrent_status status() const { return m_handle.status(lt::torrent_handle::query_pieces); }

private:
    std::unique_ptr<lt::session> m_session;
    lt::torrent_handle m_handle;
};

inline QString magnet(const TestTorrent& t, const std::vector<int>& ports)
{
    lt::add_torrent_params atp;
    atp.ti = t.info;
    atp.name = t.info->name();   // the magnet link's dn=
    std::string uri = lt::make_magnet_uri(atp);
    for (int port : ports)
        uri += "&x.pe=127.0.0.1:" + std::to_string(port);
    return QString::fromStdString(uri);
}

inline QString infoHash(const TestTorrent& t)
{
    std::ostringstream s;
    s << t.info->info_hashes().get_best();
    return QString::fromStdString(s.str());
}

} // namespace harness
