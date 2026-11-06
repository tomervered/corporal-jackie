# ui — Qt/QML interface

Owns pages, controls and user input. Never calls mpv, libtorrent or the network directly: it only talks to the Playback Session and to storage through their contracts. Arrives in Step 1 (controls over video) and Step 7 (full interface).
