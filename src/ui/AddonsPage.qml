import QtQuick
import QtQuick.Controls.Basic

// Add-ons: install by link, order, remove.
Column {
    required property Item nav   // the Pages: go(), back(), openItem() and the picked types
    function takeFocus() { addonField.forceActiveFocus() }

    spacing: 10
    PageTitle { text: "Add-ons" }
    Muted {
        width: parent.width
        text: "Add-ons give the lists, details and streams. Paste an add-on's install link (it ends in manifest.json). Cinemeta, if you allowed it on the first-run screen or in Settings, has lists and details, not streams. Add-ons higher up are asked first, and their lists come first on Home."
    }
    Row {
        width: parent.width
        spacing: 8
        TextField {
            id: addonField
            width: parent.width - installButton.width - 8
            placeholderText: "https://…/manifest.json"
            onAccepted: installButton.clicked()
        }
        Button {
            id: installButton
            text: browser.installing ? "Installing…" : "Install"
            enabled: !browser.installing && addonField.text.trim() !== ""
            onClicked: { browser.installAddon(addonField.text); addonField.text = "" }
        }
    }
    ListView {
        width: parent.width
        height: parent.height - y
        clip: true
        model: browser.addons
        spacing: 4
        ScrollBar.vertical: ScrollBar {}
        delegate: Rectangle {
            required property var modelData
            required property int index
            width: ListView.view.width
            height: addonText.implicitHeight + 20
            radius: 8
            color: "#14ffffff"
            Column {
                id: addonText
                anchors { left: parent.left; right: addonButtons.left; verticalCenter: parent.verticalCenter; margins: 10 }
                spacing: 2
                Text { text: modelData.name + "  " + modelData.version; color: Theme.text; font.pixelSize: 14; font.weight: Font.DemiBold }
                Muted { width: parent.width; text: modelData.description; visible: text !== ""; maximumLineCount: 2; elide: Text.ElideRight }
                Muted { width: parent.width; text: "Offers: " + modelData.offers }
                Muted { width: parent.width; visible: modelData.error !== ""; color: Theme.error; text: "Last request failed: " + modelData.error }
            }
            Row {
                id: addonButtons
                anchors { right: parent.right; verticalCenter: parent.verticalCenter; margins: 10 }
                spacing: 4
                Button { text: "↑"; enabled: index > 0; onClicked: browser.moveAddon(modelData.id, -1); ToolTip.visible: hovered; ToolTip.text: "Move up" }
                Button { text: "↓"; enabled: index < browser.addons.length - 1; onClicked: browser.moveAddon(modelData.id, 1); ToolTip.visible: hovered; ToolTip.text: "Move down" }
                Button { text: "Remove"; onClicked: browser.removeAddon(modelData.id) }
            }
        }
    }
}
