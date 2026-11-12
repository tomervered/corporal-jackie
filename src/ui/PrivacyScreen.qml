import QtQuick
import QtQuick.Controls.Basic

// Shown on first run, and again for an install that predates it, until the
// person answers. Covers the window; nothing is contacted before the answer.
// The four switches start off. The answer goes to `preferences`.
Rectangle {
    id: screen
    objectName: "privacyScreen"
    color: Theme.background

    // Keys and clicks stay here, away from the player and the pages below.
    MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons; hoverEnabled: true; onWheel: wheel => wheel.accepted = true }
    Keys.onPressed: event => event.accepted = true

    property bool cinemeta: false
    property bool dht: false
    property bool trackers: false
    property bool portMapping: false

    component Option: Item {
        id: option
        property string title
        property string detail
        property alias checked: sw.checked
        objectName: "privacyOption"
        width: parent ? parent.width : 400
        height: Math.max(sw.implicitHeight, texts.implicitHeight) + 12
        Column {
            id: texts
            anchors { left: parent.left; right: sw.left; rightMargin: 16; verticalCenter: parent.verticalCenter }
            spacing: 2
            Text { width: parent.width; text: option.title; color: Theme.text; font.pixelSize: 15; font.weight: Font.DemiBold; wrapMode: Text.WordWrap }
            Text { width: parent.width; text: option.detail; color: Theme.muted; font.pixelSize: 13; wrapMode: Text.WordWrap }
        }
        Switch {
            id: sw
            anchors { right: parent.right; verticalCenter: parent.verticalCenter }
            focusPolicy: Qt.NoFocus
        }
    }

    Flickable {
        anchors.fill: parent
        contentWidth: width
        contentHeight: Math.max(height, column.implicitHeight + 64)
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        Column {
            id: column
            width: Math.min(620, parent.width - 48)
            anchors.horizontalCenter: parent.horizontalCenter
            y: Math.max(32, (parent.height - implicitHeight) / 2)
            spacing: 10

            Text {
                width: parent.width
                text: "Before you start"
                color: Theme.text
                font.pixelSize: 24
                font.weight: Font.DemiBold
            }
            Text {
                width: parent.width
                text: "Corporal Jackie has no server and no account, and sends nothing to its authors. Everything below is off until you allow it, and each choice can be changed later in Settings (P)."
                color: Theme.text
                font.pixelSize: 14
                wrapMode: Text.WordWrap
            }
            Item { width: 1; height: 4 }

            Option {
                id: cinemetaOption
                title: "Browse and search with Cinemeta"
                detail: "Cinemeta is Stremio's public catalog server. It sees your IP address and every title you search for or open. Off: no lists or search until you add another add-on."
                checked: screen.cinemeta
                onCheckedChanged: screen.cinemeta = checked
            }
            Option {
                id: dhtOption
                title: "Find peers through the DHT and your local network"
                detail: "Tells unrelated computers on the internet, and your network, the torrent you open and your IP address. More peers, faster start."
                checked: screen.dht
                onCheckedChanged: screen.dht = checked
            }
            Option {
                id: trackersOption
                title: "Ask eight public trackers for peers"
                detail: "These tracker servers learn your IP address and the torrent you open. A magnet link's own trackers are used either way."
                checked: screen.trackers
                onCheckedChanged: screen.trackers = checked
            }
            Option {
                id: portOption
                title: "Let your router open a port (UPnP, NAT-PMP)"
                detail: "Lets more peers connect to you. Your router is asked while a torrent is open."
                checked: screen.portMapping
                onCheckedChanged: screen.portMapping = checked
            }

            Item { width: 1; height: 2 }
            Text {
                width: parent.width
                text: "Playing a torrent always shows your IP address and the torrent to the people sharing it, and you upload to them while it plays. A VPN hides your address; this app does not. With all of these off, only the peers and trackers in a magnet link are used, so some torrents will not start. Posters come from the servers your add-ons name. The full list is in PRIVACY.md, next to the app."
                color: Theme.muted
                font.pixelSize: 12
                wrapMode: Text.WordWrap
            }
            Item { width: 1; height: 4 }

            Row {
                spacing: 8
                Button {
                    objectName: "privacyContinue"
                    text: "Continue with these choices"
                    focusPolicy: Qt.NoFocus
                    onClicked: preferences.choosePrivacy(screen.cinemeta, screen.dht, screen.trackers, screen.portMapping)
                }
                Button {
                    objectName: "privacyAllowAll"
                    text: "Allow all"
                    focusPolicy: Qt.NoFocus
                    onClicked: preferences.choosePrivacy(true, true, true, true)
                }
            }
        }
    }
}
