# session — Playback Session

Owns the current item, position, tracks, play state, per-item settings and typed errors. The interface binds only to `Session`; it never sees which target plays the video.

- `PlaybackTypes.h`: states, errors, capabilities, tracks, chapters.
- `PlaybackTarget.h`: the contract every target implements. It is described in words in [docs/TARGET_CONTRACT.md](../../docs/TARGET_CONTRACT.md).
- `Session`: sends commands to the target, turns its events into properties, skips optional features the target does not list, and emits a resume point every 5 seconds while playing, on pause, on stop and before another item replaces the current one (saved to disk in Step 6).

Must never include mpv or any other target's headers. Tested against `tests/FakeTarget.h`.
