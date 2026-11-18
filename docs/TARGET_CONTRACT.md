# Playback target contract

A **playback target** is a device that plays media: the embedded mpv player in v1, Chromecast later. The **Session** (`src/session`) drives exactly one target at a time and is the only thing that talks to it. The UI talks only to the Session.

Every target implements `cj::PlaybackTarget` (`src/session/PlaybackTarget.h`). This page says what each part means, so a new target can be written without reading the mpv one.

## Rules

1. **Commands never block.** A command starts the work and returns. Results arrive later as events.
2. **Events arrive on the Session's thread.** A target that works on other threads must hop back before emitting.
3. **The target holds no app state.** It does not remember what is "current", what was played before, or user preferences. The Session tells it everything it needs on every `load`.
4. **Unsupported means unsupported.** A target lists its capabilities. The Session never sends a command the target does not support, and the UI hides or disables that control.
5. **Times are seconds** as `double`. Volume is 0 to `PlaybackTarget::kMaxVolume` (130); above 100 boosts. Speed is a factor (1.0 = normal).

## Commands

| Command | Meaning |
| --- | --- |
| `load(source, startSeconds)` | Replace whatever is playing with `source`, starting at `startSeconds` (0 = beginning), and play. |
| `play()` / `pause()` | Resume or pause. Ignored when nothing is loaded. |
| `stop()` | Unload the current item. The target becomes idle. |
| `seek(seconds)` | Jump to an absolute position. |
| `setAudioTrack(id)` | Select an audio track by its `Track::id`. |
| `setSubtitleTrack(id)` | Select a subtitle track by id, or `-1` for none. |
| `addExternalSubtitle(url)` | Load a subtitle file and select it. It appears in the next `tracksChanged`. |
| `addSubtitleTrack(subtitle)` | Add a subtitle (URL, language, label) as a track to pick. Allowed while loading: the target keeps it until the item is loaded, and its usual rules may then show it, the way players show a subtitle next to a video. Added after loading, it is not switched on. |
| `setSpeed(factor)` | Playback speed. |
| `setVolume(0–130)` / `setMuted(bool)` | Output volume and mute. |

### Optional commands (behind a capability)

| Capability | Command | Meaning |
| --- | --- | --- |
| `SubtitleDelay` | `setSubtitleDelay(seconds)` | Shift subtitles later (+) or earlier (−). |
| `SubtitleStyle` | `setSubtitleScale(factor)`, `setSubtitleStyle(style)` | Subtitle size (1.0 = default), and font, color, outline, background box, and whether styled (ASS) subtitles are restyled too. |
| `AudioDelay` | `setAudioDelay(seconds)` | Shift audio later (+) or earlier (−). |
| `Chapters` | `seekChapter(index)` | Jump to a chapter from `chaptersChanged`. |
| `FrameStep` | `frameStep(forward)` | Pause and move one frame. **Local only.** |
| `Screenshot` | `screenshot(filePath)` | Save the current frame as an image. **Local only.** |
| `AbLoop` | `setAbLoop(a, b)` | Repeat between two positions; `-1, -1` clears it. |
| `AspectRatio` | `setAspectRatio(text)` | Override the aspect, e.g. `"16:9"`; `""` = automatic. |
| `Crop` | `setCrop(text)` | Crop to an aspect, e.g. `"2.39:1"`; `""` = none. **Local only.** |
| `VideoEqualizer` | `setVideoEqualizer(eq)` | Brightness, contrast, saturation, gamma, hue, each −100…100. **Local only.** |
| `HardwareDecoding` | `setHardwareDecoding(bool)` | Allow GPU decoding. **Local only.** |
| `UpscaleSgsr`, `UpscaleCas`, `UpscaleFsr`, `UpscaleAnime4k`, `UpscaleNvidiaVsr`, `UpscaleIntelVsr`, `UpscaleMetalFx` | `setUpscaler(upscaler, targetHeight)` | Upscale the picture with that method, to `targetHeight` pixels high (0 = the size it is shown at). Each method has its own capability, so the UI lists only those this computer can run. `Off` is always allowed and must leave nothing loaded. **Local only.** |
| `NativeKeys` | `keyEvent(key, modifiers, text, pressed)` | A key over the video, in Qt's terms. The target runs its own binding for it (mpv's input.conf) and reports what changed with `settingChanged`. `key` 0 with `pressed` false releases every held key. |

`setFullScreen(bool)` has no capability: the window owns full screen, and a target with its own key for it hears about every change so the two agree.

"Local only" marks features that need the video to be decoded and drawn on this computer. A Cast target will not have them, because the TV does the decoding.

## Events

| Event | When |
| --- | --- |
| `stateChanged(state)` | `Idle`, `Loading`, `Playing`, `Paused` or `Ended`. |
| `positionChanged(seconds)` | Often while playing, and after every seek. |
| `durationChanged(seconds)` | When known; 0 for live or unknown. |
| `tracksChanged(tracks)` | The full list of audio, video and subtitle tracks, with which are selected. |
| `chaptersChanged(chapters)` | The full chapter list. |
| `bufferingChanged(buffering, percent)` | When the target has to wait for data, and when it stops waiting. |
| `volumeChanged(volume, muted)` | When output volume or mute changes, from any cause. |
| `errorOccurred(error, message)` | Something failed. `error` is a `PlaybackError` (see below). |
| `statsChanged(stats)` | Free-form diagnostics for the debug overlay, as name → text. |
| `settingChanged(name, value)` | The target changed a setting by itself, from its own keys: `speed`, `subtitleDelay`, `audioDelay`, `subtitleScale`, `abLoopA`, `abLoopB` (−1 = unset), `aspectRatio` (width ÷ height, 0 = the file's own), `brightness`, `contrast`, `saturation`, `gamma`, `hue`, `hardwareDecoding`, `fullScreen`, `upscaler` (`"off"` when the target had to turn its upscaler off, because the graphics driver could not run it). The Session takes the value and does not send it back. |
| `upscalerStatusChanged(status, problem)` | What the upscaler is doing, as one line the UI shows as it is (`NVIDIA VSR on · 720p → 1440p`); empty when none runs. A target says "on" only when it can tell the upscale happened (for VSR: the filtered frames are bigger than the video). `problem` is true when it had to turn the upscaler off, and the line says so. |
| `appActionRequested(action)` | The target's own keys asked for something only the app does: `stop` (close the video) or `stats` (show or hide the stats). |

When the item reaches its end the target sends `stateChanged(Ended)` and keeps the last frame. It does not unload.

## Errors

`PlaybackError` is typed so the UI can explain each case differently:

| Error | Meaning |
| --- | --- |
| `UnsupportedFile` | The source opened but contains nothing this target can play. |
| `LoadFailed` | The source could not be opened: missing, no access, broken stream. |
| `NoPeers` | A torrent: nobody sharing it was found in time. Reported by the torrent transport, not by targets. |
| `StreamFailed` | A torrent stopped with an error, such as a full disk. Reported by the torrent transport. |
| `TargetUnavailable` | The device is gone or not responding (e.g. a TV switched off). |
| `Internal` | A bug or an unexpected failure inside the target. |

## Testing

`tests/FakeTarget.h` implements this contract. It records every command as a line of text and lets a test emit any event, so Session behaviour is tested without mpv.
