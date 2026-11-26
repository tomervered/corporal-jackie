import QtQuick
import QtQuick.Dialogs

SettingsGroup {
    objectName: "downloadsGroup"
    title: "Downloads"
    SettingActions {
        title: "Download folder"
        detail: downloads.folder + "\nVideos you download are saved here, apart from the torrent cache."
        AppButton { text: "Change…"; onClicked: downloadFolderPicker.open() }
        AppButton {
            text: "Default"
            visible: downloads.customFolder
            onClicked: downloads.resetFolder()
        }
    }
    SectionLabel { section: "Advanced" }
    SettingSwitch {
        objectName: "normalTorrentsToggle"
        title: "Download torrents like a normal torrent program"
        detail: "Pieces come rarest first and go straight into the folder, which can be faster on a big swarm, and a finished download can keep sharing. Off: the start of the video comes first, so you can play it while it downloads."
        checked: downloads.normalTorrents
        onToggled: on => downloads.normalTorrents = on
    }

    FolderDialog {
        id: downloadFolderPicker
        title: "Choose where downloads are saved"
        onAccepted: downloads.setFolder(selectedFolder)
    }
}
