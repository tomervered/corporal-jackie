# Playback target contract

A **playback target** is a device that plays media: the embedded mpv player in v1, Chromecast later. The **Session** (`src/session`) drives exactly one target at a time and is the only thing that talks to it. The UI talks only to the Session.

Every target implements `cj::PlaybackTarget` (`src/session/PlaybackTarget.h`). This page says what each part means, so a new target can be written without reading the mpv one.

## Rules

1. **Commands never block.** A command starts the work and returns. Results arrive later as events.
2. **Events arrive on the Session's thread.** A target that works on other threads must hop back before emitting.
3. **The target holds no app state.** It does not remember what is "current", what was played before, or user preferences. The Session tells it everything it needs on every `load`.
4. **Unsupported means unsupported.** A target lists its capabilities. The Session never sends a command the target does not support, and the UI hides or disables that control.
5. **Times are seconds** as `double`. Volume is 0–100. Speed is a factor (1.0 = normal).

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
| `setVolume(0–100)` / `setMuted(bool)` | Output volume and mute. |

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
