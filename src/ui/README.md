# ui — Qt/QML interface

Owns pages, controls and user input. Never calls mpv, libtorrent or the network directly: it binds only to the context properties the app gives it (`session`, `browser`, `library`, `preferences`, `caches`).

- `Main.qml`: the window: video, keyboard and mouse, overlays, and where the parts below go.
- Player: `ControlBar`, `BarButton`, `LinkPopup`, `Toast`, `StatsOverlay`, `ShortcutsOverlay`, and `Format.js` for times and rates.
- `Pages.qml`: the side bar, page history and shared keys. Each page is its own file (`HomePage`, `SearchPage`, `DiscoverPage`, `DetailPage`, `StreamsPage`, `LibraryPage`, `AddonsPage`, `FoldersPage`) and has `takeFocus()`.
- `SettingsPanel.qml`, `ContinueWatching.qml`.
- Pieces: `Theme` (colours and sizes, a singleton), `Muted`, `Heading`, `PageTitle`, `SectionLabel`, `RowItem`, `PosterTile`, `PosterRow`, `NavButton`, `Chip`.

The file list is in `QmlFiles.cmake`, used by the app and by `tests/tst_ui`, which loads this window against a fake player and fails on any QML warning.
