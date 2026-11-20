// Download benchmark: how long a whole 96 MiB file takes to download with
// Corporal Jackie's two download modes and with other torrent clients, from
// the same local seeders. Three swarms: one seeder at 16 MB/s, four at
// 3 MB/s each with one leaving after 4 s, and one with no limit.
//
// Every client finds the seeders through the same HTTP tracker named in a
// magnet link (no DHT, LSD, PEX or uTP), and every saved file is checked
// byte for byte. Other clients run through tests/bench/download_competitor.cjs.
//
// CJ_BENCH_RUNS=n repeats each row n times (default 3). CJ_BENCH_CLIENTS
// picks the rows (default: all, skipping clients that are not installed).
// CJ_BENCH_OUT=file appends one JSON line per run.

#include "Downloads.h"
#include "Preferences.h"
#include "Storage.h"
#include "TorrentHarness.h"
#include "TorrentTransport.h"

#include <QDirIterator>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QtTest>

#include <algorithm>
#include <cstdio>
#include <map>
#include <memory>
#include <random>

using namespace cj;
using harness::Seeder;
using harness::TestTorrent;

namespace {

constexpr qint64 MiB = 1024 * 1024;
constexpr qint64 kFileSize = 96 * MiB;
constexpr int kPiece = 1024 * 1024;

void print(const QString& line)
{
    std::fprintf(stderr, "%s\n", line.toLocal8Bit().constData());
    std::fflush(stderr);
}

// An HTTP tracker that names the seeders it is told about.
class Tracker : public QObject
{
public:
    Tracker()
    {
        m_server.listen(QHostAddress::LocalHost, 0);
        connect(&m_server, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket* s = m_server.nextPendingConnection()) {
                connect(s, &QTcpSocket::readyRead, s, [this, s] {
                    const QByteArray head = s->peek(4096);
                    if (!head.contains("\r\n\r\n"))
                        return;
                    ++announces;
                    // Compact (6 bytes a peer) when asked; some clients take only that.
                    QByteArray body;
                    if (head.contains("compact=1")) {
                        QByteArray peers;
                        for (int port : ports)
                            peers += QByteArray("\x7f\x00\x00\x01", 4) + char((port >> 8) & 0xff) + char(port & 0xff);
                        body = "d8:intervali30e12:min intervali5e5:peers" + QByteArray::number(peers.size()) + ":" + peers + "e";
                    } else {
                        QByteArray peers;
                        for (int port : ports)
                            peers += "d2:ip9:127.0.0.14:porti" + QByteArray::number(port) + "ee";
                        body = "d8:intervali30e12:min intervali5e5:peersl" + peers + "ee";
                    }
                    s->write("HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: " + QByteArray::number(body.size())
                             + "\r\nConnection: close\r\n\r\n" + body);
                    s->disconnectFromHost();
                });
                connect(s, &QTcpSocket::disconnected, s, &QObject::deleteLater);
            }
        });
    }
    QString url() const { return QStringLiteral("http://127.0.0.1:%1/announce").arg(m_server.serverPort()); }
    std::vector<int> ports;
    int announces = 0;

private:
    QTcpServer m_server;
};

struct Swarm
{
    QString name;
    int seeders;
    int rate;        // bytes/s each, 0 = no limit
    int leaveMs;     // one seeder leaves this long after the start; 0 = none
};

QString fileUnder(const QString& folder, const QString& name)
{
    QDirIterator it(folder, {name}, QDir::Files, QDirIterator::Subdirectories);
    return it.hasNext() ? it.next() : QString();
}

bool sameAs(const QString& path, const QByteArray& data)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) && f.size() == data.size() && f.readAll() == data;
}

QString median(std::vector<qint64> v)
{
    if (v.empty())
        return QStringLiteral("-");
    std::sort(v.begin(), v.end());
    return QString::number(v[v.size() / 2]);
}

} // namespace

class TestBenchDownload : public QObject
{
    Q_OBJECT

    // Downloads with Corporal Jackie in this process, as the app does.
    qint64 runJackie(const QString& magnet, const QString& root, bool normal, const std::function<void()>& tick)
    {
        Storage storage(QDir(root).filePath(QStringLiteral("db.sqlite")));
        Preferences preferences(storage, QDir(root).filePath(QStringLiteral("cache")));
        preferences.choosePrivacy(false, false, false, false, true, false);
        BrokerSettings s;
        s.cacheDir = QDir(root).filePath(QStringLiteral("cache"));
        s.loopbackOnly = true;
        s.upload = false;
        TorrentTransport transport(s);
        Downloads downloads(storage, preferences, &transport, QDir(root).filePath(QStringLiteral("out")));
        downloads.setNetwork(s);
        downloads.setNormalTorrents(normal);
        QElapsedTimer clock;
        clock.start();
        PlayableSource source;
        source.url = QUrl(magnet);
        source.kind = PlayableSource::Kind::Torrent;
        source.title = QStringLiteral("bench");
        if (!downloads.start(source).isEmpty())
            return -1;
        const auto state = [&] { return downloads.items().value(0).toMap().value(QStringLiteral("state")).toString(); };
        const bool done = QTest::qWaitFor([&] {
            tick();
            const QString st = state();
            return st == QLatin1String("done") || st == QLatin1String("seeding") || st == QLatin1String("failed");
        }, 280000);
        const qint64 ms = clock.elapsed();
        return done && state() != QLatin1String("failed") ? ms : -1;
    }

    // Another client, in its own process.
    qint64 runOther(const QString& client, const QString& magnet, const QString& root, const std::function<void()>& tick)
    {
        QProcess p;
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("CJ_COMPETITOR"), client);
        // The tracker is on this computer: no proxy (libcurl in Transmission
        // would send the announce to one).
        for (const char* name : {"http_proxy", "https_proxy", "HTTP_PROXY", "HTTPS_PROXY", "all_proxy", "ALL_PROXY"})
            env.remove(QString::fromLatin1(name));
        env.insert(QStringLiteral("NO_PROXY"), QStringLiteral("127.0.0.1,localhost"));
        p.setProcessEnvironment(env);
        p.start(QStringLiteral("node"), {QStringLiteral(CJ_BENCH_DIR "/download_competitor.cjs"), magnet,
                                         QDir(root).filePath(QStringLiteral("out"))});
        const bool finished = QTest::qWaitFor([&] {
            tick();
            return p.state() == QProcess::NotRunning;
        }, 290000);
        if (!finished) {
            p.kill();
            p.waitForFinished();
        }
        const QByteArray out = p.readAllStandardOutput().trimmed();
        if (!finished || p.exitCode() != 0) {
            print(QStringLiteral("%1 failed: %2").arg(client, QString::fromLocal8Bit(p.readAllStandardError().right(2000))));
            return -1;
        }
        return QJsonDocument::fromJson(out.mid(out.lastIndexOf('\n') + 1)).object().value(QStringLiteral("ms")).toInteger(-1);
    }

private slots:
    void downloads()
    {
        // Transmission (4.0.5) is left out unless asked for: it gets the
        // seeders from the tracker but never dials them on this computer, so
        // it times out every run.
        QStringList clients{"jackie-stream", "jackie-normal", "aria2", "qbittorrent", "webtorrent"};
        if (qEnvironmentVariableIsSet("CJ_BENCH_CLIENTS"))
            clients = qEnvironmentVariable("CJ_BENCH_CLIENTS").split(QLatin1Char(','), Qt::SkipEmptyParts);
        const std::map<QString, QString> programs{{"aria2", "aria2c"}, {"transmission", "transmission-cli"},
                                                  {"qbittorrent", "qbittorrent-nox"}, {"webtorrent", "node"}};
        clients.erase(std::remove_if(clients.begin(), clients.end(), [&](const QString& c) {
            const auto it = programs.find(c);
            const bool missing = it != programs.end() && QStandardPaths::findExecutable(it->second).isEmpty();
            if (missing)
                print(QStringLiteral("BENCH skipping %1: %2 is not installed").arg(c, it->second));
            return missing;
        }), clients.end());
        const int runs = qMax(1, qEnvironmentVariableIntValue("CJ_BENCH_RUNS") ? qEnvironmentVariableIntValue("CJ_BENCH_RUNS") : 3);
        const std::vector<Swarm> swarms{
            {QStringLiteral("one seeder, 16 MB/s"), 1, 16 * 1024 * 1024, 0},
            {QStringLiteral("four seeders, 3 MB/s each, one leaves at 4 s"), 4, 3 * 1024 * 1024, 4000},
            {QStringLiteral("one seeder, no limit"), 1, 0, 0},
        };

        QTemporaryDir dir;
        const TestTorrent t = harness::makeTorrent(dir.filePath("seed"), "bench.bin", {kFileSize}, kPiece, 42);
        Tracker tracker;
        QFile log(qEnvironmentVariable("CJ_BENCH_OUT"));
        if (!log.fileName().isEmpty())
            QVERIFY(log.open(QIODevice::Append));
        std::mt19937 shuffle(7);

        std::map<std::pair<QString, QString>, std::vector<qint64>> results;
        for (const Swarm& swarm : swarms) {
            for (int run = 0; run < runs; ++run) {
                QStringList order = clients;
                std::shuffle(order.begin(), order.end(), shuffle);
                for (const QString& client : order) {
                    std::vector<std::unique_ptr<Seeder>> seeders;
                    tracker.ports.clear();
                    for (int i = 0; i < swarm.seeders; ++i) {
                        seeders.push_back(std::make_unique<Seeder>(t, t.root, swarm.rate));
                        tracker.ports.push_back(seeders.back()->port());
                    }
                    const QString magnet = QStringLiteral("magnet:?xt=urn:btih:%1&dn=bench.bin&tr=%2")
                                               .arg(harness::infoHash(t), QString::fromLatin1(QUrl::toPercentEncoding(tracker.url())));
                    QTemporaryDir root;
                    QElapsedTimer clock;
                    clock.start();
                    const auto tick = [&] {
                        if (swarm.leaveMs > 0 && seeders.size() == std::size_t(swarm.seeders) && clock.elapsed() >= swarm.leaveMs)
                            seeders.pop_back();   // gone: its connections drop
                    };
                    qint64 ms = client.startsWith(QLatin1String("jackie"))
                        ? runJackie(magnet, root.path(), client == QLatin1String("jackie-normal"), tick)
                        : runOther(client, magnet, root.path(), tick);
                    const QString saved = fileUnder(root.filePath("out"), QStringLiteral("bench.bin"));
                    if (ms >= 0 && !sameAs(saved, t.data[0])) {
                        print(QStringLiteral("BENCH %1: the saved file is wrong or missing (%2)").arg(client, saved));
                        ms = -1;
                    }
                    print(QStringLiteral("BENCH %1 | %2 | run %3: %4").arg(swarm.name, client).arg(run + 1)
                              .arg(ms < 0 ? QStringLiteral("failed") : QStringLiteral("%1 ms, %2 MB/s").arg(ms)
                                                                    .arg(double(kFileSize) / MiB / (double(ms) / 1000), 0, 'f', 1)));
                    if (ms >= 0)
                        results[{swarm.name, client}].push_back(ms);
                    if (log.isOpen())
                        log.write(QJsonDocument(QJsonObject{{"swarm", swarm.name}, {"client", client}, {"run", run + 1}, {"ms", ms}})
                                      .toJson(QJsonDocument::Compact) + '\n');
                }
            }
        }
        print(QStringLiteral("\nBENCH summary: median seconds to download 96 MiB (runs that finished / runs)"));
        for (const Swarm& swarm : swarms) {
            print(QStringLiteral("BENCH %1").arg(swarm.name));
            for (const QString& client : clients) {
                const auto& v = results[{swarm.name, client}];
                print(QStringLiteral("BENCH   %1 %2 s  (%3/%4)").arg(client, -14)
                          .arg(v.empty() ? QStringLiteral("-") : QString::number(median(v).toDouble() / 1000, 'f', 2))
                          .arg(v.size()).arg(runs));
            }
        }
    }
};

QTEST_MAIN(TestBenchDownload)
#include "tst_benchdownload.moc"
