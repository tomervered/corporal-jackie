# Corporal Jackie

A native desktop media player that browses Stremio add-on catalogs and local files, streams torrents with fast start and seeking, and plays everything through one embedded mpv player.

Built with C++20, Qt 6/QML, libmpv, libtorrent-rasterbar and SQLite. Windows and macOS first, Linux after.

**Status:** Step 6 (library and watch history) done. Browses Stremio add-on catalogs and your own folders, and plays local video files, web links, magnet links, torrent info hashes and .torrent files. The interface talks only to the Session, which sends commands to the mpv target; the contract between them is in [docs/TARGET_CONTRACT.md](docs/TARGET_CONTRACT.md). Torrents reach mpv through a `torrent://` stream protocol backed by the stream broker, with no local HTTP server; the broker's contract is in [docs/BROKER_CONTRACT.md](docs/BROKER_CONTRACT.md) and the protocol is described in [src/transports/README.md](src/transports/README.md). [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) maps the layers, the threads, and how a byte gets from a peer to the screen.

When nothing plays, the window shows Home: Continue Watching, then a row for each list of your add-ons and folders. The side bar has Discover (one list at a time, by type and genre), Library, Add-ons (install, order, remove) and Your folders; the search box on Home searches every add-on at once. Pick an item, then an episode, then one of the ways to play it, best first. Press **Browse** while watching to open the same pages over the video. Everything works by keyboard: Tab moves between controls and rows, arrow keys move inside them, Enter opens and Escape goes back. Cinemeta (Stremio's catalog server) gives lists and details if you allow it on the first-run screen; streams need a stream add-on. Subtitle files next to a video load with it. How it works: [src/sources/README.md](src/sources/README.md).

What the app sends and keeps is listed in [docs/PRIVACY.md](docs/PRIVACY.md), which also ships in the Windows and macOS packages.

Corporal Jackie is a hobby project. It is not affiliated with, endorsed by or sponsored by Stremio; it can use Stremio-compatible add-ons, and Cinemeta is Stremio's service. The Windows and macOS builds are not code-signed, so Windows SmartScreen and macOS Gatekeeper will warn on first run, and updates are manual: download the new build.

Corporal Jackie comes with no streams of its own. Use it only for videos you have the right to watch and share. While a torrent plays, the app also uploads it to other peers, and they can see your IP address.

To play a torrent, choose "Open a link…" (U on the pages) and paste a magnet link, drop it on the window, or pass it on the command line. While it loads, the window says what it is doing: looking for peers, getting the file list, buffering with a percentage, or waiting for peers when the swarm goes quiet.

## Using it

Open a file with the **Open** button, by dropping it on the window, or by passing its path on the command line. Drop a subtitle file on a playing video to add it.

Over the video, the keyboard is mpv's: every key goes to mpv and does what mpv's standard [input.conf](https://github.com/mpv-player/mpv/blob/master/etc/input.conf) says, so the app's keys and mpv's never clash. Options › "Keyboard keys (mpv's)" lists the common ones. Three keys differ from mpv, because in mpv they need a Lua script or end the player:

| Key | Action |
| --- | --- |
| Q, Ctrl+W | Close the video and go back to Home (mpv quits) |
| I | The app's stats: decoder, codec, resolution, FPS, dropped frames |
| Del, ` | Nothing (mpv's on-screen controller and console) |

Browse, Settings and Open are buttons on the bar. On the pages (Home, Discover, Library...), P opens Settings, O opens a file, U a link, and B or Esc closes them over a playing video.

When a video has chapters, the bar shows **⏭** (next track) and **Tracks**, a list of every chapter with its start time; pick one to jump there.

## Layout

One folder per layer under `src/`. Each folder's README says what the layer owns and what it must never do.

```
src/app         entry point
src/launcher    Windows launcher at the top of the shipped folder
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

You need CMake 3.21+, Ninja, a C++20 compiler, and Qt 6, libmpv, SQLite, Boost headers and OpenSSL with their development files. libtorrent is built from source with a small patch (see below).

**Windows**: install [MSYS2](https://www.msys2.org), open the *UCRT64* shell, and run:

```sh
pacman -S --needed mingw-w64-ucrt-x86_64-{toolchain,cmake,ninja,pkgconf,qt6-base,qt6-declarative,qt6-tools,mpv,boost,openssl,sqlite3}
```

**macOS**:

```sh
brew install qt mpv boost openssl@3 ninja pkgconf
export CMAKE_PREFIX_PATH="$(brew --prefix qt)"
```

**Linux (Ubuntu 24.04)**:

```sh
sudo apt install ninja-build pkg-config qt6-base-dev qt6-declarative-dev \
  qml6-module-qtquick qml6-module-qtquick-window qml6-module-qtqml-workerscript \
  qml6-module-qtquick-controls qml6-module-qtquick-templates qml6-module-qtquick-dialogs \
  qml6-module-qtquick-layouts qml6-module-qt-labs-folderlistmodel qml6-module-qtquick-shapes \
  libmpv-dev libboost-dev libssl-dev libsqlite3-dev
```

Then, on any platform, build libtorrent once (about four minutes), and the app against it:

```sh
third_party/libtorrent/build.sh "$HOME/cj-libtorrent"
cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH="$HOME/cj-libtorrent"
cmake --build build
ctest --test-dir build -LE bench --output-on-failure
ctest --test-dir build -L bench -V    # the torrent benchmark (docs/BROKER_CONTRACT.md)
```

On macOS add the Qt prefix: `-DCMAKE_PREFIX_PATH="$(brew --prefix qt);$HOME/cj-libtorrent"`. The patch keeps playback from stalling when a seeder leaves ([docs/ENGINE.md](docs/ENGINE.md)). Any other libtorrent 2.x still builds and works, with a configure warning, and CI passes `-DCJ_REQUIRE_PATCHED_LIBTORRENT=ON` to make that an error.

CI builds with `-DCJ_WARNINGS_AS_ERRORS=ON`, so any compiler warning fails the build, and runs the tests a second time on Linux built with `-DCJ_SANITIZE=ON` (AddressSanitizer and UndefinedBehaviorSanitizer). `tst_ui` loads the real window against a fake player and fails on any QML warning; set `CJ_UI_SHOTS` to a folder to get a picture of each page.

## Downloading a CI build

Every push builds Windows and macOS packages. Open the latest run under **Actions**, and download `CorporalJackie-windows-x64` or `CorporalJackie-macos-arm64` from its Artifacts section.

The Windows folder holds `CorporalJackie.exe` (start this one), the license files and an `app` folder with the real program and everything it needs. Keep `app` next to the exe. The top `CorporalJackie.exe` is a small launcher, because Windows only finds a program's DLLs when they sit next to it.

The builds are not signed yet. On macOS, right-click the app and choose **Open** the first time. On Windows, choose **More info → Run anyway** if SmartScreen asks.

## Logs

Set `CJ_MPV_LOG=v` to include mpv's detailed log, and `CJ_MPV_OPTS="name=value,..."` to pass extra mpv options.

Logs are written to the `logs` folder inside the app's data folder (for example `%APPDATA%\Corporal Jackie\logs` or `~/Library/Application Support/Corporal Jackie/logs`). Turn on detailed logs for a layer with `QT_LOGGING_RULES="cj.broker.debug=true"`.

## License

GPL-3.0-or-later. See [LICENSE](LICENSE) and [docs/DEPENDENCIES.md](docs/DEPENDENCIES.md).

Each package also holds `THIRD_PARTY_NOTICES.txt`, a `licenses` folder with the license files of every library it ships, and `SOURCE.txt`, which links the exact source of the build and of each library.
