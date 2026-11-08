// Session tests against the fake target: commands go out, events come in,
// optional features respect capabilities, and resume points are emitted.

#include "FakeTarget.h"
#include "Session.h"

#include <QFileInfo>
#include <QSignalSpy>
#include <QtTest>

using namespace cj;

class TestSession : public QObject
{
    Q_OBJECT

private:
    static void startItem(Session& s, FakeTarget& t, double duration = 100)
    {
        s.open(QUrl(QStringLiteral("file:///videos/movie.mkv")));
        t.startPlaying(duration);
    }

private slots:
    void heldArrowKeepsJumpingFromTheSeekTarget()
    {
        FakeTarget t;
        Session s(&t);
        startItem(s, t, 600);
        t.movePosition(100);
        // Three quick jumps while the target has not reported a new position
        // yet (a torrent seek takes a moment): each adds to the last target.
        s.seekRelative(10);
        s.seekRelative(10);
        s.seekRelative(10);
        QVERIFY(t.sent(QStringLiteral("seek 110")));
        QVERIFY(t.sent(QStringLiteral("seek 120")));
        QVERIFY(t.sent(QStringLiteral("seek 130")));
        // Once there, relative seeks start from the real position again.
        t.movePosition(130.2);
        t.movePosition(131);
        s.seekRelative(-10);
        QVERIFY(t.sent(QStringLiteral("seek 121")));
    }

    void resolversClaimInputFirst()
    {
        FakeTarget t;
        Session s(&t);
        s.addSourceResolver([](const QString& input) -> std::optional<PlayableSource> {
            if (!input.startsWith(QLatin1String("magnet:")))
                return std::nullopt;
            return PlayableSource::fromUrl(QUrl(QStringLiteral("torrent://abc")), QStringLiteral("A movie"));
        });
        s.openPath(QStringLiteral("magnet:?xt=urn:btih:abc"));
        QVERIFY(t.sent(QStringLiteral("load torrent://abc 0")));
        QCOMPARE(s.title(), QStringLiteral("A movie"));

        // Not claimed: a plain path (absolute paths differ by OS, e.g. D:/ on Windows).
        const QString path = QStringLiteral("/videos/other.mkv");
        s.openPath(path);
        const QString url = QUrl::fromLocalFile(QFileInfo(path).absoluteFilePath()).toString();
        QVERIFY2(t.sent("load " + url + " 0"), qPrintable(t.commands.join('\n')));
    }

    void streamStatusResetsOnLoad()
    {
        FakeTarget t;
        Session s(&t);
        startItem(s, t);
        QSignalSpy spy(&s, &Session::streamStatusChanged);
        const QUrl src(QStringLiteral("file:///videos/movie.mkv"));
        s.setStreamStatus({StreamPhase::Buffering, 40, 7, 2048, 3.5, QUrl(QStringLiteral("torrent://old"))});
        QCOMPARE(s.streamPhase(), StreamPhase::Inactive);  // about another item: ignored
        s.setStreamStatus({StreamPhase::Buffering, 40, 7, 2048, 3.5, src});
        QCOMPARE(s.streamPhase(), StreamPhase::Buffering);
        QCOMPARE(s.streamPercent(), 40);
        QCOMPARE(s.streamPeers(), 7);
        s.setStreamStatus({StreamPhase::Buffering, 40, 7, 2048, 3.5, src});  // unchanged: no signal
        QCOMPARE(spy.count(), 1);
        startItem(s, t);
        QCOMPARE(s.streamPhase(), StreamPhase::Inactive);
        QCOMPARE(spy.count(), 2);
    }

    void loadSendsCommandAndResetsItem()
    {
        FakeTarget t;
        Session s(&t);
        QCOMPARE(s.state(), PlaybackState::Idle);
        s.open(QUrl(QStringLiteral("file:///videos/movie.mkv")), 42);
        QVERIFY(t.sent(QStringLiteral("load file:///videos/movie.mkv 42")));
        QCOMPARE(s.state(), PlaybackState::Loading);
        QCOMPARE(s.title(), QStringLiteral("movie"));
        QCOMPARE(s.position(), 42.0);
        QVERIFY(!s.hasMedia());
    }

    void eventsUpdateProperties()
    {
        FakeTarget t;
        Session s(&t);
        startItem(s, t, 120);
        QVERIFY(s.hasMedia());
        QCOMPARE(s.duration(), 120.0);
        t.movePosition(33);
        QCOMPARE(s.position(), 33.0);

        QList<Track> tracks;
        tracks.append({Track::Type::Audio, 1, QStringLiteral("Main"), QStringLiteral("eng"), {}, true});
        tracks.append({Track::Type::Audio, 2, {}, QStringLiteral("heb"), {}, false});
        tracks.append({Track::Type::Subtitle, 1, QStringLiteral("Full"), {}, {}, false});
        emit t.tracksChanged(tracks);
        QCOMPARE(s.audioTracks().size(), 2);
        QCOMPARE(s.subtitleTracks().size(), 1);
        QCOMPARE(s.audioTrack(), 1);
        QCOMPARE(s.subtitleTrack(), -1);
        QCOMPARE(s.audioTracks().at(0).toMap().value("label").toString(), QStringLiteral("Main (eng)"));

        emit t.bufferingChanged(true, 40);
        QVERIFY(s.buffering());
        QCOMPARE(s.bufferPercent(), 40);

        QSignalSpy ended(&s, &Session::ended);
        t.setState(PlaybackState::Ended);
        QCOMPARE(ended.count(), 1);
    }

    void basicCommands()
    {
        FakeTarget t;
        Session s(&t);
        s.play();  // no media: nothing sent
        QVERIFY(!t.sent(QStringLiteral("play")));

        startItem(s, t);
        s.togglePause();
        QCOMPARE(t.last(), QStringLiteral("pause"));
        t.setState(PlaybackState::Paused);
        s.togglePause();
        QCOMPARE(t.last(), QStringLiteral("play"));

        t.movePosition(50);
        s.seekRelative(-10);
        QCOMPARE(t.last(), QStringLiteral("seek 40"));
        s.seek(500);  // clamped to the duration
        QCOMPARE(t.last(), QStringLiteral("seek 100"));
        s.seek(-5);
        QCOMPARE(t.last(), QStringLiteral("seek 0"));

        s.setSpeed(10);
        QCOMPARE(s.speed(), 4.0);
        QCOMPARE(t.last(), QStringLiteral("speed 4"));
        s.setVolume(150);
        QCOMPARE(s.volume(), 100.0);
        s.setVolume(30);
        QCOMPARE(t.last(), QStringLiteral("volume 30"));
        s.setMuted(true);
        QCOMPARE(t.last(), QStringLiteral("muted true"));

        s.setSubtitleTrack(-1);
        QCOMPARE(t.last(), QStringLiteral("subtitleTrack -1"));
        s.setAudioTrack(2);
        QCOMPARE(t.last(), QStringLiteral("audioTrack 2"));
    }

    void optionalFeaturesWhenSupported()
    {
        FakeTarget t;
        Session s(&t);
        startItem(s, t);
        QVERIFY(s.supports(QStringLiteral("FrameStep")));
        QVERIFY(!s.supports(QStringLiteral("NoSuchThing")));

        s.setSubtitleDelay(0.5);
        QCOMPARE(t.last(), QStringLiteral("subtitleDelay 0.5"));
        s.setAudioDelay(-0.2);
        QCOMPARE(t.last(), QStringLiteral("audioDelay -0.2"));
        s.setSubtitleScale(1.5);
        QCOMPARE(t.last(), QStringLiteral("subtitleScale 1.5"));
        SubtitleStyle style;
        style.font = QStringLiteral("Arial");
        style.color = QStringLiteral("#FFE14D");
        style.outline = 50;    // clamped
        style.background = 70;
        s.setSubtitleStyle(style);
        QCOMPARE(t.last(), QStringLiteral("subtitleStyle Arial #FFE14D 10 70 false"));
        QCOMPARE(s.subtitleStyle().outline, 10.0);
        style.color = QStringLiteral("not a color");
        s.setSubtitleStyle(style);  // falls back to white
        QCOMPARE(s.subtitleStyle().color, QStringLiteral("#FFFFFF"));
        s.frameStep(false);
        QCOMPARE(t.last(), QStringLiteral("frameStep false"));
        s.cycleAspectRatio();
        QCOMPARE(s.aspectRatio(), QStringLiteral("16:9"));
        QCOMPARE(t.last(), QStringLiteral("aspect 16:9"));
        s.cycleCrop();
        QCOMPARE(t.last(), QStringLiteral("crop 16:9"));
        s.setEqualizer(QStringLiteral("contrast"), 20);
        QCOMPARE(t.last(), QStringLiteral("equalizer 0 20 0 0 0"));
        s.setEqualizer(QStringLiteral("contrast"), 500);
        QCOMPARE(s.contrast(), 100);
        s.resetEqualizer();
        QCOMPARE(t.last(), QStringLiteral("equalizer 0 0 0 0 0"));
        s.screenshot();
        QVERIFY(t.last().startsWith(QStringLiteral("screenshot ")));
        QVERIFY(t.last().endsWith(QStringLiteral("movie 00-00-00.png")));
        s.setHardwareDecoding(false);
        QCOMPARE(t.last(), QStringLiteral("hwdec false"));
    }

    void unsupportedFeaturesAreNotSent()
    {
        FakeTarget t(Capabilities{});  // e.g. a Cast device
        Session s(&t);
        startItem(s, t);
        const int before = t.commands.size();
        s.setSubtitleDelay(1);
        s.setAudioDelay(1);
        s.setSubtitleScale(2);
        s.frameStep(true);
        s.screenshot();
        s.toggleAbLoop();
        s.cycleAspectRatio();
        s.cycleCrop();
        s.setEqualizer(QStringLiteral("hue"), 10);
        s.setHardwareDecoding(false);
        s.nextChapter();
        QCOMPARE(t.commands.size(), before);
        QCOMPARE(s.subtitleDelay(), 0.0);
        QCOMPARE(s.aspectRatio(), QString());
        QVERIFY(!s.supports(QStringLiteral("FrameStep")));
    }

    void abLoopCycle()
    {
        FakeTarget t;
        Session s(&t);
        startItem(s, t);
        t.movePosition(10);
        s.toggleAbLoop();
        QCOMPARE(s.abLoopA(), 10.0);
        QCOMPARE(s.abLoopB(), -1.0);
        t.movePosition(20);
        s.toggleAbLoop();
        QCOMPARE(t.last(), QStringLiteral("abLoop 10 20"));
        s.toggleAbLoop();
        QCOMPARE(t.last(), QStringLiteral("abLoop -1 -1"));
        QCOMPARE(s.abLoopA(), -1.0);
    }

    void chapters()
    {
        FakeTarget t;
        Session s(&t);
        startItem(s, t);
        emit t.chaptersChanged({{0, QStringLiteral("One")}, {30, QStringLiteral("Two")}, {60, QStringLiteral("Three")}});
        QCOMPARE(s.chapters().size(), 3);

        t.movePosition(40);
        QCOMPARE(s.currentChapter(), 1);
        s.nextChapter();
        QCOMPARE(t.last(), QStringLiteral("chapter 2"));
        s.previousChapter();  // 10 s into chapter 2: back to its start
        QCOMPARE(t.last(), QStringLiteral("chapter 1"));
        t.movePosition(31);
        s.previousChapter();  // within the first 3 s: the chapter before
        QCOMPARE(t.last(), QStringLiteral("chapter 0"));
        t.movePosition(70);
        const int before = t.commands.size();
        s.nextChapter();  // already in the last chapter
        QCOMPARE(t.commands.size(), before);
    }

    void resumePoints()
    {
        FakeTarget t;
        Session s(&t);
        s.setResumeInterval(50);
        QSignalSpy points(&s, &Session::resumePoint);
        startItem(s, t, 200);
        t.movePosition(12);
        QTRY_VERIFY_WITH_TIMEOUT(points.count() >= 2, 2000);
        QCOMPARE(points.last().at(1).toDouble(), 12.0);
        QCOMPARE(points.last().at(2).toDouble(), 200.0);

        t.setState(PlaybackState::Paused);  // pausing saves too, then the timer stops
        const int afterPause = points.count();
        QTest::qWait(200);
        QCOMPARE(points.count(), afterPause);

        t.movePosition(15);
        s.stop();
        QCOMPARE(points.count(), afterPause + 1);
        QCOMPARE(points.last().at(1).toDouble(), 15.0);

        // Replacing the item saves the old one first.
        t.setState(PlaybackState::Playing);
        t.movePosition(18);
        points.clear();
        s.open(QUrl(QStringLiteral("file:///videos/next.mkv")));
        QCOMPARE(points.count(), 1);
        QCOMPARE(points.first().first().value<PlayableSource>().url,
                 QUrl(QStringLiteral("file:///videos/movie.mkv")));
        QCOMPARE(points.first().at(1).toDouble(), 18.0);
    }

    void perItemSettingsReset()
    {
        FakeTarget t;
        Session s(&t);
        startItem(s, t);
        s.setSubtitleDelay(1);
        s.cycleAspectRatio();
        s.setSpeed(1.5);
        s.open(QUrl(QStringLiteral("file:///videos/next.mkv")));
        QCOMPARE(s.subtitleDelay(), 0.0);
        QCOMPARE(s.aspectRatio(), QString());
        QCOMPARE(s.speed(), 1.5);  // speed carries over
        QCOMPARE(s.title(), QStringLiteral("next"));
    }

    void errorsPassThrough()
    {
        FakeTarget t;
        Session s(&t);
        QSignalSpy errors(&s, &Session::errorOccurred);
        emit t.errorOccurred(PlaybackError::UnsupportedFile, QStringLiteral("nope"));
        QCOMPARE(errors.count(), 1);
        QCOMPARE(errors.first().first().value<PlaybackError>(), PlaybackError::UnsupportedFile);
    }
};

QTEST_GUILESS_MAIN(TestSession)
#include "tst_session.moc"
