// Torrent benchmark: a local seeder here, the player (bench/bench_player.cpp)
// in its own process, on disk and in RAM. Prints the numbers; it checks only
// that every run finished, since the spread between runs sets any threshold.
//
// CJ_BENCH_RUNS=n repeats each mode n times (default 1). CJ_BENCH_OUT=file
// saves every run's JSON, one per line.

#include "TorrentHarness.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>
#include <QtTest>

#include <algorithm>
#include <cstdio>
#include <map>
#include <memory>

using harness::Seeder;
using harness::TestTorrent;

namespace {

constexpr qint64 MiB = 1024 * 1024;
constexpr qint64 kFileSize = 96 * MiB;
constexpr int kPiece = 1024 * 1024;       // as real torrents of this size use
constexpr int kSeederRate = 16 * 1024 * 1024;  // bytes/s, twice the play rate
constexpr qint64 kRamCap = 32 * MiB;      // small, so played pieces get dropped

QString msLine(std::vector<qint64> v)
{
    if (v.empty())
        return QStringLiteral("-");
    std::sort(v.begin(), v.end());
    return QStringLiteral("median %1 ms, worst %2 ms (%3)").arg(v[v.size() / 2]).arg(v.back()).arg(v.size());
}

} // namespace

class TestBench : public QObject
{
    Q_OBJECT

private:
    // Runs the player. On failure, shows the end of its log.
    QJsonObject runPlayer(const QString& magnet, const QString& cacheDir, qint64 ramCap)
    {
        QProcess p;
        QStringList args{magnet, cacheDir};
        if (ramCap > 0)
            args << QString::number(ramCap);
        // Its log goes to stderr even when that is not a terminal (Qt may
        // pick the system log otherwise), to be shown if the run fails.
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("QT_FORCE_STDERR_LOGGING"), QStringLiteral("1"));
        p.setProcessEnvironment(env);
        p.start(QStringLiteral(CJ_BENCH_PLAYER), args);
        const bool finished = p.waitForFinished(240000);
        if (!finished) {
            p.kill();
            p.waitForFinished();
        }
        const QByteArray out = p.readAllStandardOutput().trimmed();
        if (!finished || p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0) {
            const QList<QByteArray> log = p.readAllStandardError().split('\n');
            QByteArray tail;
            for (qsizetype i = qMax<qsizetype>(0, log.size() - 60); i < log.size(); ++i)
                tail += log[i] + '\n';
            print(QStringLiteral("bench_player %1 (exit code %2). Its last log lines:\n%3")
                      .arg(finished ? QStringLiteral("failed") : QStringLiteral("timed out"))
                      .arg(p.exitCode())
                      .arg(QString::fromLocal8Bit(tail)));
            return {};
        }
        const QByteArray last = out.mid(out.lastIndexOf('\n') + 1);
        return QJsonDocument::fromJson(last).object();
    }

    // To stderr, flushed: on Windows CI, stdout of a test does not reach the log.
    static void print(const QString& line)
    {
        std::fprintf(stderr, "%s\n", line.toLocal8Bit().constData());
        std::fflush(stderr);
    }

    void report(const char* mode, const QJsonObject& r)
    {
        std::map<QString, std::vector<qint64>> byKind;
        for (const QJsonValue v : r[QStringLiteral("seeks")].toArray()) {
            const QJsonObject s = v.toObject();
            byKind[s[QStringLiteral("kind")].toString()].push_back(s[QStringLiteral("ms")].toInteger());
        }
        print(QStringLiteral("BENCH %1: open %2 ms, first byte %3 ms, stalled %4 ms while playing (%5 stalls, longest %6 ms)")
                                 .arg(QLatin1String(mode))
                                 .arg(r[QStringLiteral("openMs")].toInteger())
                                 .arg(r[QStringLiteral("firstByteMs")].toInteger())
                                 .arg(r[QStringLiteral("playStallMs")].toInteger())
                                 .arg(r[QStringLiteral("playStalls")].toInteger())
                                 .arg(r[QStringLiteral("longestStallMs")].toInteger()));
        for (const char* kind : {"cached", "missing", "evicted"})
            print(QStringLiteral("BENCH %1:   seek to %2: %3")
                                     .arg(QLatin1String(mode), QLatin1String(kind), msLine(byKind[QLatin1String(kind)])));
        print(QStringLiteral("BENCH %1:   peak memory: process %2 MiB, piece store %3 MiB, read cache %4 MiB")
                                 .arg(QLatin1String(mode))
                                 .arg(r[QStringLiteral("peakProcessBytes")].toInteger() / MiB)
                                 .arg(r[QStringLiteral("peakStoreBytes")].toInteger() / MiB)
                                 .arg(r[QStringLiteral("peakReadCacheBytes")].toInteger() / MiB));
    }

private slots:
    // One seeder for every run.
    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
        m_torrent = harness::makeTorrent(m_dir.filePath(QStringLiteral("seed")), QStringLiteral("movie.mkv"),
                                         {kFileSize}, kPiece, 77);
        m_seeder = std::make_unique<Seeder>(m_torrent, m_torrent.root, kSeederRate);
        m_magnet = harness::magnet(m_torrent, {m_seeder->port()});
    }

    void cleanupTestCase() { m_seeder.reset(); }

    // A row per run, so each gets Qt's per-function time limit.
    void run_data()
    {
        QTest::addColumn<bool>("ram");
        const int runs = qMax(1, qEnvironmentVariableIntValue("CJ_BENCH_RUNS"));
        for (int i = 1; i <= runs; ++i) {
            QTest::addRow("disk %d", i) << false;
            QTest::addRow("ram %d", i) << true;
        }
    }

    void run()
    {
        QFETCH(bool, ram);
        // A fresh cache each time: every run starts from nothing.
        const QString cache = m_dir.filePath(QStringLiteral("cache-%1").arg(++m_runs));
        const QJsonObject r = runPlayer(m_magnet, cache, ram ? kRamCap : 0);
        QVERIFY2(!r.isEmpty(), ram ? "RAM run failed" : "disk run failed");
        QCOMPARE(r[QStringLiteral("seeks")].toArray().size(), 20);
        report(ram ? "ram " : "disk", r);
        QFile save(qEnvironmentVariable("CJ_BENCH_OUT"));
        if (!save.fileName().isEmpty() && save.open(QIODevice::WriteOnly | QIODevice::Append)) {
            save.write(QJsonDocument(r).toJson(QJsonDocument::Compact));
            save.write("\n");
        }
    }

private:
    QTemporaryDir m_dir;
    TestTorrent m_torrent;
    std::unique_ptr<Seeder> m_seeder;
    QString m_magnet;
    int m_runs = 0;
};

QTEST_GUILESS_MAIN(TestBench)
#include "tst_bench.moc"
