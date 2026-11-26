import QtQuick

SettingRow {
    id: toggle
    property bool checked
    signal toggled(bool on)
    AppSwitch {
        enabled: toggle.enabledHere
        checked: toggle.checked
        Accessible.name: toggle.title
        onToggled: toggle.toggled(checked)
    }
}
