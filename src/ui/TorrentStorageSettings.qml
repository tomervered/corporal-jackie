import QtQuick
import QtQuick.Dialogs

SettingsGroup {
    title: "Torrent storage"
    SettingSwitch {
        title: "Keep torrents in memory only"
        detail: "Nothing from the video is written to your disk, which spares an SSD. Jumping far back downloads that part again."
        checked: preferences.torrentsInMemory
        onToggled: on => preferences.torrentsInMemory = on
    }
    SettingChoice {
        visible: preferences.torrentsInMemory
        title: "Memory to use"
        options: [[512, "512 MB"], [1024, "1 GB"], [2048, "2 GB"], [4096, "4 GB"]]
        value: preferences.memoryCapMB
        onPicked: v => preferences.memoryCapMB = v
    }
    SettingChoice {
        visible: !preferences.torrentsInMemory
        title: "Disk space to use"
        detail: "Videos you watch stay on disk up to this size, so watching again starts at once. The oldest go first."
        options: [[5, "5 GB"], [10, "10 GB"], [20, "20 GB"], [50, "50 GB"]]
        value: preferences.diskCacheGB
        onPicked: v => preferences.diskCacheGB = v
    }
    SettingActions {
        visible: !preferences.torrentsInMemory
        title: "Cache folder"
        detail: preferences.cacheFolder + "\nPick a folder on a hard disk to keep torrent writes off your SSD."
        AppButton { text: "Change…"; onClicked: cacheFolderPicker.open() }
        AppButton {
            text: "Default"
            visible: preferences.customCacheFolder
            onClicked: preferences.resetCacheFolder()
        }
    }
    Rectangle {
        visible: preferences.restartNeeded
        width: parent.width
        height: restartNote.implicitHeight + 16
        radius: 6
        color: Theme.raised
        border.color: Theme.accent
        Text {
            textFormat: Text.PlainText
            id: restartNote
            anchors { fill: parent; margins: 8 }
            text: "Torrent storage changes apply the next time you open Corporal Jackie."
            color: Theme.text
            font.pixelSize: Theme.smallSize
            wrapMode: Text.WordWrap
        }
    }
    SettingActions {
        title: "Torrent cache on disk"
        detail: caches.busy ? "Measuring…" : caches.torrentCacheUsed !== "" ? "Using " + caches.torrentCacheUsed + ". What is playing now is kept." : ""
        AppButton { text: "Clear torrent cache"; enabled: !caches.busy; onClicked: caches.clearTorrents() }
    }

    FolderDialog {
        id: cacheFolderPicker
        title: "Choose where torrents are cached"
        onAccepted: preferences.setCacheFolder(selectedFolder)
    }
}
