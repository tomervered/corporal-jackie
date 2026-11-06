import QtQuick
import QtQuick.Window

Window {
    id: root
    width: 1280
    height: 720
    minimumWidth: 640
    minimumHeight: 360
    visible: true
    title: "Corporal Jackie"
    color: "#101014"

    Column {
        anchors.centerIn: parent
        spacing: 8

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "Corporal Jackie"
            color: "#f2f2f5"
            font.pixelSize: 40
            font.weight: Font.DemiBold
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "Groundwork build " + appVersion
            color: "#9a9aa6"
            font.pixelSize: 16
        }
    }
}
