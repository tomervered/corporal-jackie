import QtQuick
import QtQuick.Controls.Basic

// "Open a link": a magnet link, an info hash or a web address. Starts with
// what is on the clipboard.
Popup {
    id: popup
    signal openRequested(string text)   // the receiver closes the popup if it opened
    modal: true
    focus: true
    padding: 20
    background: Rectangle { radius: 12; color: Theme.overlay; border.color: "#30ffffff" }
    onOpened: { linkField.text = ""; linkField.paste(); linkField.selectAll(); linkField.forceActiveFocus() }
    Column {
        width: parent.width
        spacing: 12
        Text {
            text: "Open a link"
            color: Theme.text
            font.pixelSize: 18
            font.weight: Font.DemiBold
        }
        Text {
            width: parent.width
            wrapMode: Text.WordWrap
            text: "A magnet link, a torrent info hash, or a web address of a video."
            color: Theme.muted
            font.pixelSize: 13
        }
        TextField {
            id: linkField
            width: parent.width
            placeholderText: "magnet:?xt=urn:btih:…"
            onAccepted: popup.openRequested(text)
        }
        Row {
            anchors.right: parent.right
            spacing: 8
            Button { text: "Cancel"; onClicked: popup.close() }
            Button { text: "Open"; enabled: linkField.text.trim() !== ""; onClicked: popup.openRequested(linkField.text) }
        }
    }
}
