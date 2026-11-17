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

    spacing: 10
    PageTitle { text: "Add-ons" }
    Muted {
        width: parent.width
        text: "Add-ons give the lists, details and streams. Paste an add-on's install link (it ends in manifest.json). WikiMaze, if you allowed it on the first-run screen or in Settings, has lists and details, not streams. Add-ons higher up are asked first, and their lists come first on Home."
    }
    Row {
        width: parent.width
        spacing: 8
        AppTextField {
            id: addonField
            width: parent.width - installButton.width - 8
            placeholderText: "https://…/manifest.json"
            onAccepted: installButton.clicked()
        }
        AppButton {
            id: installButton
            primary: true
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
        radius: Theme.radius
        color: Theme.card
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
                    objectName: "addonConsentInstall"
                    primary: true
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
    Muted {
        width: parent.width
        visible: browser.addons.length === 0
        topPadding: 8
        text: "No add-ons installed. Paste an install link above."
    }
    ListView {
        id: addonList
        width: parent.width
        height: parent.height - y
        clip: true
        model: browser.addons
        spacing: 6
        topMargin: 4
        ScrollBar.vertical: ScrollBar {}
        delegate: Rectangle {
            id: addonCard
            required property var modelData
            required property int index
            // Remove asks once more: the add-on's link may be hard to find again.
            property bool confirmRemove: false
            width: ListView.view.width
            height: addonText.implicitHeight + 24
            radius: Theme.radius
            color: Theme.card
            border.color: modelData.error !== "" ? "#60e09080" : "transparent"
            Column {
                id: addonText
                anchors { left: parent.left; right: addonButtons.left; verticalCenter: parent.verticalCenter; leftMargin: 14; rightMargin: 12 }
                spacing: 3
                Row {
                    spacing: 8
                    Text { id: addonName; textFormat: Text.PlainText; text: modelData.name; color: Theme.text; font.pixelSize: 15; font.weight: Font.DemiBold }
                    Text { textFormat: Text.PlainText; anchors.baseline: addonName.baseline; text: modelData.version; color: Theme.faint; font.pixelSize: 12 }
                }
                Muted { width: parent.width; text: modelData.description; visible: text !== ""; maximumLineCount: 2; elide: Text.ElideRight }
                Muted { width: parent.width; text: "Offers " + modelData.offers; font.pixelSize: 12; color: Theme.faint }
                Muted { width: parent.width; visible: modelData.error !== ""; color: Theme.error; text: "Last request failed: " + modelData.error }
            }
            Row {
                id: addonButtons
                anchors { right: parent.right; verticalCenter: parent.verticalCenter; rightMargin: 12 }
                spacing: 4
                AppButton {
                    text: "↑"; implicitWidth: 36; leftPadding: 0; rightPadding: 0
                    visible: browser.addons.length > 1
                    enabled: addonCard.index > 0
                    onClicked: browser.moveAddon(addonCard.modelData.id, -1)
                    ToolTip.visible: hovered; ToolTip.text: "Ask this one earlier"
                }
                AppButton {
                    text: "↓"; implicitWidth: 36; leftPadding: 0; rightPadding: 0
                    visible: browser.addons.length > 1
                    enabled: addonCard.index < browser.addons.length - 1
                    onClicked: browser.moveAddon(addonCard.modelData.id, 1)
                    ToolTip.visible: hovered; ToolTip.text: "Ask this one later"
                }
                AppButton {
                    text: addonCard.confirmRemove ? "Remove it?" : "Remove"
                    highlighted: addonCard.confirmRemove
                    onClicked: {
                        if (!addonCard.confirmRemove) { addonCard.confirmRemove = true; return }
                        browser.removeAddon(addonCard.modelData.id)
                    }
                    onActiveFocusChanged: if (!activeFocus) addonCard.confirmRemove = false
                }
            }
            HoverHandler { onHoveredChanged: if (!hovered) addonCard.confirmRemove = false }
        }
    }
}
