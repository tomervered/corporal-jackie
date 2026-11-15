// Integration test for the mpv target: real mpv, no video or audio output.
// Plays a tiny bundled test clip (tests/data, made with FFmpeg's testsrc), so it
// needs no GPU. Everything is checked through the Session, the way the UI sees it.

#include "FakeHttpServer.h"
#include "MpvTarget.h"
#include "ScopedHttp.h"
#include "Session.h"

#include <QSignalSpy>
#include <mpv/client.h>
#include <QtTest>
#include <clocale>

using namespace cj;
using test::FakeHttpServer;

namespace {
// Filled from mpv's stream thread (ScopedHttp) and the main thread.
QMutex capturedMutex;
QStringList capturedMessages;

// 10 seconds, 160x120, 25 fps, a keyframe every second.
const QString kTestPattern = QStringLiteral(CJ_TEST_DATA_DIR "/testsrc-10s.mkv");
}

class TestMpvTarget : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase() { std::setlocale(LC_NUMERIC, "C"); }

    // Links come from add-ons: mpv must not run yt-dlp or Lua scripts for them.
    void playerRunsNoScriptsAndNoYtdl()
    {
        MpvTarget target(MpvTarget::Mode::Headless);
        // A libmpv built without Lua (ours on Windows) has no ytdl or osc
        // option at all, which is as good as "no": nothing can run.
        char* osc = mpv_get_property_string(target.handle(), "osc");
        const bool hasLua = osc != nullptr;
        mpv_free(osc);
        for (const char* name : {"ytdl", "load-scripts"}) {
            char* value = mpv_get_property_string(target.handle(), name);
            if (!value) {
                QVERIFY2(!hasLua, name);
                continue;
            }
            QCOMPARE(QString::fromUtf8(value), QStringLiteral("no"));
            mpv_free(value);
        }
    }

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

    // Qt's keys as mpv's input.conf names them.
    void namesKeysTheWayMpvDoes()
    {
        const auto name = [](int key, Qt::KeyboardModifiers m, const char* text) {
            return MpvTarget::keyName(key, m, QString::fromUtf8(text));
        };
        QCOMPARE(name(Qt::Key_Space, {}, " "), QStringLiteral("SPACE"));
        QCOMPARE(name(Qt::Key_F, {}, "f"), QStringLiteral("f"));
        QCOMPARE(name(Qt::Key_F, Qt::ShiftModifier, "F"), QStringLiteral("F"));
        // A Hebrew layout: the text is a Hebrew letter, the key is still F.
        QCOMPARE(name(Qt::Key_F, {}, "כ"), QStringLiteral("f"));
        QCOMPARE(name(Qt::Key_Right, Qt::ShiftModifier, ""), QStringLiteral("Shift+RIGHT"));
        QCOMPARE(name(Qt::Key_BraceLeft, Qt::ShiftModifier, "{"), QStringLiteral("{"));
        QCOMPARE(name(Qt::Key_NumberSign, Qt::ShiftModifier, "#"), QStringLiteral("SHARP"));
        QCOMPARE(name(Qt::Key_Exclam, Qt::ShiftModifier, "!"), QStringLiteral("!"));
        QCOMPARE(name(Qt::Key_PageDown, {}, ""), QStringLiteral("PGDWN"));
        QCOMPARE(name(Qt::Key_Backspace, {}, "\b"), QStringLiteral("BS"));
        QCOMPARE(name(Qt::Key_9, Qt::KeypadModifier, "9"), QStringLiteral("KP9"));
        QCOMPARE(name(Qt::Key_F9, {}, ""), QStringLiteral("F9"));
        QCOMPARE(name(Qt::Key_MediaTogglePlayPause, {}, ""), QStringLiteral("PLAYPAUSE"));
        QCOMPARE(name(Qt::Key_Shift, Qt::ShiftModifier, ""), QString());
#ifndef Q_OS_MACOS
        QCOMPARE(name(Qt::Key_H, Qt::ControlModifier, "\x08"), QStringLiteral("Ctrl+h"));
        QCOMPARE(name(Qt::Key_Plus, Qt::ControlModifier | Qt::ShiftModifier, "+"), QStringLiteral("Ctrl++"));
#endif
        QCOMPARE(name(Qt::Key_Left, Qt::KeypadModifier, ""), QStringLiteral("LEFT"));  // macOS arrows
    }

    // The keys run mpv's own bindings, and what they change reaches the
    // Session. q closes the video instead of ending mpv.
    void keysRunMpvsBindings()
    {
        MpvTarget target(MpvTarget::Mode::Headless);
        Session s(&target);
        s.openPath(kTestPattern);
        QTRY_VERIFY_WITH_TIMEOUT(s.hasMedia(), 10000);
        const auto press = [&](int key, const char* text, Qt::KeyboardModifiers m = {}) {
            QVERIFY(s.keyEvent(key, int(m), QString::fromUtf8(text), true));
            s.keyEvent(key, int(m), QString::fromUtf8(text), false);
        };
        press(Qt::Key_M, "m");
        QTRY_VERIFY_WITH_TIMEOUT(s.muted(), 5000);
        press(Qt::Key_BracketRight, "]");
        QTRY_VERIFY_WITH_TIMEOUT(qAbs(s.speed() - 1.1) < 0.001, 5000);
        press(Qt::Key_X, "x");
        QTRY_VERIFY_WITH_TIMEOUT(qAbs(s.subtitleDelay() - 0.1) < 0.001, 5000);
        press(Qt::Key_F, "f");
        QTRY_VERIFY_WITH_TIMEOUT(s.fullScreen(), 5000);
        press(Qt::Key_Escape, "");
        QTRY_VERIFY_WITH_TIMEOUT(!s.fullScreen(), 5000);
        QSignalSpy actions(&s, &Session::appActionRequested);
        press(Qt::Key_I, "i");
        QTRY_COMPARE_WITH_TIMEOUT(actions.count(), 1, 5000);
        QCOMPARE(actions.first().first().toString(), QStringLiteral("stats"));
        press(Qt::Key_Q, "q");
        QTRY_COMPARE_WITH_TIMEOUT(s.state(), PlaybackState::Idle, 5000);
        // mpv is still there for the next item.
        s.openPath(kTestPattern);
        QTRY_VERIFY_WITH_TIMEOUT(s.hasMedia(), 10000);
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

    // A signed link's token, a user:password and an add-on's path must not
    // reach the log.
    void playbackLogsOmitSignedQuery()
    {
        FakeHttpServer server;
        MpvTarget target(MpvTarget::Mode::Headless);
        capturedMessages.clear();
        const auto previous = qInstallMessageHandler([](QtMsgType, const QMessageLogContext&, const QString& message) {
            QMutexLocker lock(&capturedMutex);
            capturedMessages << message;
        });
        auto source = PlayableSource::fromUrl(QUrl(server.base() + "/clip.mkv?token=auditDummySigned987"), "Clip");
        target.load(source, 0);
        source.url = QUrl("http://someone:s3cretPass@127.0.0.1:1/s3cretPath/clip.mkv");
        target.load(source, 0);
        QTest::qWait(500);  // mpv's own failure messages too
        qInstallMessageHandler(previous);
        QMutexLocker lock(&capturedMutex);
        const QString all = capturedMessages.join("\n");
        QVERIFY2(!all.contains("auditDummySigned987"), "Playback log contains signed URL credential");
        QVERIFY2(!all.contains("s3cret"), qPrintable(all));
    }

    // Credentials go only to the stream's own origin (scheme, host, port),
    // at every hop. Two servers on one host differ only by port.
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
        origin.set("/clip.mkv", clipAnswer());
        other.set("/clip.mkv", clipAnswer());
        other.set("/other.srt", srtAnswer());
        origin.set("/redirect", redirectTo(other.base() + (subtitle ? "/other.srt" : "/clip.mkv")));
        auto source = PlayableSource::fromUrl(QUrl(origin.base() + (subtitle ? "/clip.mkv" : "/redirect")), "Clip");
        source.headers.insert("Authorization", "Bearer audit-dummy");
        source.headers.insert("Cookie", "audit=dummy");
        if (subtitle)
            source.subtitles = {{QUrl(origin.base() + "/redirect"), "en", {}}};
        MpvTarget target(MpvTarget::Mode::Headless);
        Session session(&target);
        session.playSource(source);
        QTRY_VERIFY_WITH_TIMEOUT(session.hasMedia(), 10000);
        if (subtitle)
            QTRY_COMPARE_WITH_TIMEOUT(subtitleTracks(session), 1, 10000);
        QVERIFY(!other.requests.isEmpty());
        for (const auto& request : std::as_const(other.requests)) {
            QVERIFY2(!request.headers.contains("authorization"), "Credential crossed origin");
            QVERIFY2(!request.headers.contains("cookie"), "Cookie crossed origin");
        }
        // The origin itself still got them, on the first hop.
        QVERIFY(!origin.requests.isEmpty());
        for (const auto& request : std::as_const(origin.requests))
            QCOMPARE(request.headers.value("authorization"), QByteArray("Bearer audit-dummy"));
    }

    // A redirect within the origin keeps the credentials (signed CDNs and
    // add-ons with a login redirect inside their own server), and seeking
    // asks the origin again with them.
    void sameOriginRedirectKeepsCredentialsAndSeeks()
    {
        FakeHttpServer origin;
        origin.set("/clip.mkv", clipAnswer());
        origin.set("/redirect", redirectTo("/clip.mkv"));
        auto source = PlayableSource::fromUrl(QUrl(origin.base() + "/redirect"), "Clip");
        source.headers.insert("Authorization", "Bearer kept");
        MpvTarget target(MpvTarget::Mode::Headless);
        Session session(&target);
        session.playSource(source);
        QTRY_VERIFY_WITH_TIMEOUT(session.hasMedia(), 10000);
        session.pause();
        session.seek(7.0);
        QTRY_VERIFY_WITH_TIMEOUT(qAbs(session.position() - 7.0) < 0.5, 10000);
        QVERIFY(origin.paths().contains("/clip.mkv"));
        for (const auto& request : std::as_const(origin.requests))
            QCOMPARE(request.headers.value("authorization"), QByteArray("Bearer kept"));
    }

    // Once a hop leaves the origin, a redirect back does not bring the
    // credentials back: the other server chose that link.
    void credentialsDoNotReturnAfterLeaving()
    {
        FakeHttpServer origin, other;
        origin.set("/clip.mkv", clipAnswer());
        origin.set("/start", redirectTo(other.base() + "/bounce"));
        other.set("/bounce", redirectTo(origin.base() + "/clip.mkv"));
        auto source = PlayableSource::fromUrl(QUrl(origin.base() + "/start"), "Clip");
        source.headers.insert("Authorization", "Bearer once");
        MpvTarget target(MpvTarget::Mode::Headless);
        Session session(&target);
        session.playSource(source);
        QTRY_VERIFY_WITH_TIMEOUT(session.hasMedia(), 10000);
        for (const auto& request : std::as_const(origin.requests))
            QCOMPARE(request.headers.contains("authorization"), request.path == "/start");
        for (const auto& request : std::as_const(other.requests))
            QVERIFY(!request.headers.contains("authorization"));
    }

    // A user:password in the link is a credential too: Basic auth for that
    // origin only, never in the request line or the log.
    void userInfoIsScopedLikeHeaders()
    {
        FakeHttpServer origin, other;
        other.set("/clip.mkv", clipAnswer());
        origin.set("/clip.mkv", redirectTo(other.base() + "/clip.mkv"));
        QUrl url(origin.base() + "/clip.mkv");
        url.setUserName("someone");
        url.setPassword("pw");
        MpvTarget target(MpvTarget::Mode::Headless);
        Session session(&target);
        session.playSource(PlayableSource::fromUrl(url, "Clip"));
        QTRY_VERIFY_WITH_TIMEOUT(session.hasMedia(), 10000);
        QCOMPARE(origin.requests.first().headers.value("authorization"), "Basic " + QByteArray("someone:pw").toBase64());
        for (const auto& request : std::as_const(other.requests))
            QVERIFY(!request.headers.contains("authorization"));
    }

    // A redirect may only lead to another web link.
    void redirectToALocalFileFails()
    {
        FakeHttpServer origin;
        origin.set("/clip.mkv", redirectTo(QUrl::fromLocalFile(kTestPattern).toString()));
        auto source = PlayableSource::fromUrl(QUrl(origin.base() + "/clip.mkv"), "Clip");
        source.headers.insert("Authorization", "Bearer x");
        MpvTarget target(MpvTarget::Mode::Headless);
        Session session(&target);
        QSignalSpy errors(&session, &Session::errorOccurred);
        session.playSource(source);
        QTRY_COMPARE_WITH_TIMEOUT(errors.count(), 1, 10000);
        QVERIFY(!session.hasMedia());
    }

    // An add-on's subtitle is capped in size and in how long it may stall,
    // and a bad one does not stop the video.
    void subtitleDownloadsAreBounded()
    {
        ScopedHttp::setStallTimeoutMs(1500);
        FakeHttpServer server;
        server.set("/clip.mkv", clipAnswer());
        FakeHttpServer::Answer huge = srtAnswer();
        huge.body = QByteArray(21 * 1024 * 1024, 'x');
        server.set("/huge.srt", huge);
        FakeHttpServer::Answer stalled = srtAnswer();
        stalled.delayMs = -1;
        server.set("/stalled.srt", stalled);
        server.set("/good.srt", srtAnswer());
        auto source = PlayableSource::fromUrl(QUrl(server.base() + "/clip.mkv"), "Clip");
        source.subtitles = {{QUrl(server.base() + "/huge.srt"), "en", {}},
                            {QUrl(server.base() + "/stalled.srt"), "fr", {}},
                            {QUrl(server.base() + "/good.srt"), "de", {}}};
        MpvTarget target(MpvTarget::Mode::Headless);
        Session session(&target);
        session.playSource(source);
        QTRY_VERIFY_WITH_TIMEOUT(session.hasMedia(), 10000);
        QTRY_COMPARE_WITH_TIMEOUT(subtitleTracks(session), 1, 10000);
        QTest::qWait(2500);  // past the stall limit: still only the good one
        QCOMPARE(subtitleTracks(session), 1);
        QCOMPARE(session.state(), PlaybackState::Playing);
        ScopedHttp::setStallTimeoutMs(30000);
    }

    // A subtitle file the user picks on this computer still works, through
    // its own entry point, while a stream with credentials is playing.
    void userPickedLocalSubtitlePlays()
    {
        QTemporaryDir dir;
        QFile srt(dir.filePath("mine.srt"));
        QVERIFY(srt.open(QIODevice::WriteOnly));
        srt.write(srtAnswer().body);
        srt.close();
        FakeHttpServer server;
        server.set("/clip.mkv", clipAnswer());
        auto source = PlayableSource::fromUrl(QUrl(server.base() + "/clip.mkv"), "Clip");
        source.headers.insert("Authorization", "Bearer x");
        MpvTarget target(MpvTarget::Mode::Headless);
        Session session(&target);
        session.playSource(source);
        QTRY_VERIFY_WITH_TIMEOUT(session.hasMedia(), 10000);
        session.addExternalSubtitle(QUrl::fromLocalFile(srt.fileName()));
        QTRY_COMPARE_WITH_TIMEOUT(subtitleTracks(session), 1, 10000);
        QTRY_VERIFY_WITH_TIMEOUT(session.subtitleTrack() > 0, 5000);
    }

    // Only web links and local files become subtitle tracks.
    void subtitleSchemesAreChecked()
    {
        FakeHttpServer server;
        server.set("/clip.mkv", clipAnswer());
        auto source = PlayableSource::fromUrl(QUrl(server.base() + "/clip.mkv"), "Clip");
        source.subtitles = {{QUrl("ftp://127.0.0.1:1/x.srt"), "en", {}}, {QUrl("data:text/plain,hi"), "fr", {}}};
        MpvTarget target(MpvTarget::Mode::Headless);
        Session session(&target);
        session.playSource(source);
        QTRY_VERIFY_WITH_TIMEOUT(session.hasMedia(), 10000);
        QTest::qWait(500);
        QCOMPARE(subtitleTracks(session), 0);
    }

    // An HLS playlist played with credentials: segments named relative to it
    // come through the same scoping; a segment on another server gets none.
    void playlistSegmentsFollowTheSameRule()
    {
        FakeHttpServer origin, other;
        origin.set("/seg.mkv", clipAnswer());
        other.set("/seg.mkv", clipAnswer());
        FakeHttpServer::Answer playlist;
        playlist.contentType = "application/vnd.apple.mpegurl";
        playlist.body = "#EXTM3U\n#EXT-X-VERSION:3\n#EXT-X-TARGETDURATION:10\n#EXT-X-MEDIA-SEQUENCE:0\n"
                        "#EXTINF:10.0,\nseg.mkv\n#EXTINF:10.0,\n" + (other.base() + "/seg.mkv").toUtf8()
                      + "\n#EXT-X-ENDLIST\n";
        origin.set("/list.m3u8", playlist);
        auto source = PlayableSource::fromUrl(QUrl(origin.base() + "/list.m3u8"), "Clip");
        source.headers.insert("Authorization", "Bearer hls");
        MpvTarget target(MpvTarget::Mode::Headless);
        Session session(&target);
        session.playSource(source);
        QTRY_VERIFY_WITH_TIMEOUT(origin.paths().contains("/seg.mkv"), 15000);
        for (const auto& request : std::as_const(origin.requests))
            QCOMPARE(request.headers.value("authorization"), QByteArray("Bearer hls"));
        for (const auto& request : std::as_const(other.requests))
            QVERIFY(!request.headers.contains("authorization"));
    }

    // --- Untrusted media (security review 2026-11-15, docs/SECURITY.md) ---------

    // mpv's own HTTPS must check certificates, and FFmpeg may open only
    // network protocols for the links inside a file.
    void networkOptionsAreSafe()
    {
        MpvTarget target(MpvTarget::Mode::Headless);
        char* verify = mpv_get_property_string(target.handle(), "tls-verify");
        QVERIFY(verify);
        QCOMPARE(QByteArray(verify), QByteArray("yes"));
        mpv_free(verify);
        char* lavf = mpv_get_property_string(target.handle(), "demuxer-lavf-o");
        QVERIFY(lavf);
        QVERIFY2(QByteArray(lavf).contains("protocol_whitelist") && !QByteArray(lavf).contains("file"), lavf);
        mpv_free(lavf);
    }

    // A video that is really a playlist (as a file in a torrent could be)
    // must not make mpv open the files it names. Local videos and torrents
    // get the same rule; a local file is easier to set up here.
    void listDisguisedAsAVideoOpensNothing_data()
    {
        QTest::addColumn<QByteArray>("header");
        QTest::newRow("m3u") << QByteArray("#EXTM3U\n");
        QTest::newRow("mpv EDL") << QByteArray("# mpv EDL v0\n");
    }
    void listDisguisedAsAVideoOpensNothing()
    {
        QFETCH(QByteArray, header);
        QTemporaryDir dir;
        QFile list(dir.filePath(QStringLiteral("film.mkv")));
        QVERIFY(list.open(QIODevice::WriteOnly));
        list.write(header + QFile::encodeName(kTestPattern) + '\n');
        list.close();

        MpvTarget target(MpvTarget::Mode::Headless);
        Session s(&target);
        QSignalSpy errors(&s, &Session::errorOccurred);
        s.openPath(list.fileName());
        QTRY_VERIFY_WITH_TIMEOUT(errors.count() >= 1, 10000);
        QTest::qWait(1000);
        QVERIFY(!s.hasMedia());
        char* refs = mpv_get_property_string(target.handle(), "access-references");
        QCOMPARE(QByteArray(refs), QByteArray("no"));
        mpv_free(refs);
    }

    // A playlist file the user opens on purpose still plays, as in any player.
    void userPickedPlaylistPlays()
    {
        QTemporaryDir dir;
        QFile list(dir.filePath(QStringLiteral("films.m3u")));
        QVERIFY(list.open(QIODevice::WriteOnly));
        list.write("#EXTM3U\n" + QFile::encodeName(kTestPattern) + '\n');
        list.close();
        MpvTarget target(MpvTarget::Mode::Headless);
        Session s(&target);
        s.openPath(list.fileName());
        QTRY_VERIFY_WITH_TIMEOUT(s.hasMedia(), 10000);
    }

    // A web link with credentials plays through cjhttp://, which mpv trusts
    // like a local path. A playlist from there may name only web links...
    void webPlaylistCannotNameALocalFile()
    {
        FakeHttpServer origin;
        FakeHttpServer::Answer list;
        list.contentType = "audio/x-mpegurl";
        list.body = "#EXTM3U\n" + QFile::encodeName(kTestPattern) + '\n';
        origin.set("/list.m3u", list);
        auto source = PlayableSource::fromUrl(QUrl(origin.base() + "/list.m3u"), "List");
        source.headers.insert("Authorization", "Bearer x");
        MpvTarget target(MpvTarget::Mode::Headless);
        Session s(&target);
        QSignalSpy errors(&s, &Session::errorOccurred);
        s.playSource(source);
        QTRY_VERIFY_WITH_TIMEOUT(errors.count() >= 1, 10000);
        QTest::qWait(1000);
        QVERIFY(!s.hasMedia());
    }

    // ...and an mpv EDL list or CUE sheet from there is refused outright.
    void webFileListIsRefused_data()
    {
        QTest::addColumn<QByteArray>("body");
        QTest::newRow("mpv EDL") << QByteArray("# mpv EDL v0\n") + QFile::encodeName(kTestPattern) + '\n';
        // No CUE row: mpv's CUE reader keeps only the file name and looks for
        // it next to the sheet, so a CUE from a server can only name files on
        // that server. tst_common checks that the sniff still spots CUE sheets.
    }
    void webFileListIsRefused()
    {
        QFETCH(QByteArray, body);
        FakeHttpServer origin;
        FakeHttpServer::Answer list;
        list.contentType = "text/plain";
        list.body = body;
        origin.set("/film.mkv", list);
        auto source = PlayableSource::fromUrl(QUrl(origin.base() + "/film.mkv"), "Film");
        source.headers.insert("Authorization", "Bearer x");
        MpvTarget target(MpvTarget::Mode::Headless);
        Session s(&target);
        QSignalSpy errors(&s, &Session::errorOccurred);
        s.playSource(source);
        QTRY_VERIFY_WITH_TIMEOUT(errors.count() >= 1, 10000);
        QTest::qWait(1000);
        QVERIFY(!s.hasMedia());
    }

    // An HLS list read through cjhttp:// that names a file: segment. FFmpeg's
    // HLS reader takes file: segments (on Windows that can be a network
    // share); its own http keeps a whitelist, our protocol has none. Our
    // whitelist stops FFmpeg, and onLoadHook() stops mpv's playlist reader,
    // which takes over when FFmpeg gives up.
    void webHlsCannotNameALocalFile()
    {
        FakeHttpServer origin;
        FakeHttpServer::Answer playlist;
        playlist.contentType = "application/vnd.apple.mpegurl";
        playlist.body = "#EXTM3U\n#EXT-X-VERSION:3\n#EXT-X-TARGETDURATION:10\n#EXT-X-MEDIA-SEQUENCE:0\n"
                        "#EXTINF:10.0,\n" + QUrl::fromLocalFile(kTestPattern).toEncoded() + "\n#EXT-X-ENDLIST\n";
        origin.set("/list.m3u8", playlist);
        MpvTarget target(MpvTarget::Mode::Headless);
        Session s(&target);
        QSignalSpy errors(&s, &Session::errorOccurred);
        auto source = PlayableSource::fromUrl(QUrl(origin.base() + "/list.m3u8"), "List");
        source.headers.insert("Authorization", "Bearer x");
        s.playSource(source);
        QTRY_VERIFY_WITH_TIMEOUT(errors.count() >= 1, 15000);
        QTest::qWait(1000);
        QVERIFY(!s.hasMedia());
    }

private:
    static FakeHttpServer::Answer clipAnswer()
    {
        FakeHttpServer::Answer clip;
        clip.contentType = "video/x-matroska";
        clip.filePath = kTestPattern;
        return clip;
    }
    static FakeHttpServer::Answer srtAnswer()
    {
        FakeHttpServer::Answer text;
        text.contentType = "text/plain";
        text.body = "1\n00:00:00,000 --> 00:00:09,000\nHello\n";
        return text;
    }
    static FakeHttpServer::Answer redirectTo(const QString& location)
    {
        FakeHttpServer::Answer redirect;
        redirect.status = 302;
        redirect.location = location.toUtf8();
        return redirect;
    }

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
