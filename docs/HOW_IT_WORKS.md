# How Corporal Jackie works, in plain words

This page is for someone who has never read code: a lawyer checking what the app does, a student, a new contributor on their first day. You don't need to know C++. If a word is new, the glossary at the bottom explains it. The technical map is [ARCHITECTURE.md](ARCHITECTURE.md).

## What the app is

Corporal Jackie is a video player for Windows and macOS. It plays a file from your computer, a link to a video on the web, or a torrent while it is still downloading. It does not provide, host or sell any movies or shows. What it can find to play depends on the add-ons you install and the folders you point it at. See [COPYRIGHT_POLICY.md](COPYRIGHT_POLICY.md).

Playing torrents, and uploading to other people while you do, are off until you turn them on. Nothing contacts an add-on, the torrent network or an open tracker until you answer the first-run screen. [PRIVACY.md](PRIVACY.md) lists what the app sends and keeps.

## What happens when you press Play

Say you pick a movie from an add-on, and the add-on offers a torrent.

1. **You click.** The window (the screens in `src/ui/`) tells the app which stream you picked.
2. **The app builds a "source".** A source is one thing that can be played: a file path, a web link or a torrent link, plus its title and subtitles. `src/sources/` makes sources.
3. **The torrent link becomes a private address.** The app swaps the magnet link for an address that only exists inside the app: `torrent://<fingerprint>/<file number>`. No web server runs on your computer. The code is in `src/transports/`.
4. **The player asks for the file.** The video player inside the app is mpv, a well-known open-source player. mpv sees the address and asks the app for the first bytes of the video.
5. **The torrent engine finds people who have the file.** It asks the torrent network, downloads the missing pieces from other people, and checks each piece is genuine. The code is in `src/broker/`, and it is the only place that talks to the torrent library.
6. **mpv gets its bytes, draws the picture, and plays the sound.** If the next piece has not arrived yet, mpv waits, and the app shows "buffering".
7. **The app remembers where you stopped**, if "Remember what I watch" is on. That list lives in a small database file on your computer (`src/storage/`).

## The main parts, and the folder each lives in

| Part | Folder | In one sentence |
|---|---|---|
| The window | `src/ui/` | The screens you see: Home, Discover, Library, Downloads, Settings, the player controls. |
| The app itself | `src/app/` | Starts everything in the right order (`main.cpp`), keeps your preferences, runs downloads. |
| The playing session | `src/session/` | Knows what is playing, the position, the volume, the subtitles. It tells the player what to do and never touches mpv directly. |
| The player | `src/targets/` | The only code that talks to mpv. |
| Where videos come from | `src/sources/` | Add-ons, your own folders, pasted links. |
| The link to mpv | `src/transports/` | Hands torrent bytes to mpv through the private `torrent://` address. |
| The torrent engine | `src/broker/` | Downloads pieces, in the order the player needs them. |
| Your data | `src/storage/` | Settings, the add-ons you installed, your library and your watch history, in one database file. |
| Shared helpers | `src/common/` | Log files, folder locations, crash reports, link safety checks. |

The rule that keeps this tidy: a part only calls the parts below it in the table. The window never touches the torrent engine, and the torrent engine does not know that mpv exists. That makes each part easy to test alone, and easy to replace.

## What is stored on your computer

- A database file with your settings, installed add-ons, library, watch history and the list of downloads.
- A cache folder with posters, and with torrent pieces if you chose the disk cache. In RAM mode, torrent pieces stay in memory and never reach your disk.
- Log files and crash reports, in the app's own folder. Paths and passwords are cut out before anything is written. The rules are in [PRIVACY.md](PRIVACY.md).

## How the code is checked

- Every part has automated tests in `tests/`. They run on Windows, macOS and Linux each time the code changes.
- One test opens the real window and fails if the screen code prints a single warning.
- A "smoke test" starts the finished app, plays a short clip, and quits. If the clip does not play, the build fails.
- Extra runs hunt for memory mistakes (ASan, UBSan) and for two threads stepping on each other (TSan).

## Reading the code

Start at `src/app/main.cpp`. The function `main()` is a list of steps: set the name, read the command line, open the log, build the app, load the window. Each step is a short function with a comment saying why it exists. Most long functions elsewhere follow the same pattern: the top function lists the steps, and each step has its own name.

## Glossary

- **Add-on:** a small web service that lists videos and where to get them. You install the ones you want.
- **Cache:** a folder or memory area where downloaded data waits until it is played.
- **Database (SQLite):** a single file that holds your settings and lists, so they survive a restart.
- **Magnet link:** a text link that names a torrent by its fingerprint. It holds no video.
- **mpv:** the open-source player library that decodes video and sound.
- **Peer:** another person's computer in a torrent. A **seeder** is a peer that has the whole file.
- **Piece:** a torrent is cut into pieces, each a few megabytes. The engine downloads and checks them one by one.
- **Torrent:** a way to download a file from many people at once, instead of from one server.
- **Fingerprint (info hash):** a code that identifies one torrent. Two torrents with the same fingerprint are the same torrent.
- **Qt and QML:** the toolkit that draws the window. QML is its language for screens.
- **Thread:** a worker inside the app. Several threads run at once, so a slow download never freezes the window.
- **Mutex (lock):** a rule that lets one thread at a time change a piece of shared data.
