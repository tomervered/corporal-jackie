import QtQuick

SettingRow {
    id: choice
    property var options: []      // [[value, label], ...]
    property var value
    signal picked(var value)
    AppCombo {
        width: 220
        enabled: choice.enabledHere
        Accessible.name: choice.title
        model: choice.options.map(o => o[1])
        currentIndex: choice.options.findIndex(o => o[0] === choice.value)
        displayText: currentIndex >= 0 ? currentText : ""
        onActivated: index => choice.picked(choice.options[index][0])
    }
}
