import QtQuick
import QtQuick.Controls.Basic
import "Notices.js" as Notices

// Shown on first run, and again for an install that predates it, until the
// person answers. Covers the window; nothing is contacted before the answer.
// The switches start off, or as answered before when the screen asks again
// after gaining a question. The answer goes to `preferences`.
Rectangle {
    id: screen
    objectName: "privacyScreen"
    color: Theme.background

    // Keys and clicks stay here, away from the player and the pages below.
    MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons; hoverEnabled: true; onWheel: wheel => wheel.accepted = true }
    Keys.onPressed: event => event.accepted = true

    property bool catalogAddon: preferences.earlierChoices.catalogAddon === true
    property bool dht: preferences.earlierChoices.dht === true
    property bool trackers: preferences.earlierChoices.trackers === true
    property bool portMapping: preferences.earlierChoices.portMapping === true
    property bool torrents: false
    property bool uploads: false

    component Option: Item {
        id: option
        property string title
        property string detail
        property alias checked: sw.checked
        property alias switchEnabled: sw.enabled
        property int indent: 0
        objectName: "privacyOption"
        width: parent ? parent.width : 400
        opacity: sw.enabled ? 1 : 0.45
        height: Math.max(sw.implicitHeight, texts.implicitHeight) + 12
        Column {
            id: texts
            anchors { left: parent.left; leftMargin: option.indent; right: sw.left; rightMargin: 16; verticalCenter: parent.verticalCenter }
            spacing: 2
            Text { textFormat: Text.PlainText; width: parent.width; text: option.title; color: Theme.text; font.pixelSize: 15; font.weight: Font.DemiBold; wrapMode: Text.WordWrap }
            Text { textFormat: Text.PlainText; width: parent.width; text: option.detail; color: Theme.muted; font.pixelSize: 13; wrapMode: Text.WordWrap }
        }
        AppSwitch {
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
                textFormat: Text.PlainText
                width: parent.width
                text: "Before you start"
                color: Theme.text
                font.pixelSize: 24
                font.weight: Font.DemiBold
            }
            Text {
                textFormat: Text.PlainText
                width: parent.width
                text: "Corporal Jackie has no server and no account, and sends nothing to its authors. Everything below is off until you allow it, and each choice can be changed later in Settings (P)."
                color: Theme.text
                font.pixelSize: 14
                wrapMode: Text.WordWrap
            }
            Text {
                objectName: "copyrightNotice"
                textFormat: Text.PlainText
                width: parent.width
                text: Notices.copyright
                color: Theme.text
                font.pixelSize: 14
                wrapMode: Text.WordWrap
            }
            Item { width: 1; height: 4 }

            Option {
                id: torrentsOption
                objectName: "privacyTorrents"
                title: Notices.torrentsTitle
                detail: Notices.torrentsDetail
                checked: screen.torrents
                onCheckedChanged: { screen.torrents = checked; if (!checked) screen.uploads = false }
            }
            Option {
                id: uploadsOption
                objectName: "privacyUploads"
                indent: 16
                title: Notices.uploadsTitle
                detail: Notices.uploadsDetail
                switchEnabled: screen.torrents
                checked: screen.uploads
                onCheckedChanged: screen.uploads = checked
            }

            Option {
                id: catalogAddonOption
                title: "Browse and search with WikiMaze"
                detail: "WikiMaze is a catalog add-on with data from Wikidata and TVmaze, run by this app's author on Vercel. It sees your IP address and every title you search for or open. Off: no lists or search until you add another add-on."
                checked: screen.catalogAddon
                onCheckedChanged: screen.catalogAddon = checked
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
                textFormat: Text.PlainText
                width: parent.width
                text: "A VPN hides your address from peers; this app does not. With the DHT, trackers and port mapping off, only the peers and trackers in a magnet link are used, so some torrents will not start. The servers and peers belong to others and may be in other countries. Posters come from the servers your add-ons name. The full list is in PRIVACY.md, and the copyright policy in COPYRIGHT_POLICY.md, next to the app."
                color: Theme.muted
                font.pixelSize: 12
                wrapMode: Text.WordWrap
            }
            Item { width: 1; height: 4 }

            AppButton {
                objectName: "privacyContinue"
                primary: true
                text: "Continue with these choices"
                focusPolicy: Qt.NoFocus
                onClicked: preferences.choosePrivacy(screen.catalogAddon, screen.dht, screen.trackers, screen.portMapping,
                                                     screen.torrents, screen.torrents && screen.uploads)
            }
        }
    }
}
