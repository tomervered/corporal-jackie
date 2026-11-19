import QtQuick

// A row of a player panel that opens its choices in place under it (speed,
// aspect ratio, subtitle size...). The panel keeps which one is open, so one
// closes when another opens. (A file of its own: as an inline component, its
// ids pointed at the last instance in the file on Qt 6.4.)
Column {
    id: choices
    property string key
    property string label
    property var options: []     // [[value, label], ...]
    property var current
    property bool open: false
    signal picked(var value)
    signal toggled()
    width: parent ? parent.width : 300
    PanelRow {
        objectName: "choice-" + choices.key
        label: choices.label
        value: (choices.options.find(o => o[0] === choices.current) || [null, ""])[1]
        expandable: true
        expanded: choices.open
        enabled: choices.enabled
        onClicked: choices.toggled()
    }
    Item {
        visible: choices.open
        width: parent.width
        height: visible ? optionColumn.implicitHeight + 6 : 0
        Rectangle { x: 10; y: 2; width: 1; height: optionColumn.implicitHeight; color: Theme.pLine }
        Column {
            id: optionColumn
            x: 18
            y: 2
            width: parent.width - 22
            Repeater {
                model: choices.open ? choices.options : []
                PanelRow {
                    required property var modelData
                    implicitHeight: 33
                    fontSize: 13
                    label: modelData[1]
                    selected: modelData[0] === choices.current
                    tickSpace: true
                    onClicked: choices.picked(modelData[0])
                }
            }
        }
    }
}
