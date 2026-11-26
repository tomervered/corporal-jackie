// Integration test for the mpv target: real mpv, no video or audio output.
// Plays a tiny bundled test clip (tests/data, made with FFmpeg's testsrc), so it
// needs no GPU. Everything is checked through the Session, the way the UI sees it.

#include "FakeHttpServer.h"
#include "GraphicsCards.h"
#include "MpvTarget.h"
#include "ScopedHttp.h"
#include "Session.h"

#include <QSignalSpy>
#include <QTcpSocket>
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

    static QStringList shaders(MpvTarget& target)
    {
        QStringList list;
        mpv_node node;
        if (mpv_get_property(target.handle(), "glsl-shaders", MPV_FORMAT_NODE, &node) < 0)
            return list;
        if (node.format == MPV_FORMAT_NODE_ARRAY)
            for (int i = 0; i < node.u.list->num; ++i)
                if (node.u.list->values[i].format == MPV_FORMAT_STRING)
                    list << QString::fromUtf8(node.u.list->values[i].u.string);
        mpv_free_node_contents(&node);
        return list;
    }

    static QByteArray readFile(const QString& path)
    {
        QFile f(path);
        return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
    }

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

    void staleFramebufferCannotStartAReplacedOrClosedLoad()
    {
        MpvTarget target(MpvTarget::Mode::Video);
        Session session(&target);
        session.openPath(kTestPattern);
        const auto first = target.pendingRenderLoad();
        QVERIFY(first != 0);

        session.openPath(kTestPattern);
        const auto replacement = target.pendingRenderLoad();
        QVERIFY(replacement > first);
        target.updateViewportHeight(720, first);
        QCOMPARE(target.pendingRenderLoad(), replacement);

        session.stop();
        target.updateViewportHeight(720, replacement);
        QCOMPARE(target.pendingRenderLoad(), uint64_t(0));
        QCOMPARE(session.state(), PlaybackState::Idle);
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

    // FSR is a shader file mpv loads; off must leave nothing loaded or on disk.
    void upscalerLoadsAndUnloadsTheShader()
    {
        MpvTarget target(MpvTarget::Mode::Headless);
        Session s(&target);
        QVERIFY(s.upscalers().contains(QStringLiteral("fsr")));
        QVERIFY(shaders(target).isEmpty());

        // mpv clears the old list before publishing the replacement.
        // Check one snapshot per poll and keep the path that passed.
        const auto changedShader = [&target](const QString& previous, QString& path) {
            const QStringList active = shaders(target);
            if (active.size() != 1 || active.first().isEmpty() || active.first() == previous)
                return false;
            path = active.first();
            return true;
        };
        s.setUpscaler(QStringLiteral("fsr"));
        QString screenSized;
        QTRY_VERIFY_WITH_TIMEOUT(changedShader({}, screenSized), 5000);
        QByteArray text = readFile(screenSized);
        QVERIFY(text.contains("FSR 1 EASU"));
        QVERIFY(text.contains("//!WIDTH OUTPUT.w\n"));
        QVERIFY(!text.contains("@WHEN@") && !text.contains("@WIDTH@") && !text.contains("@HEIGHT@"));

        s.setUpscaleHeight(1440);
        QString fixed;
        QTRY_VERIFY_WITH_TIMEOUT(changedShader(screenSized, fixed), 5000);
        text = readFile(fixed);
        QVERIFY(text.contains("//!WHEN 1440 LUMA.h / 1.0 >\n"));
        QVERIFY(text.contains("//!WIDTH LUMA.w 1440 * LUMA.h /\n"));
        QVERIFY(text.contains("//!HEIGHT 1440\n"));
        QVERIFY(!QFile::exists(screenSized));
        QTRY_COMPARE_WITH_TIMEOUT(s.stats().value(QStringLiteral("Upscaler")).toString(), QStringLiteral("AMD FSR 1 to 1440p"), 5000);

        // SGSR, the light one: its own file, the same sizes.
        s.setUpscaler(QStringLiteral("sgsr"));
        QString light;
        QTRY_VERIFY_WITH_TIMEOUT(changedShader(fixed, light), 5000);
        text = readFile(light);
        QVERIFY(text.contains("Snapdragon GSR 1"));
        QVERIFY(text.contains("//!HEIGHT 1440\n"));
        QVERIFY(!QFile::exists(fixed));
        QTRY_COMPARE_WITH_TIMEOUT(s.stats().value(QStringLiteral("Upscaler")).toString(), QStringLiteral("Snapdragon GSR to 1440p"), 5000);

        // CAS and Anime4K: their own files; Anime4K gets only its WHEN.
        s.setUpscaler(QStringLiteral("cas"));
        QString cas;
        QTRY_VERIFY_WITH_TIMEOUT(changedShader(light, cas), 5000);
        QVERIFY(readFile(cas).contains("AMD CAS"));
        QVERIFY(!QFile::exists(light));
        s.setUpscaler(QStringLiteral("anime4k"));
        QString cartoon;
        QTRY_VERIFY_WITH_TIMEOUT(changedShader(cas, cartoon), 5000);
        text = readFile(cartoon);
        QVERIFY(text.contains("Anime4K"));
        QCOMPARE(text.count("//!WHEN 1440 MAIN.h / 1.200 >\n"), 5);
        QVERIFY(!text.contains("@WHEN@"));

        s.setUpscaler(QStringLiteral("off"));
        QTRY_VERIFY_WITH_TIMEOUT(shaders(target).isEmpty(), 5000);
        QVERIFY(!QFile::exists(cartoon));
        QCOMPARE(s.stats().value(QStringLiteral("Upscaler")).toString(), QStringLiteral("off"));
    }

    // Which cards get VSR, and the filter it runs as. Plain functions, so
    // they are checked on every platform; VSR itself needs Windows and the card.
    void videoSuperResolutionRules()
    {
        const auto caps = [](unsigned vendor, const char* name) {
            return MpvTarget::vsrCapabilities(vendor, QString::fromLatin1(name));
        };
        QCOMPARE(caps(0x10DE, "NVIDIA GeForce RTX 4060"), Capabilities(Capability::UpscaleNvidiaVsr));
        QCOMPARE(caps(0x10DE, "NVIDIA GeForce GTX 1660"), Capabilities());
        QCOMPARE(caps(0x8086, "Intel(R) Arc(TM) A770 Graphics"), Capabilities(Capability::UpscaleIntelVsr));
        QCOMPARE(caps(0x8086, "Intel(R) Iris(R) Xe Graphics"), Capabilities(Capability::UpscaleIntelVsr));
        QCOMPARE(caps(0x8086, "Intel(R) UHD Graphics 630"), Capabilities());  // a 10th gen i3
        QCOMPARE(caps(0x8086, "Intel(R) UHD Graphics"), Capabilities(Capability::UpscaleIntelVsr));  // 12th-14th gen laptop
        QCOMPARE(caps(0x8086, "Intel(R) UHD Graphics 770"), Capabilities(Capability::UpscaleIntelVsr));
        QCOMPARE(caps(0x8086, "Intel(R) Arc(TM) Graphics"), Capabilities(Capability::UpscaleIntelVsr));  // Core Ultra
        QCOMPARE(caps(0x8086, "Intel(R) Iris(R) Plus Graphics"), Capabilities());
        QCOMPARE(caps(0x8086, "Intel(R) HD Graphics 620"), Capabilities());
        QCOMPARE(caps(0x10DE, "NVIDIA GeForce RTX 4060 Laptop GPU"), Capabilities(Capability::UpscaleNvidiaVsr));
        QCOMPARE(caps(0x1002, "AMD Radeon RX 7600"), Capabilities());
        QCOMPARE(caps(0x1414, "Microsoft Basic Render Driver"), Capabilities());

        const auto vf = [](Upscaler u, int videoHeight, int target, int shown, bool intelCopyback = false) {
            return MpvTarget::vsrFilter(u, videoHeight, target, shown, intelCopyback);
        };
        QCOMPARE(vf(Upscaler::NvidiaVsr, 720, 1440, 0), QStringLiteral("d3d11vpp=scaling-mode=nvidia:scale=2.00"));
        QCOMPARE(vf(Upscaler::IntelVsr, 1080, 2160, 0), QStringLiteral("d3d11vpp=scaling-mode=intel:scale=2.00"));
        // 0 = the size shown; steps of 0.05, at most 4x.
        QCOMPARE(vf(Upscaler::NvidiaVsr, 480, 0, 1000), QStringLiteral("d3d11vpp=scaling-mode=nvidia:scale=2.10"));
        QCOMPARE(vf(Upscaler::NvidiaVsr, 240, 2160, 0), QStringLiteral("d3d11vpp=scaling-mode=nvidia:scale=4.00"));
        // The compatibility mode is asked for only by Intel VSR, and only when
        // it is on: the default filter is the same as before.
        QVERIFY(!vf(Upscaler::IntelVsr, 1080, 2160, 0).contains(QLatin1String("intel-copyback")));
        QCOMPARE(vf(Upscaler::IntelVsr, 1080, 2160, 0, false), QStringLiteral("d3d11vpp=scaling-mode=intel:scale=2.00"));
        QCOMPARE(vf(Upscaler::IntelVsr, 1080, 2160, 0, true),
                 QStringLiteral("d3d11vpp=scaling-mode=intel:scale=2.00:intel-copyback=yes"));
        QCOMPARE(vf(Upscaler::NvidiaVsr, 720, 1440, 0, true), QStringLiteral("d3d11vpp=scaling-mode=nvidia:scale=2.00"));
        QCOMPARE(vf(Upscaler::IntelVsr, 2160, 1440, 0, true), QString());
        // Nothing to make bigger, not VSR, or not known yet: no filter.
        QCOMPARE(vf(Upscaler::NvidiaVsr, 2160, 1440, 0), QString());
        QCOMPARE(vf(Upscaler::Fsr, 720, 1440, 0), QString());
        QCOMPARE(vf(Upscaler::NvidiaVsr, 0, 1440, 0), QString());
        QCOMPARE(vf(Upscaler::NvidiaVsr, 720, 0, 0), QString());

        // The line on screen: VSR says "on" only once the filter hands on
        // bigger frames, which is what shows the driver did the work.
        const auto status = MpvTarget::upscalerStatus;
        QCOMPARE(status(Upscaler::NvidiaVsr, 720, 1440, 0, true), QStringLiteral("NVIDIA VSR starting…"));
        QCOMPARE(status(Upscaler::NvidiaVsr, 720, 1440, 720, true), QStringLiteral("NVIDIA VSR starting…"));
        QCOMPARE(status(Upscaler::NvidiaVsr, 720, 1440, 1440, true), QStringLiteral("NVIDIA VSR on · 720p → 1440p"));
        QCOMPARE(status(Upscaler::IntelVsr, 1080, 1440, 1080, false), QStringLiteral("Intel VSR starting…"));
        QCOMPARE(status(Upscaler::NvidiaVsr, 2160, 1440, 2160, false), QStringLiteral("NVIDIA VSR idle · the video is already 2160p"));
        QCOMPARE(status(Upscaler::Fsr, 720, 1440, 720, false), QStringLiteral("AMD FSR 1 on · 720p → 1440p"));
        QCOMPARE(status(Upscaler::Fsr, 720, 0, 720, false), QStringLiteral("AMD FSR 1 on · 720p → screen size"));
        QCOMPARE(status(Upscaler::Sgsr, 720, 1440, 720, false), QStringLiteral("Snapdragon GSR on · 720p → 1440p"));
        QCOMPARE(status(Upscaler::Off, 720, 1440, 720, false), QString());
        QCOMPARE(status(Upscaler::NvidiaVsr, 0, 1440, 0, true), QString());  // no video yet
    }

    // Windows' per-program graphics card entry: read and rewritten without
    // losing the other settings Windows keeps in the same string.
    void graphicsCardPreferenceEntry()
    {
        QCOMPARE(GraphicsCards::gpuPreferenceIn(QString()), 0);
        QCOMPARE(GraphicsCards::gpuPreferenceIn(QStringLiteral("GpuPreference=2;")), 2);
        QCOMPARE(GraphicsCards::gpuPreferenceIn(QStringLiteral("SwapEffectUpgradeEnable=1;GpuPreference=1;")), 1);
        QCOMPARE(GraphicsCards::gpuPreferenceIn(QStringLiteral("GpuPreference=9;")), 0);
        QCOMPARE(GraphicsCards::withGpuPreference(QString(), 2), QStringLiteral("GpuPreference=2;"));
        QCOMPARE(GraphicsCards::withGpuPreference(QStringLiteral("GpuPreference=2;"), 0), QString());
        QCOMPARE(GraphicsCards::withGpuPreference(QStringLiteral("SwapEffectUpgradeEnable=1;GpuPreference=2;"), 1),
                 QStringLiteral("GpuPreference=1;SwapEffectUpgradeEnable=1;"));
        QCOMPARE(GraphicsCards::withGpuPreference(QStringLiteral("SwapEffectUpgradeEnable=1;GpuPreference=2;"), 0),
                 QStringLiteral("SwapEffectUpgradeEnable=1;"));
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

    // The relay a credentialed link plays through answers only the link it
    // gave out: GET or HEAD, its own Host (no DNS rebinding), ranges as the
    // origin allows, and nothing once the next item starts.
    void relayAnswersOnlyItsOwnRequests()
    {
        FakeHttpServer origin;
        origin.set("/clip.mkv", clipAnswer());
        ScopedHttp http;
        const QUrl relay = http.routeVideo(QUrl(origin.base() + "/clip.mkv?sig=1"), {{"Authorization", "Bearer r"}});
        QCOMPARE(relay.scheme(), QStringLiteral("http"));
        QCOMPARE(relay.host(), QStringLiteral("127.0.0.1"));
        QCOMPARE(relay.path(), QStringLiteral("/clip.mkv"));
        QCOMPARE(relay.query(), QStringLiteral("sig=1"));
        const quint16 port = quint16(relay.port());
        const QByteArray host = "Host: 127.0.0.1:" + QByteArray::number(port) + "\r\n";

        QCOMPARE(statusOf(ask(port, "GET /clip.mkv?sig=1 HTTP/1.1\r\nHost: evil.example\r\n\r\n")), 403);
        QCOMPARE(statusOf(ask(port, "GET /clip.mkv?sig=1 HTTP/1.1\r\n\r\n")), 403);
        QCOMPARE(statusOf(ask(port, "POST /clip.mkv HTTP/1.1\r\n" + host + "\r\n")), 405);
        QCOMPARE(statusOf(ask(port, "GET http://elsewhere.example/ HTTP/1.1\r\n" + host + "\r\n")), 400);
        QVERIFY(origin.requests.isEmpty());

        QFile file(kTestPattern);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QByteArray whole = file.readAll();
        const QByteArray full = ask(port, "GET /clip.mkv?sig=1 HTTP/1.1\r\n" + host + "\r\n");
        QCOMPARE(statusOf(full), 200);
        QCOMPARE(bodyOf(full), whole);
        const QByteArray tail = ask(port, "GET /clip.mkv?sig=1 HTTP/1.1\r\n" + host + "Range: bytes=1000-\r\n\r\n");
        QCOMPARE(statusOf(tail), 206);
        QVERIFY(tail.contains("Content-Range: bytes 1000-" + QByteArray::number(whole.size() - 1) + '/'
                              + QByteArray::number(whole.size())));
        QCOMPARE(bodyOf(tail), whole.mid(1000));
        // An HLS byte-range segment: exactly the bytes asked for.
        const QByteArray piece = ask(port, "GET /clip.mkv?sig=1 HTTP/1.1\r\n" + host + "Range: bytes=100-199\r\n\r\n");
        QCOMPARE(statusOf(piece), 206);
        QVERIFY(piece.contains("Content-Range: bytes 100-199/" + QByteArray::number(whole.size())));
        QCOMPARE(bodyOf(piece), whole.mid(100, 100));
        QCOMPARE(statusOf(ask(port, "GET /clip.mkv?sig=1 HTTP/1.1\r\n" + host + "Range: bytes=200-100\r\n\r\n")), 416);
        QCOMPARE(statusOf(ask(port, "GET /clip.mkv?sig=1 HTTP/1.1\r\n" + host + "Range: bytes="
                                     + QByteArray::number(whole.size()) + "-\r\n\r\n")),
                 416);
        QVERIFY(!origin.requests.isEmpty());
        for (const auto& request : std::as_const(origin.requests)) {
            QCOMPARE(request.headers.value("authorization"), QByteArray("Bearer r"));
            QCOMPARE(request.path, QStringLiteral("/clip.mkv?sig=1"));
        }

        // The next item: this port answers nothing more.
        http.reset();
        QTcpSocket late;
        late.connectToHost(QHostAddress::LocalHost, port);
        QTRY_VERIFY_WITH_TIMEOUT(late.state() == QAbstractSocket::UnconnectedState, 5000);
        QVERIFY(late.readAll().isEmpty());
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
    // Sends one raw request to the relay and returns the whole answer.
    static QByteArray ask(quint16 port, const QByteArray& request)
    {
        QTcpSocket socket;
        QByteArray answer;
        QObject::connect(&socket, &QTcpSocket::readyRead, &socket, [&] { answer += socket.readAll(); });
        socket.connectToHost(QHostAddress::LocalHost, port);
        if (!QTest::qWaitFor([&] { return socket.state() == QAbstractSocket::ConnectedState; }, 5000))
            return {};
        socket.write(request);
        if (!QTest::qWaitFor([&] { return socket.state() == QAbstractSocket::UnconnectedState; }, 15000))
            return {};
        answer += socket.readAll();
        return answer;
    }
    static int statusOf(const QByteArray& answer)
    {
        return answer.startsWith("HTTP/1.1 ") ? answer.mid(9, 3).toInt() : 0;
    }
    static QByteArray bodyOf(const QByteArray& answer)
    {
        const qsizetype end = answer.indexOf("\r\n\r\n");
        return end < 0 ? QByteArray() : answer.mid(end + 4);
    }
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
