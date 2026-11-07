# targets — Playback targets

Runs commands on one device. Keeps no app state of its own; the Session owns that.

- `MpvTarget`: mpv on this computer, one instance created once and reused. Supports every optional feature in the contract.
- `MpvVideoItem`: the video surface the interface places in its window (`VideoSurface` in QML). It draws the mpv target's frames when that target is the Session's current one.
- `SleepBlocker`: keeps the screen awake while a video plays.

Cast is designed for (it will list fewer capabilities) but built later.
