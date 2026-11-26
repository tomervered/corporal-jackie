import QtQuick

SettingRow {
    id: actionRow
    default property alias buttons: buttonRow.data
    Row { id: buttonRow; spacing: 6 }
}
