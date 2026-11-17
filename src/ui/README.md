# ui — Qt/QML interface

Owns pages, controls and user input. Never calls mpv, libtorrent or the network directly: it binds only to the context properties the app gives it (`session`, `browser`, `library`, `preferences`, `caches`).

- `Main.qml`: the window: video, keyboard and mouse, overlays, and where the parts below go.
- Player: `ControlBar`, `BarButton`, `PlainMenuItem`, `EpisodesPanel` (the episodes of the series that plays), `LinkPopup`, `Toast`, `StatsOverlay`, `ShortcutsOverlay`, and `Format.js` for times and rates.
- `Pages.qml`: the side bar, page history and shared keys. Each page is its own file (`HomePage`, `SearchPage`, `DiscoverPage`, `DetailPage`, `StreamsPage`, `LibraryPage`, `AddonsPage`, `FoldersPage`) and has `takeFocus()`.
- `SettingsPanel.qml`, `ContinueWatching.qml`.
- Pieces: `Theme` (colours and sizes, a singleton), `Muted`, `Heading`, `PageTitle`, `SectionLabel`, `RowItem`, `PosterTile`, `PosterRow`, `PosterGrid`, `PlaceholderTiles`, `NavButton`, `Chip`, `Icon` (drawn control-bar icons).
- Controls: `AppButton`, `AppTextField`, `AppComboBox` and `AppSwitch` are Basic style's controls with rounded corners and the app's colours. Use them instead of the plain ones. The rest of Basic style (menus, tool tips, scroll bars, the drop-down lists) takes its colours from the window's `palette` in `Main.qml`, which Basic style reads instead of the application palette.

The file list is in `QmlFiles.cmake`, used by the app and by `tests/tst_ui`, which loads this window against a fake player and fails on any QML warning.

## Text is plain text

Every `Text` sets `textFormat: Text.PlainText`, and `Chip` and `PlainMenuItem` replace Basic style's label with such a `Text`. Qt's default (`AutoText`) shows a string that starts with a tag as styled text, and styled text loads `<img src="...">` images, from any server. Titles, track and chapter names come from video files and add-ons, so a crafted file could make the app call a server of its choosing when a menu opened (security review 2026-11-15). `tst_ui` fails on any `Text` in the window that is not plain. A new `Text`, or a control that shows outside text, follows the same rule.
