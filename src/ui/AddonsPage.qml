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
        text: "Add-ons give the lists, details and streams. Paste an add-on's install link (it ends in manifest.json). Two come installed unless you turned them off on the first-run screen or in Settings: WikiMaze, with lists and details but no streams, and Public Domain Movies, with films whose copyright has run out and their torrents. Add-ons higher up are asked first, and their lists come first on Home."
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
            id: addonCard
            required property var modelData
            required property int index
            width: ListView.view.width
            height: addonText.implicitHeight + 28
            radius: 8
            color: Theme.panel
            border.color: Theme.divider
            // A switched-off add-on stays here, dimmed.
            opacity: modelData.enabled ? 1 : 0.6
            Column {
                id: addonText
                anchors { left: parent.left; right: addonButtons.left; verticalCenter: parent.verticalCenter; margins: 14 }
                spacing: 3
                Text { textFormat: Text.PlainText; text: modelData.name + "  " + modelData.version; color: Theme.text; font.pixelSize: Theme.bodySize; font.weight: Font.DemiBold }
                Muted { width: parent.width; text: modelData.description; visible: text !== ""; maximumLineCount: 2; elide: Text.ElideRight }
                Muted { width: parent.width; text: "Protocol: " + modelData.protocol }
                Muted { width: parent.width; text: modelData.enabled ? "Offers: " + modelData.offers : "Switched off: gives no lists, details or streams" }
                Muted { width: parent.width; visible: modelData.error !== ""; color: Theme.error; text: "Last request failed: " + modelData.error }
                Muted { width: parent.width; visible: modelData.cached === true; text: "Showing cached results (" + modelData.cachedIn + "): this source did not answer." }
                // Which of its lists show up in Home, Discover and search.
                Text {
                    id: listsToggle
                    objectName: "listsToggle-" + modelData.id
                    visible: modelData.enabled && modelData.catalogs.length > 1
                    property bool expanded: false
                    readonly property int shown: modelData.catalogs.filter(c => c.enabled).length
                    textFormat: Text.PlainText
                    text: (expanded ? "Hide the list of lists" : "Lists shown: " + shown + " of " + modelData.catalogs.length + "  ·  choose")
                    color: Theme.link
                    font.underline: true
                    font.weight: Font.DemiBold
                    font.pixelSize: Theme.smallSize + 1
                    // Reachable by Tab, opened with Space or Enter, and named for screen readers.
                    activeFocusOnTab: true
                    Accessible.role: Accessible.Button
                    Accessible.name: text
                    Accessible.onPressAction: expanded = !expanded
                    Keys.onSpacePressed: expanded = !expanded
                    Keys.onReturnPressed: expanded = !expanded
                    Keys.onEnterPressed: expanded = !expanded
                    Rectangle {
                        anchors { fill: parent; margins: -3 }
                        visible: listsToggle.activeFocus
                        color: "transparent"
                        radius: 3
                        border.width: 2
                        border.color: Theme.accent
                    }
                    TapHandler { onTapped: listsToggle.expanded = !listsToggle.expanded }
                }
                Column {
                    width: parent.width
                    visible: listsToggle.visible && listsToggle.expanded
                    spacing: 2
                    Repeater {
                        model: modelData.catalogs
                        Row {
                            required property var modelData
                            spacing: 8
                            AppSwitch {
                                objectName: "catalogSwitch-" + parent.modelData.id + "-" + parent.modelData.type
                                anchors.verticalCenter: parent.verticalCenter
                                checked: parent.modelData.enabled
                                Accessible.name: (checked ? "Hide " : "Show ") + parent.modelData.name
                                onToggled: browser.setCatalogEnabled(addonCard.modelData.id, parent.modelData.type, parent.modelData.id, checked)
                            }
                            Muted { anchors.verticalCenter: parent.verticalCenter; text: parent.modelData.name }
                        }
                    }
                }
            }
            Row {
                id: addonButtons
                anchors { right: parent.right; verticalCenter: parent.verticalCenter; margins: 12 }
                spacing: 4
                AppSwitch {
                    objectName: "addonSwitch-" + modelData.id
                    anchors.verticalCenter: parent.verticalCenter
                    checked: modelData.enabled
                    Accessible.name: (modelData.enabled ? "Switch off " : "Switch on ") + modelData.name
                    ToolTip.visible: hovered
                    ToolTip.delay: 500
                    ToolTip.text: modelData.enabled ? "On: switch off without removing" : "Off: switch on"
                    onToggled: browser.setAddonEnabled(modelData.id, checked)
                }
                IconButton { iconName: "up"; takesFocus: true; enabled: index > 0; tip: "Move up (asked earlier)"; onClicked: browser.moveAddon(modelData.id, -1) }
                IconButton { iconName: "down"; takesFocus: true; enabled: index < browser.addons.length - 1; tip: "Move down"; onClicked: browser.moveAddon(modelData.id, 1) }
                AppButton { text: "Remove"; onClicked: browser.removeAddon(modelData.id) }
            }
        }
    }
}
