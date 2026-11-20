import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs

// Local: open one video or a link, and the user's own folders of videos.
Column {
    required property Item nav   // the Pages: go(), back(), openItem() and the picked types
    function takeFocus() { openFileButton.forceActiveFocus() }

    spacing: 14
    Row {
        spacing: 8
        AppButton { id: openFileButton; iconName: "file"; text: "Open a video…"; onClicked: nav.openFileRequested() }
        AppButton { iconName: "link"; text: "Open a link or magnet…"; onClicked: nav.openLinkRequested() }
    }
    Muted {
        width: parent.width
        text: "You can also drop a video, a subtitle file or a magnet link anywhere on the window."
    }
    Rectangle { width: parent.width; height: 1; color: Theme.divider }
    Item {
        width: parent.width
        height: addFolderButton.height
        Heading { anchors { left: parent.left; verticalCenter: parent.verticalCenter } text: "Your folders" }
        Row {
            anchors { right: parent.right; verticalCenter: parent.verticalCenter }
            spacing: 8
            Muted {
                anchors.verticalCenter: parent.verticalCenter
                visible: browser.folders.length > 0
                text: browser.scanning ? "Scanning…" : browser.localVideoCount === 1 ? "1 video found" : browser.localVideoCount + " videos found"
            }
            AppButton {
                iconName: "refresh"
                text: "Scan again"
                visible: browser.folders.length > 0
                enabled: !browser.scanning
                onClicked: browser.rescanFolders()
            }
            AppButton { id: addFolderButton; iconName: "plus"; text: "Add a folder…"; onClicked: folderDialog.open() }
        }
    }
    Muted {
        width: parent.width
        text: browser.folders.length === 0 ? "No folders yet. Videos in a folder you add show up as lists on Home and in Discover."
            : "Videos in these folders show up as lists on Home and in Discover. Subtitle files next to a video, or in a Subs folder beside it, load with it."
    }
    ListView {
        id: folderList
        width: parent.width
        height: parent.height - y
        clip: true
        spacing: 6
        activeFocusOnTab: true
        keyNavigationEnabled: true
        model: browser.folders
        delegate: Rectangle {
            id: folderRow
            required property var modelData
            required property int index
            width: ListView.view.width
            height: 52
            radius: 8
            color: Theme.panel
            border.width: ListView.isCurrentItem && folderList.activeFocus ? 2 : 1
            border.color: ListView.isCurrentItem && folderList.activeFocus ? Theme.accent : Theme.divider
            Icon { id: folderIcon; anchors { left: parent.left; leftMargin: 14; verticalCenter: parent.verticalCenter } name: "local"; color: Theme.muted }
            Text {
                textFormat: Text.PlainText
                anchors { left: folderIcon.right; leftMargin: 12; right: removeFolder.left; rightMargin: 12; verticalCenter: parent.verticalCenter }
                text: folderRow.modelData
                color: Theme.text
                font.pixelSize: Theme.bodySize - 1
                elide: Text.ElideMiddle
            }
            IconButton {
                id: removeFolder
                anchors { right: parent.right; rightMargin: 6; verticalCenter: parent.verticalCenter }
                iconName: "trash"
                iconSize: 17
                tip: "Remove this folder"
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
