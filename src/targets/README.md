# targets — Playback targets

Runs commands on one device. Keeps no app state of its own; the Session owns that.

- `MpvTarget`: mpv on this computer, one instance created once and reused. Supports every optional feature in the contract.
- `shaders/fsr1.glsl`: AMD FSR 1 as an mpv shader, ported from AMD's MIT-licensed source. `MpvTarget` fills in the target size and writes it to a temporary file when FSR is picked; Off removes it.
- NVIDIA and Intel Video Super Resolution (Windows): `MpvTarget` checks the first graphics card's name once at start, and while VSR is picked it decodes to Direct3D 11 frames and adds mpv's `d3d11vpp` filter. Our Windows mpv has a patch for this (`packaging/windows/mpv-patches`).
- Apple MetalFX (macOS 13 and later): `MetalFxUpscaler` lets mpv draw a small frame into an IOSurface that OpenGL and Metal share, runs MetalFX's spatial scaler on it, and draws the result into the video surface. Only `MpvVideoItem` uses it, while MetalFX is picked; turning it off frees its textures.
- `MpvVideoItem`: the video surface the interface places in its window (`VideoSurface` in QML). It draws the mpv target's frames when that target is the Session's current one.
- `SleepBlocker`: keeps the screen awake while a video plays.

Cast is designed for (it will list fewer capabilities) but built later.
