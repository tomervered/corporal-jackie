import QtQuick

SettingsGroup {
    title: "Privacy"
    SettingNote { text: "What Corporal Jackie may contact. Each applies at once. A VPN hides your IP address from torrent peers; this app does not. PRIVACY.md, next to the app, lists everything."; bottomPadding: 10 }
    SettingSwitch {
        title: "Use the built-in add-ons: WikiMaze and Public Domain Movies"
        detail: "WikiMaze (on Vercel, from this app's author) sees your IP address and every title you search for or open. Public Domain Movies (on Stremio's server) sees your IP address, its list pages and your searches in it. Off removes both from Add-ons."
        checked: preferences.useCatalogAddon
        onToggled: on => preferences.useCatalogAddon = on
    }
    SettingSwitch {
        title: "Find peers through the DHT and your local network"
        detail: "Tells unrelated computers the torrent you open and your IP address."
        checked: preferences.useDht
        onToggled: on => preferences.useDht = on
    }
    SettingSwitch {
        title: "Ask eight public trackers for peers"
        detail: "They learn your IP address and the torrent you open. A magnet link's own trackers are used either way."
        checked: preferences.usePublicTrackers
        onToggled: on => preferences.usePublicTrackers = on
    }
    SettingSwitch {
        title: "Let your router open a port (UPnP, NAT-PMP)"
        detail: "Lets more peers connect to you."
        checked: preferences.usePortMapping
        onToggled: on => preferences.usePortMapping = on
    }
}
