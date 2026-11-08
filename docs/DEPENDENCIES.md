# Dependency register

Every library the app ships with or links. Rule 9 of the build plan: add a row here, and check its license, before merging anything that adds a dependency.

The app is licensed **GPL-3.0-or-later**, so every dependency must be GPL-3.0 compatible. All of the ones below are.

## Linked by our code

| Name | Version | License | How it's linked | How it arrives | Used by |
| --- | --- | --- | --- | --- | --- |
| Qt 6 (Core, Gui, Network, Qml, Quick, QuickControls2, OpenGL) | 6.4 or newer; Windows CI has 6.12 | LGPL-3.0 | Dynamic | MSYS2 / Homebrew / apt | ui, app, all layers (QtCore); sources (Network, HTTPS through Qt's TLS plugin) |
| libmpv | Windows: mpv 0.39.0 built by us; elsewhere 0.37 or newer from the package manager | GPL-2.0-or-later (GPL build) | Dynamic | Windows: `packaging/windows/build-mpv.sh`; Homebrew / apt | targets, transports (stream callback API) |
| libtorrent-rasterbar | 2.0 or newer; Windows CI has 2.1 | BSD-3-Clause | Dynamic | MSYS2 / Homebrew / apt | broker only |
| SQLite | 3.x | Public domain | Dynamic | MSYS2 / macOS SDK / apt | storage (Step 6) |

## Pulled in by those

On Windows the package holds exactly what `ldd` finds; each CI run prints the full DLL list in its Package step.

| Name | Comes with | License |
| --- | --- | --- |
| FFmpeg 7.1.1 (avcodec, avformat, avfilter, avutil, swscale), decoders only, built by us on Windows | mpv | GPL-2.0-or-later (built with `--enable-gpl --enable-version3`, so GPL-3.0) |
| libplacebo 7.349, built by us on Windows | mpv | LGPL-2.1-or-later |
| dav1d (AV1 decoder) | FFmpeg | BSD-2-Clause |
| libass, fribidi, HarfBuzz, graphite2, FreeType, Fontconfig, expat | mpv (subtitles) | ISC, LGPL-2.1+, MIT, LGPL-2.1+ or MPL, FreeType License, MIT-style, MIT |
| Little CMS 2, libdovi, zimg, uchardet | mpv / libplacebo (color, Dolby Vision, scaling, subtitle encodings) | MIT, MIT, WTFPL, MPL-1.1 / GPL-2.0+ / LGPL-2.1+ |
| GLib, PCRE2, libintl, libiconv | HarfBuzz / mpv | LGPL-2.1+, BSD-3-Clause, LGPL-2.1+, LGPL-2.1+ |
| ICU 78 | Qt | Unicode License v3 |
| libpng, libjpeg-turbo, zlib, zstd | Qt image plugins, FFmpeg | libpng License, IJG and BSD-3-Clause, zlib License, BSD-3-Clause |
| OpenSSL 3 | libtorrent | Apache-2.0 |
| Boost | libtorrent (headers) | BSL-1.0 |
| GCC runtime (libstdc++, libgcc_s) | MinGW toolchain on Windows | GPL-3.0 with the GCC Runtime Library Exception |

The small `CorporalJackie.exe` launcher on Windows is plain C, linked statically, and needs no DLLs.

Not used, though the build plan named it: KDE MpvQt. Step 1 wrote its own bridge on mpv's render API instead, so it is not a dependency.

## Notices in the packages

CI adds three things to each package: `THIRD_PARTY_NOTICES.txt` (from `packaging/`), a `licenses` folder with the license files of every library in the package, and `SOURCE.txt` with the exact versions and where to get their source. `packaging/windows/collect-licenses.sh` finds the MSYS2 package behind each shipped DLL and fetches the license files of FFmpeg, libplacebo and mpv at the tags `build-mpv.sh` builds. `packaging/macos/collect-licenses.sh` does the same for the Homebrew formulae behind each bundled library. A few packages ship no license file (MSYS2's ICU and libass, Homebrew's Qt and GLib); `packaging/licenses` holds theirs, taken from upstream. The Package step fails when a shipped library has no license file at all. When a dependency with a new license joins, add it to `THIRD_PARTY_NOTICES.txt` too.

## Version pinning

The Windows mpv, FFmpeg and libplacebo versions are fixed in `packaging/windows/build-mpv.sh`. Everything else takes the package manager's current version, which keeps CI simple while nothing depends on exact versions. The app logs the version of each library it actually loaded at startup, and CI prints them, so any change shows up in the logs.

Before the first public release (Step 9), pin the rest: a fixed Qt, fixed libmpv builds on macOS, and fixed libtorrent and SQLite versions.
