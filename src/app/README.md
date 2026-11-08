# app — Entry point

- `AppCore`: everything but the window: the database, preferences, the Session and how it is wired to the torrent transport, the source adapters, the browse model, the library and the caches. Tested in `tests/tst_app` and `tests/tst_ui`.
- `Preferences`, `Caches`: what Settings shows for torrent storage and the two caches.
- `main.cpp`: logging and crash reports, the mpv target, the QML window, and the `--smoke-test` CI check (which also fails on any QML warning).
