import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs

// The user's own folders of videos.
Column {
    required property Item nav   // the Pages: go(), back(), openItem() and the picked types
    function takeFocus() {
        if (browser.folders.length > 0) folderList.forceActiveFocus()
        else addButton.forceActiveFocus()
    }

    spacing: 10
    PageTitle { text: "Your folders" }
    Muted {
        width: parent.width
        text: "Videos in these folders show up as lists on Home and in Discover. Subtitle files next to a video, or in a Subs folder beside it, load with it."
    }
    Row {
        spacing: 8
        AppButton { id: addButton; text: "Add a folder…"; primary: true; onClicked: folderDialog.open() }
        AppButton { text: "Scan again"; visible: browser.folders.length > 0; enabled: !browser.scanning; onClicked: browser.rescanFolders() }
        Muted {
            anchors.verticalCenter: parent.verticalCenter
            visible: browser.folders.length > 0
            leftPadding: 4
            text: browser.scanning ? "Scanning…" : browser.localVideoCount === 1 ? "1 video found" : browser.localVideoCount + " videos found"
        }
    }
    Muted {
        width: parent.width
        visible: browser.folders.length === 0
        topPadding: 8
        text: "No folders yet."
    }
    // A folder per row, with Remove on the right (Delete removes the one
    // the keyboard is on). Removing a folder leaves its files alone.
    ListView {
        id: folderList
        objectName: "folderList"
        width: parent.width
        height: parent.height - y
        clip: true
        activeFocusOnTab: true
        keyNavigationEnabled: true
        spacing: 6
        topMargin: 4
        model: browser.folders
        ScrollBar.vertical: ScrollBar {}
        delegate: Rectangle {
            id: folderRow
            required property string modelData
            required property int index
            width: ListView.view.width
            height: 52
            radius: Theme.radius
            color: Theme.card
            border.color: ListView.isCurrentItem && folderList.activeFocus ? Theme.accent : "transparent"
            Text {
                textFormat: Text.PlainText
                anchors { left: parent.left; right: removeButton.left; verticalCenter: parent.verticalCenter; leftMargin: 14; rightMargin: 12 }
                text: folderRow.modelData
                color: Theme.text
                font.pixelSize: 14
                elide: Text.ElideMiddle
            }
            AppButton {
                id: removeButton
                objectName: "removeFolder"
                anchors { right: parent.right; verticalCenter: parent.verticalCenter; rightMargin: 8 }
                text: "Remove"
                focusPolicy: Qt.NoFocus
                ToolTip.visible: hovered
                ToolTip.text: "Stop listing this folder. Its files stay where they are."
                onClicked: browser.removeFolder(folderRow.modelData)
            }
        }
        Keys.onDeletePressed: if (currentIndex >= 0) browser.removeFolder(browser.folders[currentIndex])
    }

    FolderDialog {
        id: folderDialog
        title: "Add a folder with videos"
        onAccepted: browser.addFolder(selectedFolder)
    }
}
