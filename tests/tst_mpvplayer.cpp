// Integration test for the mpv target: real mpv, no video or audio output.
// Plays a tiny bundled test clip (tests/data, made with FFmpeg's testsrc), so it
// needs no GPU.

#include "MpvPlayer.h"

#include <QSignalSpy>
#include <QtTest>
#include <clocale>

using cj::MpvPlayer;

namespace {
// 10 seconds, 160x120, 25 fps, a keyframe every second.
const QString kTestPattern = QStringLiteral(CJ_TEST_DATA_DIR "/testsrc-10s.mkv");
}

class TestMpvPlayer : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase() { std::setlocale(LC_NUMERIC, "C"); }

    void loadsAndReportsDuration()
    {
        MpvPlayer player(MpvPlayer::Mode::Headless);
        player.openPath(kTestPattern);
        QTRY_VERIFY_WITH_TIMEOUT(player.fileLoaded(), 10000);
        QTRY_COMPARE_WITH_TIMEOUT(qRound(player.duration()), 10, 5000);
        QTRY_COMPARE_WITH_TIMEOUT(player.videoWidth(), 160, 5000);
        QCOMPARE(player.videoHeight(), 120);
    }

    void pausesAndSeeks()
    {
        MpvPlayer player(MpvPlayer::Mode::Headless);
        player.openPath(kTestPattern);
        QTRY_VERIFY_WITH_TIMEOUT(player.fileLoaded(), 10000);

        player.setPaused(true);
        QTRY_VERIFY_WITH_TIMEOUT(player.paused(), 5000);

        player.seek(6.0);
        QTRY_VERIFY_WITH_TIMEOUT(qAbs(player.position() - 6.0) < 0.5, 5000);

        player.seekRelative(-2.0);
        QTRY_VERIFY_WITH_TIMEOUT(qAbs(player.position() - 4.0) < 0.5, 5000);

        player.togglePause();
        QTRY_VERIFY_WITH_TIMEOUT(!player.paused(), 5000);
    }

    void volumeIsClamped()
    {
        MpvPlayer player(MpvPlayer::Mode::Headless);
        player.setVolume(150);
        QTRY_COMPARE_WITH_TIMEOUT(player.volume(), 100.0, 5000);
        player.setVolume(30);
        QTRY_COMPARE_WITH_TIMEOUT(player.volume(), 30.0, 5000);
    }

    void missingFileIsATypedError()
    {
        MpvPlayer player(MpvPlayer::Mode::Headless);
        QSignalSpy errors(&player, &MpvPlayer::errorOccurred);
        player.openPath(QStringLiteral("/definitely/not/here.mkv"));
        QTRY_COMPARE_WITH_TIMEOUT(errors.count(), 1, 10000);
        QCOMPARE(errors.first().first().value<MpvPlayer::Error>(), MpvPlayer::Error::LoadFailed);
        QVERIFY(!player.fileLoaded());
    }

    void reusesOneInstanceForManyItems()
    {
        MpvPlayer player(MpvPlayer::Mode::Headless);
        for (int i = 0; i < 3; ++i) {
            player.openPath(kTestPattern);
            QTRY_VERIFY_WITH_TIMEOUT(player.fileLoaded(), 10000);
        }
    }
};

QTEST_GUILESS_MAIN(TestMpvPlayer)
#include "tst_mpvplayer.moc"
