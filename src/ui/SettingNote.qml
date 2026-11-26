import QtQuick

Text {
    textFormat: Text.PlainText
    width: parent ? parent.width : implicitWidth
    color: Theme.muted
    font.pixelSize: Theme.smallSize
    wrapMode: Text.WordWrap
    lineHeight: 1.1
    topPadding: 4
    bottomPadding: 4
}
