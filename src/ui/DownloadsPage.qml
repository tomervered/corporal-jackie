import QtQuick
import QtQuick.Controls.Basic
import CorporalJackie.Player   // KeyedList

// Downloads: what is being saved and what was saved this time the app is
// open, with progress, sharing with peers, and the folder. Binds to
// `downloads`; the folder and the normal-torrent choice are in Settings.
Column {
    id: downloadsPage
    required property Item nav   // the Pages: openSettingsAt(), message()
    function takeFocus() { if (downloadList.count > 0) downloadList.forceActiveFocus(); else folderButton.forceActiveFocus() }

    spacing: 14
    Row {
        spacing: 8
        AppButton { id: folderButton; iconName: "folder"; text: "Open the download folder"; onClicked: downloads.openFolder() }
        AppButton { iconName: "settings"; text: "Download settings…"; onClicked: nav.openSettingsAt("Downloads") }
        AppButton {
            objectName: "downloadPlaying"
            visible: session.hasMedia && downloads.canDownloadPlaying()
            iconName: "download"
            text: "Download what's playing"
            onClicked: {
                const error = downloads.downloadPlaying()
                if (error !== "") nav.message(error, true)
            }
        }
        AppButton {
            visible: downloads.items.some(d => d.state === "done" || d.state === "failed" || d.state === "cancelled")
            iconName: "trash"
            text: "Clear finished"
            onClicked: downloads.clearFinished()
        }
    }
    Muted {
        width: parent.width
        text: "Saving to " + downloads.folder + (downloads.normalTorrents ? ". Torrents download like in a torrent program (rarest pieces first)." : ". Torrents download from the start, so you can play them while they download.")
    }
    Rectangle { width: parent.width; height: 1; color: Theme.divider }
    Muted {
        width: parent.width
        visible: downloadList.count === 0
        text: "Nothing downloading. Press the download button next to a way to play on a film or episode."
    }
    ListView {
        id: downloadList
        objectName: "downloadList"
        width: parent.width
        height: parent.height - y
        clip: true
        spacing: 8
        activeFocusOnTab: true
        keyNavigationEnabled: true
        // Updated in place twice a second, so a switch or the keyboard
        // focus is not thrown away with each update.
        model: KeyedList { keyRole: "id"; values: downloads.items }
        ScrollBar.vertical: ScrollBar {}
        delegate: Rectangle {
            id: row
            required property var entry
            required property int index
            readonly property bool running: entry.state === "starting" || entry.state === "downloading"
            width: ListView.view.width
            height: rowColumn.implicitHeight + 24
            radius: 8
            color: Theme.panel
            border.width: ListView.isCurrentItem && downloadList.activeFocus ? 2 : 1
            border.color: ListView.isCurrentItem && downloadList.activeFocus ? Theme.accent : Theme.divider

            Icon {
                id: stateIcon
                anchors { left: parent.left; leftMargin: 14; top: parent.top; topMargin: 14 }
                name: row.entry.state === "done" || row.entry.state === "seeding" ? "check"
                    : row.entry.state === "failed" || row.entry.state === "cancelled" ? "close" : "downloads"
                color: row.entry.state === "failed" ? Theme.error : row.running ? Theme.accent : Theme.muted
            }
            Column {
                id: rowColumn
                anchors { left: stateIcon.right; leftMargin: 12; right: rowTools.left; rightMargin: 12; top: parent.top; topMargin: 12 }
                spacing: 6
                Text {
                    textFormat: Text.PlainText
                    width: parent.width
                    text: row.entry.name
                    color: Theme.text
                    font.pixelSize: Theme.bodySize
                    elide: Text.ElideMiddle
                }
                // Progress: the bar fills as the file arrives.
                Rectangle {
                    visible: row.running
                    width: parent.width
                    height: 6
                    radius: 3
                    color: Theme.divider
                    Rectangle {
                        width: parent.width * Math.min(1, row.entry.percent / 100)
                        height: parent.height
                        radius: 3
                        color: Theme.accent
                        Behavior on width { NumberAnimation { duration: 300 } }
                    }
                }
                Text {
                    textFormat: Text.PlainText
                    width: parent.width
                    text: (row.running && row.entry.percent > 0 ? row.entry.percent + "%  ·  " : "") + row.entry.detail
                    color: row.entry.state === "failed" ? Theme.error : Theme.muted
                    font.pixelSize: Theme.smallSize
                    wrapMode: Text.WordWrap
                    elide: Text.ElideMiddle
                    maximumLineCount: 2
                }
            }
            Row {
                id: rowTools
                anchors { right: parent.right; rightMargin: 10; verticalCenter: parent.verticalCenter }
                spacing: 10
                // Sending pieces to other peers, for this download only.
                Row {
                    visible: row.entry.canShare
                    spacing: 8
                    anchors.verticalCenter: parent.verticalCenter
                    Text {
                        textFormat: Text.PlainText
                        anchors.verticalCenter: parent.verticalCenter
                        text: "Share with peers"
                        color: Theme.muted
                        font.pixelSize: Theme.smallSize
                    }
                    AppSwitch {
                        objectName: "shareSwitch"
                        anchors.verticalCenter: parent.verticalCenter
                        checked: row.entry.sharing
                        Accessible.name: "Share " + row.entry.name + " with peers"
                        onToggled: downloads.setSharing(row.entry.id, checked)
                    }
                }
                IconButton {
                    visible: row.entry.state === "done" || row.entry.state === "seeding"
                    anchors.verticalCenter: parent.verticalCenter
                    iconName: "folder"
                    iconSize: 18
                    tip: "Open the download folder"
                    onClicked: downloads.openFolder()
                }
                IconButton {
                    objectName: "cancelDownload"
                    visible: row.running
                    anchors.verticalCenter: parent.verticalCenter
                    iconName: "close"
                    iconSize: 18
                    tip: "Cancel this download"
                    onClicked: downloads.cancel(row.entry.id)
                }
            }
        }
        Keys.onDeletePressed: {
            const d = downloads.items[currentIndex]
            if (d) downloads.cancel(d.id)
        }
    }
}
