// Integration test for the mpv target: real mpv, no video or audio output.
// Plays a tiny bundled test clip (tests/data, made with FFmpeg's testsrc), so it
// needs no GPU. Everything is checked through the Session, the way the UI sees it.

#include "MpvTarget.h"
#include "Session.h"

#include <QSignalSpy>
#include <QtTest>
#include <clocale>

using namespace cj;

namespace {
// 10 seconds, 160x120, 25 fps, a keyframe every second.
const QString kTestPattern = QStringLiteral(CJ_TEST_DATA_DIR "/testsrc-10s.mkv");
}

class TestMpvTarget : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase() { std::setlocale(LC_NUMERIC, "C"); }

    void loadsAndReportsDuration()
    {
        MpvTarget target(MpvTarget::Mode::Headless);
        Session s(&target);
        s.openPath(kTestPattern);
        QTRY_VERIFY_WITH_TIMEOUT(s.hasMedia(), 10000);
        QTRY_COMPARE_WITH_TIMEOUT(qRound(s.duration()), 10, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(s.stats().value(QStringLiteral("Resolution")).toString().startsWith("160"), 5000);
        QCOMPARE(s.title(), QStringLiteral("testsrc-10s"));
    }

    void pausesAndSeeks()
    {
        MpvTarget target(MpvTarget::Mode::Headless);
        Session s(&target);
        s.openPath(kTestPattern);
        QTRY_VERIFY_WITH_TIMEOUT(s.hasMedia(), 10000);

        s.pause();
        QTRY_VERIFY_WITH_TIMEOUT(s.paused(), 5000);

        s.seek(6.0);
        QTRY_VERIFY_WITH_TIMEOUT(qAbs(s.position() - 6.0) < 0.5, 5000);
        s.seekRelative(-2.0);
        QTRY_VERIFY_WITH_TIMEOUT(qAbs(s.position() - 4.0) < 0.5, 5000);

        s.togglePause();
        QTRY_COMPARE_WITH_TIMEOUT(s.state(), PlaybackState::Playing, 5000);
    }

    void startsAtResumePosition()
    {
        MpvTarget target(MpvTarget::Mode::Headless);
        Session s(&target);
        s.openPath(kTestPattern, 5);
        QTRY_VERIFY_WITH_TIMEOUT(s.hasMedia(), 10000);
        QTRY_VERIFY_WITH_TIMEOUT(s.position() >= 4.5, 5000);
    }

    void reportsTracks()
    {
        MpvTarget target(MpvTarget::Mode::Headless);
        Session s(&target);
        QSignalSpy tracks(&s, &Session::tracksChanged);
        s.openPath(kTestPattern);
        QTRY_VERIFY_WITH_TIMEOUT(!s.tracks().isEmpty(), 10000);
        QCOMPARE(s.tracks().first().type, Track::Type::Video);
        QCOMPARE(s.subtitleTrack(), -1);
    }

    void playsToTheEnd()
    {
        MpvTarget target(MpvTarget::Mode::Headless);
        Session s(&target);
        QSignalSpy ended(&s, &Session::ended);
        s.openPath(kTestPattern, 9);
        QTRY_COMPARE_WITH_TIMEOUT(ended.count(), 1, 10000);
        QCOMPARE(s.state(), PlaybackState::Ended);
    }

    void optionalFeaturesReachMpv()
    {
        MpvTarget target(MpvTarget::Mode::Headless);
        Session s(&target);
        s.openPath(kTestPattern);
        QTRY_VERIFY_WITH_TIMEOUT(s.hasMedia(), 10000);
        s.pause();
        QTRY_VERIFY_WITH_TIMEOUT(s.paused(), 5000);
        s.seek(2);
        QTRY_VERIFY_WITH_TIMEOUT(qAbs(s.position() - 2.0) < 0.3, 5000);
        s.frameStep(true);
        QTRY_VERIFY_WITH_TIMEOUT(s.position() > 2.01, 5000);
        // These must not break playback.
        s.setSubtitleDelay(0.5);
        s.setAudioDelay(0.1);
        s.cycleAspectRatio();
        s.cycleCrop();
        s.setEqualizer(QStringLiteral("contrast"), 20);
        s.play();
        QTRY_COMPARE_WITH_TIMEOUT(s.state(), PlaybackState::Playing, 5000);
    }

    void missingFileIsATypedError()
    {
        MpvTarget target(MpvTarget::Mode::Headless);
        Session s(&target);
        QSignalSpy errors(&s, &Session::errorOccurred);
        s.openPath(QStringLiteral("/definitely/not/here.mkv"));
        QTRY_COMPARE_WITH_TIMEOUT(errors.count(), 1, 10000);
        QCOMPARE(errors.first().first().value<PlaybackError>(), PlaybackError::LoadFailed);
        QVERIFY(!s.hasMedia());
    }

    void reusesOneInstanceForManyItems()
    {
        MpvTarget target(MpvTarget::Mode::Headless);
        Session s(&target);
        for (int i = 0; i < 3; ++i) {
            s.openPath(kTestPattern, i);
            QTRY_VERIFY_WITH_TIMEOUT(s.hasMedia(), 10000);
        }
    }
};

QTEST_GUILESS_MAIN(TestMpvTarget)
#include "tst_mpvtarget.moc"
