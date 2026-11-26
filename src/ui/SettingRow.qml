import QtQuick

Item {
    id: row
    property string title
    property string detail
    property bool enabledHere: true
    default property alias control: controlSlot.data
    // A wide control (the palette) on a narrow page goes under the words,
    // so they are not squeezed into a column of single words.
    readonly property bool stacked: controlSlot.width > width * 0.5
    width: parent ? parent.width : 400
    height: stacked ? texts.implicitHeight + 10 + controlSlot.height + 20
                    : Math.max(controlSlot.childrenRect.height, texts.implicitHeight) + 20
    opacity: enabledHere ? 1 : 0.5
    Rectangle { width: parent.width; height: 1; color: Theme.divider }
    Column {
        id: texts
        anchors { left: parent.left; right: row.stacked ? parent.right : controlSlot.left; rightMargin: row.stacked ? 0 : 22 }
        y: row.stacked ? 10 : (row.height - implicitHeight) / 2
        spacing: 3
        Text { textFormat: Text.PlainText; width: parent.width; text: row.title; color: Theme.text; font.pixelSize: Theme.bodySize - 1; wrapMode: Text.WordWrap }
        Text { textFormat: Text.PlainText; width: parent.width; text: row.detail; color: Theme.muted; font.pixelSize: Theme.smallSize; wrapMode: Text.WordWrap; lineHeight: 1.1; visible: text !== "" }
    }
    Item {
        id: controlSlot
        x: row.stacked ? 0 : row.width - width
        y: row.stacked ? texts.y + texts.implicitHeight + 10 : (row.height - height) / 2
        width: childrenRect.width
        height: childrenRect.height
    }
}
