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
#include <map>

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
    QJsonObject runPlayer(const QString& magnet, const QString& cacheDir, qint64 ramCap)
    {
        QProcess p;
        p.setProcessChannelMode(QProcess::ForwardedErrorChannel);
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("QT_LOGGING_RULES"), QStringLiteral("cj.broker.info=false"));
        p.setProcessEnvironment(env);
        QStringList args{magnet, cacheDir};
        if (ramCap > 0)
            args << QString::number(ramCap);
        p.start(QStringLiteral(CJ_BENCH_PLAYER), args);
        if (!p.waitForFinished(240000)) {
            p.kill();
            p.waitForFinished();
            return {};
        }
        if (p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0)
            return {};
        const QByteArray out = p.readAllStandardOutput().trimmed();
        const QByteArray last = out.mid(out.lastIndexOf('\n') + 1);
        return QJsonDocument::fromJson(last).object();
    }

    void report(const char* mode, const QJsonObject& r)
    {
        std::map<QString, std::vector<qint64>> byKind;
        for (const QJsonValue v : r[QStringLiteral("seeks")].toArray()) {
            const QJsonObject s = v.toObject();
            byKind[s[QStringLiteral("kind")].toString()].push_back(s[QStringLiteral("ms")].toInteger());
        }
        qInfo().noquote() << QStringLiteral("BENCH %1: open %2 ms, first byte %3 ms, stalled %4 ms while playing (%5 stalls, longest %6 ms)")
                                 .arg(QLatin1String(mode))
                                 .arg(r[QStringLiteral("openMs")].toInteger())
                                 .arg(r[QStringLiteral("firstByteMs")].toInteger())
                                 .arg(r[QStringLiteral("playStallMs")].toInteger())
                                 .arg(r[QStringLiteral("playStalls")].toInteger())
                                 .arg(r[QStringLiteral("longestStallMs")].toInteger());
        for (const char* kind : {"cached", "missing", "evicted"})
            qInfo().noquote() << QStringLiteral("BENCH %1:   seek to %2: %3")
                                     .arg(QLatin1String(mode), QLatin1String(kind), msLine(byKind[QLatin1String(kind)]));
        qInfo().noquote() << QStringLiteral("BENCH %1:   peak memory: process %2 MiB, piece store %3 MiB, read cache %4 MiB")
                                 .arg(QLatin1String(mode))
                                 .arg(r[QStringLiteral("peakProcessBytes")].toInteger() / MiB)
                                 .arg(r[QStringLiteral("peakStoreBytes")].toInteger() / MiB)
                                 .arg(r[QStringLiteral("peakReadCacheBytes")].toInteger() / MiB);
    }

private slots:
    void diskAndRam()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const TestTorrent t = harness::makeTorrent(dir.filePath(QStringLiteral("seed")), QStringLiteral("movie.mkv"),
                                                   {kFileSize}, kPiece, 77);
        Seeder seeder(t, t.root, kSeederRate);
        const QString magnet = harness::magnet(t, {seeder.port()});

        const int runs = qMax(1, qEnvironmentVariableIntValue("CJ_BENCH_RUNS"));
        QFile save(qEnvironmentVariable("CJ_BENCH_OUT"));
        if (!save.fileName().isEmpty())
            QVERIFY(save.open(QIODevice::WriteOnly | QIODevice::Append));

        for (int run = 0; run < runs; ++run) {
            for (const bool ram : {false, true}) {
                // A fresh cache each time: every run starts from nothing.
                const QString cache = dir.filePath(QStringLiteral("cache-%1-%2").arg(run).arg(ram));
                const QJsonObject r = runPlayer(magnet, cache, ram ? kRamCap : 0);
                QVERIFY2(!r.isEmpty(), ram ? "RAM run failed" : "disk run failed");
                QCOMPARE(r[QStringLiteral("seeks")].toArray().size(), 20);
                report(ram ? "ram " : "disk", r);
                if (save.isOpen()) {
                    save.write(QJsonDocument(r).toJson(QJsonDocument::Compact));
                    save.write("\n");
                }
            }
        }
    }
};

QTEST_GUILESS_MAIN(TestBench)
#include "tst_bench.moc"
