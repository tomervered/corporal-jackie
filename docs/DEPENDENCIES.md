# Dependency register

Every library the app ships with or links. Rule 9 of the build plan: add a row here, and check its license, before merging anything that adds a dependency.

The app is licensed **GPL-3.0-or-later**, so every dependency must be GPL-3.0 compatible.

| Name | Version | License | How it's linked | How it arrives | Used by |
| --- | --- | --- | --- | --- | --- |
| Qt 6 (Core, Gui, Qml, Quick) | 6.4 or newer; CI gets the current package | LGPL-3.0 | Dynamic | MSYS2 / Homebrew / apt | ui, app, all layers (QtCore) |
| libmpv | client API 2.x (mpv 0.37 or newer) | GPL-2.0-or-later (standard build) | Dynamic | MSYS2 / Homebrew / apt | targets |
| FFmpeg (through mpv) | whatever mpv is built against | GPL-2.0-or-later in these builds | Dynamic, indirect | With mpv | targets |
| libtorrent-rasterbar | 2.0.x | BSD-3-Clause | Dynamic | MSYS2 / Homebrew / apt | broker |
| Boost (through libtorrent) | whatever libtorrent is built against | BSL-1.0 | Headers and dynamic, indirect | With libtorrent | broker |
| OpenSSL (through libtorrent) | 3.x | Apache-2.0 | Dynamic, indirect | With libtorrent | broker |
| SQLite | 3.x | Public domain | Dynamic | MSYS2 / macOS SDK / apt | storage |

Planned, not added yet:

| Name | License | Step |
| --- | --- | --- |
| KDE MpvQt | LGPL-2.1-or-later | Step 1 |

## Version pinning

For now every platform takes its package manager's current version, which keeps CI simple while nothing depends on exact versions. The app logs the version of each library it actually loaded at startup, and CI prints them in the test output, so any change shows up in the logs.

Before the first public release (Step 9), pin exact versions: fixed Qt via aqtinstall, fixed libmpv dev builds per platform, and vcpkg with a baseline for libtorrent and SQLite.
