# Corporal Jackie

A native desktop media player that browses Stremio add-on catalogs and local files, streams torrents with fast start and seeking, and plays everything through one embedded mpv player.

Built with C++20, Qt 6/QML, libmpv, libtorrent-rasterbar and SQLite. Windows and macOS first, Linux after.

**Status:** Step 1 (video in a native window). Plays local video files with controls drawn over the video.

## Using it

Open a file with the **Open** button, by dropping it on the window, or by passing its path on the command line.

| Key | Action |
| --- | --- |
| Space or K | Play / pause |
| ← / → | Seek 5 seconds (hold Shift for 60) |
| ↑ / ↓ | Volume |
| M | Mute |
| F or double-click | Full screen |
| I | Debug overlay: decoder, codec, resolution, FPS, dropped frames |
| H | Hardware decoding on/off (remembered) |
| O | Open a file |

## Layout

One folder per layer under `src/`. Each folder's README says what the layer owns and what it must never do.

```
src/app         entry point
src/ui          Qt/QML interface
src/session     Playback Session
src/targets     playback targets (mpv)
src/sources     source adapters (Stremio, local files)
src/transports  how readers reach the broker
src/broker      torrent stream broker
src/storage     SQLite storage
src/common      logging, crash reports, app folders
tests/          Qt Test suites, run by ctest
```

## Building

You need CMake 3.21+, Ninja, a C++20 compiler, and Qt 6, libmpv, libtorrent-rasterbar 2.x and SQLite with their development files.

**Windows**: install [MSYS2](https://www.msys2.org), open the *UCRT64* shell, and run:

```sh
pacman -S --needed mingw-w64-ucrt-x86_64-{toolchain,cmake,ninja,pkgconf,qt6-base,qt6-declarative,qt6-tools,mpv,libtorrent-rasterbar,sqlite3}
```

**macOS**:

```sh
brew install qt mpv libtorrent-rasterbar ninja pkgconf
export CMAKE_PREFIX_PATH="$(brew --prefix qt)"
```

**Linux (Ubuntu 24.04)**:

```sh
sudo apt install ninja-build pkg-config qt6-base-dev qt6-declarative-dev \
  qml6-module-qtquick qml6-module-qtquick-window qml6-module-qtqml-workerscript \
  qml6-module-qtquick-controls qml6-module-qtquick-templates qml6-module-qtquick-dialogs \
  qml6-module-qtquick-layouts qml6-module-qt-labs-folderlistmodel qml6-module-qtquick-shapes \
  libmpv-dev libtorrent-rasterbar-dev libsqlite3-dev
```

Then, on any platform:

```sh
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
```

## Downloading a CI build

Every push builds Windows and macOS packages. Open the latest run under **Actions**, and download `CorporalJackie-windows-x64` or `CorporalJackie-macos-arm64` from its Artifacts section.

The builds are not signed yet. On macOS, right-click the app and choose **Open** the first time. On Windows, choose **More info → Run anyway** if SmartScreen asks.

## Logs

Set `CJ_MPV_LOG=v` to include mpv's detailed log, and `CJ_MPV_OPTS="name=value,..."` to pass extra mpv options.

Logs are written to the `logs` folder inside the app's data folder (for example `%APPDATA%\Corporal Jackie\logs` or `~/Library/Application Support/Corporal Jackie/logs`). Turn on detailed logs for a layer with `QT_LOGGING_RULES="cj.broker.debug=true"`.

## License

GPL-3.0-or-later. See [LICENSE](LICENSE) and [docs/DEPENDENCIES.md](docs/DEPENDENCIES.md).
