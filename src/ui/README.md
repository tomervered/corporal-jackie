# ui — Qt/QML interface

Owns pages, controls and user input. Never calls mpv, libtorrent or the network directly: it binds only to the context properties the app gives it (`session`, `browser`, `library`, `preferences`, `caches`).

- `Main.qml`: the window: video, keyboard and mouse, overlays, and where the parts below go.
- Player: `ControlBar` (one row of icons, and the panels it opens: chapters, volume, audio and subtitles, playback settings), `PlayerPanel` and `PanelRow` (a panel and its rows), `BarButton`, `EpisodesPanel` (the episodes of the series that plays), `LinkPopup`, `Toast`, `StatsOverlay`, `ShortcutsOverlay`, and `Format.js` for times and rates.
- `Pages.qml`: the side bar, the header with the search of every add-on, page history and shared keys. Each page is its own file (`HomePage`, `SearchPage`, `DiscoverPage`, `DetailPage`, `StreamsPage`, `LibraryPage`, `AddonsPage`, `FoldersPage` (shown as Local), `SettingsPanel`) and has `takeFocus()`.
- `ContinueWatching.qml`.
- Pieces: `Theme` (the three looks, their colours, type sizes and spacing; a singleton that Main.qml points at the saved look), `Icon` with `Icons.js` (line icons drawn with Qt Quick Shapes from SVG path data), `IconButton`, `AppButton`, `AppField`, `AppCombo`, `AppSwitch`, `Muted`, `Heading`, `PageTitle`, `SectionLabel`, `RowItem`, `PosterTile`, `PosterRow`, `NavButton`, `Chip`. `images/logo.png` is the side bar's badge.

Use the App* controls, not Qt's Button, TextField, ComboBox and Switch: they take the look's colours, and AppCombo shows its entries as plain text. A Theme property must not be named on<Something> (QML reads that as a change handler).

The file list is in `QmlFiles.cmake`, used by the app and by `tests/tst_ui`, which loads this window against a fake player and fails on any QML warning.

## Text is plain text

Every `Text` sets `textFormat: Text.PlainText`, and `Chip` replaces Basic style's label with such a `Text` (the player's panels draw their own rows). Qt's default (`AutoText`) shows a string that starts with a tag as styled text, and styled text loads `<img src="...">` images, from any server. Titles, track and chapter names come from video files and add-ons, so a crafted file could make the app call a server of its choosing when a menu opened (security review 2026-11-15). `tst_ui` fails on any `Text` in the window that is not plain. A new `Text`, or a control that shows outside text, follows the same rule.
