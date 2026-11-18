# targets — Playback targets

Runs commands on one device. Keeps no app state of its own; the Session owns that.

- `MpvTarget`: mpv on this computer, one instance created once and reused. Supports every optional feature in the contract.
- `shaders/sgsr1.glsl`: Snapdragon GSR 1, a one-pass upscaler for weak graphics chips, ported from Qualcomm's BSD-3-Clause shader. Handled like FSR.
- `shaders/cas.glsl`: AMD CAS with its upscale, one pass, ported from AMD's MIT-licensed `ffx_cas.h`.
- `shaders/anime4k_upscale_s.glsl`: Anime4K's smallest 2x upscaler (MIT), unchanged but for its `WHEN` lines.
- `shaders/fsr1.glsl`: AMD FSR 1 as an mpv shader, ported from AMD's MIT-licensed source. `MpvTarget` fills in the target size and writes it to a temporary file when FSR is picked; Off removes it.
- NVIDIA and Intel Video Super Resolution (Windows): `MpvTarget` checks the name of the graphics card the app runs on once at start (`GraphicsCards::activeVsr`), and while VSR is picked it decodes to Direct3D 11 frames and adds mpv's `d3d11vpp` filter. Our Windows mpv has a patch for this (`packaging/windows/mpv-patches`).
- `MpvVideoItem`: the video surface the interface places in its window (`VideoSurface` in QML). It draws the mpv target's frames when that target is the Session's current one.
- `GraphicsCards` (Windows): lists the graphics cards and reads or writes Windows' own per-program choice of card (Settings > System > Display > Graphics), shown as "Graphics card" in Settings. Windows applies it at the next start.
- `SleepBlocker`: keeps the screen awake while a video plays.

Cast is designed for (it will list fewer capabilities) but built later.
