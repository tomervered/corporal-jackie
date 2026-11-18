import QtQuick
import QtQuick.Controls.Basic
import "Notices.js" as Notices

// Add-ons: install by link, order, remove. Installing asks first: the
// add-on is contacted only after the person agrees to the notice.
Column {
    id: addonsPage
    objectName: "addonsPage"
    required property Item nav   // the Pages: go(), back(), openItem() and the picked types
    function takeFocus() { addonField.forceActiveFocus() }

    property string pendingLink: ""
    // scheme://host of a link: the rest can hold an add-on's settings or a login.
    function siteOf(link) {
        const m = /^\s*([a-z][a-z0-9+.-]*:\/\/[^\/?#]+)/i.exec(link)
        return m ? m[1] : link.trim()
    }

    spacing: 12
    Muted {
        width: parent.width
        text: "Add-ons give the lists, details and streams. Paste an add-on's install link (it ends in manifest.json). WikiMaze, if you allowed it on the first-run screen or in Settings, has lists and details, not streams. Add-ons higher up are asked first, and their lists come first on Home."
    }
    Row {
        width: parent.width
        spacing: 8
        AppField {
            id: addonField
            width: parent.width - installButton.width - 8
            placeholderText: "https://…/manifest.json"
            onAccepted: installButton.clicked()
        }
        AppButton {
            id: installButton
            primary: true
            iconName: "plus"
            text: browser.installing ? "Installing…" : "Install"
            enabled: !browser.installing && addonField.text.trim() !== ""
            onClicked: { addonsPage.pendingLink = addonField.text.trim(); addonField.text = "" }
        }
    }
    Rectangle {
        objectName: "addonConsent"
        visible: addonsPage.pendingLink !== ""
        width: parent.width
        height: visible ? consentColumn.implicitHeight + 24 : 0
        radius: 8
        color: Theme.raised
        border.color: Theme.accent
        Column {
            id: consentColumn
            x: 12; y: 12
            width: parent.width - 24
            spacing: 10
            Text {
                textFormat: Text.PlainText
                width: parent.width
                text: Notices.addonConsent(addonsPage.siteOf(addonsPage.pendingLink))
                color: Theme.text
                font.pixelSize: 13
                wrapMode: Text.WordWrap
            }
            Row {
                spacing: 8
                AppButton {
                    primary: true
                    objectName: "addonConsentInstall"
                    text: "Install"
                    onClicked: { browser.installAddon(addonsPage.pendingLink); addonsPage.pendingLink = "" }
                }
                AppButton {
                    objectName: "addonConsentCancel"
                    text: "Cancel"
                    onClicked: addonsPage.pendingLink = ""
                }
            }
        }
    }
    Heading { text: "Installed add-ons"; topPadding: 6 }
    ListView {
        width: parent.width
        height: parent.height - y
        clip: true
        model: browser.addons
        spacing: 6
        ScrollBar.vertical: ScrollBar {}
        delegate: Rectangle {
            required property var modelData
            required property int index
            width: ListView.view.width
            height: addonText.implicitHeight + 28
            radius: 8
            color: Theme.panel
            border.color: Theme.divider
            Column {
                id: addonText
                anchors { left: parent.left; right: addonButtons.left; verticalCenter: parent.verticalCenter; margins: 14 }
                spacing: 3
                Text { textFormat: Text.PlainText; text: modelData.name + "  " + modelData.version; color: Theme.text; font.pixelSize: Theme.bodySize; font.weight: Font.DemiBold }
                Muted { width: parent.width; text: modelData.description; visible: text !== ""; maximumLineCount: 2; elide: Text.ElideRight }
                Muted { width: parent.width; text: "Offers: " + modelData.offers }
                Muted { width: parent.width; visible: modelData.error !== ""; color: Theme.error; text: "Last request failed: " + modelData.error }
            }
            Row {
                id: addonButtons
                anchors { right: parent.right; verticalCenter: parent.verticalCenter; margins: 12 }
                spacing: 4
                IconButton { iconName: "up"; takesFocus: true; enabled: index > 0; tip: "Move up (asked earlier)"; onClicked: browser.moveAddon(modelData.id, -1) }
                IconButton { iconName: "down"; takesFocus: true; enabled: index < browser.addons.length - 1; tip: "Move down"; onClicked: browser.moveAddon(modelData.id, 1) }
                AppButton { text: "Remove"; onClicked: browser.removeAddon(modelData.id) }
            }
        }
    }
}
