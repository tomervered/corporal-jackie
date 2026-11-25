import QtQuick
import "Notices.js" as Notices

SettingsGroup {
    title: "Torrents and copyright"
    SettingNote { text: Notices.copyright; topPadding: 4; bottomPadding: 10 }
    SettingSwitch {
        objectName: "settingsTorrents"
        title: Notices.torrentsTitle
        detail: Notices.torrentsDetail
        checked: preferences.useTorrents
        onToggled: on => {
            preferences.useTorrents = on
            if (!on) preferences.allowUploads = false
        }
    }
    SettingSwitch {
        objectName: "settingsUploads"
        title: Notices.uploadsTitle
        detail: Notices.uploadsDetail
        enabledHere: preferences.useTorrents
        checked: preferences.allowUploads
        onToggled: on => preferences.allowUploads = on
    }
}
