// Integration test for the mpv target: real mpv, no video or audio output.
// Plays a tiny bundled test clip (tests/data, made with FFmpeg's testsrc), so it
// needs no GPU. Everything is checked through the Session, the way the UI sees it.

#include "FakeHttpServer.h"
#include "MpvTarget.h"
#include "Session.h"

#include <QSignalSpy>
#include <QtTest>
#include <clocale>

using namespace cj;
using test::FakeHttpServer;

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

    // A stream's headers (a token, say) are for the server that sent the
    // stream. A subtitle from somewhere else must not receive them.
    void streamHeadersStayWithTheirServer()
    {
        FakeHttpServer video;
        FakeHttpServer elsewhere;
        FakeHttpServer::Answer clip;
        clip.contentType = "video/x-matroska";
        clip.filePath = kTestPattern;
        video.set(QStringLiteral("/clip.mkv"), clip);
        FakeHttpServer::Answer srt;
        srt.contentType = "text/plain";
        srt.body = "1\n00:00:00,000 --> 00:00:09,000\nHello\n";
        video.set(QStringLiteral("/same.srt"), srt);
        elsewhere.set(QStringLiteral("/other.srt"), srt);

        PlayableSource source = PlayableSource::fromUrl(QUrl(video.base() + QStringLiteral("/clip.mkv")), QStringLiteral("Clip"));
        source.headers.insert(QStringLiteral("Authorization"), QStringLiteral("Bearer secret"));
        source.headers.insert(QStringLiteral("User-Agent"), QStringLiteral("Custom/1"));
        source.subtitles = {{QUrl(video.base() + QStringLiteral("/same.srt")), QStringLiteral("en"), QString()}};

        MpvTarget target(MpvTarget::Mode::Headless);
        Session s(&target);
        s.playSource(source);
        QTRY_VERIFY_WITH_TIMEOUT(s.hasMedia(), 10000);
        s.addSubtitleTracks({{QUrl(elsewhere.base() + QStringLiteral("/other.srt")), QStringLiteral("fr"), QString()}},
                            s.source());
        QTRY_COMPARE_WITH_TIMEOUT(elsewhere.paths().count(QStringLiteral("/other.srt")), 1, 10000);
        QTRY_VERIFY_WITH_TIMEOUT(video.paths().contains(QStringLiteral("/same.srt")), 10000);
        // Both subtitles end up as tracks.
        QTRY_COMPARE_WITH_TIMEOUT(subtitleTracks(s), 2, 10000);

        for (const auto& r : std::as_const(video.requests)) {
            QCOMPARE(r.headers.value("authorization"), QByteArray("Bearer secret"));
            QCOMPARE(r.headers.value("user-agent"), QByteArray("Custom/1"));
        }
        for (const auto& r : std::as_const(elsewhere.requests)) {
            QVERIFY2(!r.headers.contains("authorization"), r.headers.value("authorization").constData());
            QVERIFY(r.headers.value("user-agent") != "Custom/1");
        }
    }

    void redirectMustNotForwardCredentials_data()
    {
        QTest::addColumn<bool>("subtitle");
        QTest::newRow("video") << false;
        QTest::newRow("subtitle") << true;
    }

    void redirectMustNotForwardCredentials()
    {
        QFETCH(bool, subtitle);
        FakeHttpServer origin, other;
        FakeHttpServer::Answer clip;
        clip.contentType = "video/x-matroska";
        clip.filePath = kTestPattern;
        origin.set("/clip.mkv", clip);
        other.set("/clip.mkv", clip);
        FakeHttpServer::Answer text;
        text.contentType = "text/plain";
        text.body = "1\n00:00:00,000 --> 00:00:09,000\nHello\n";
        other.set("/other.srt", text);
        FakeHttpServer::Answer redirect;
        redirect.status = 302;
        redirect.location = (other.base() + (subtitle ? "/other.srt" : "/clip.mkv")).toUtf8();
        origin.set("/redirect", redirect);
        auto source = PlayableSource::fromUrl(QUrl(origin.base() + (subtitle ? "/clip.mkv" : "/redirect")), "Clip");
        source.headers.insert("Authorization", "Bearer audit-dummy");
        source.headers.insert("Cookie", "audit=dummy");
        if (subtitle)
            source.subtitles = {{QUrl(origin.base() + "/redirect"), "en", {}}};
        MpvTarget target(MpvTarget::Mode::Headless);
        Session session(&target);
        session.playSource(source);
        QTRY_VERIFY_WITH_TIMEOUT(!other.requests.isEmpty(), 10000);
        for (const auto& request : std::as_const(other.requests)) {
            QVERIFY2(!request.headers.contains("authorization"), "Credential crossed origin");
            QVERIFY2(!request.headers.contains("cookie"), "Cookie crossed origin");
        }
    }

private:
    static int subtitleTracks(const Session& s)
    {
        int n = 0;
        for (const Track& t : s.tracks())
            n += t.type == Track::Type::Subtitle;
        return n;
    }
};

QTEST_GUILESS_MAIN(TestMpvTarget)
#include "tst_mpvtarget.moc"
