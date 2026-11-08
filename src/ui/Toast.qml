import QtQuick

// A short message on a dark rounded box over the video.
Rectangle {
    property alias text: label.text
    property alias fontSize: label.font.pixelSize
    property alias bold: label.font.bold
    width: label.implicitWidth + 32
    height: label.implicitHeight + 18
    radius: 10
    color: "#a0000000"
    Text {
        id: label
        anchors.centerIn: parent
        color: Theme.text
        font.pixelSize: 18
    }
}
