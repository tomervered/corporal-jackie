import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs

// The user's own folders of videos.
Column {
    required property Item nav   // the Pages: go(), back(), openItem() and the picked types
    function takeFocus() { folderList.forceActiveFocus() }

    spacing: 10
    PageTitle { text: "Your folders" }
    Muted {
        width: parent.width
        text: "Videos in these folders show up as lists on Home and in Discover. Subtitle files next to a video, or in a Subs folder beside it, load with it."
    }
    Row {
        spacing: 8
        Button { text: "Add a folder…"; onClicked: folderDialog.open() }
        Button { text: "Scan again"; enabled: browser.folders.length > 0 && !browser.scanning; onClicked: browser.rescanFolders() }
        Muted {
            anchors.verticalCenter: parent.verticalCenter
            text: browser.scanning ? "Scanning…" : browser.localVideoCount + " videos found"
        }
    }
    ListView {
        id: folderList
        width: parent.width
        height: parent.height - y
        clip: true
        activeFocusOnTab: true
        keyNavigationEnabled: true
        model: browser.folders
        delegate: RowItem {
            required property var modelData
            primary: modelData
            secondary: "Click to remove"
            onClicked: browser.removeFolder(modelData)
        }
        Keys.onDeletePressed: if (currentIndex >= 0) browser.removeFolder(browser.folders[currentIndex])
    }

    FolderDialog {
        id: folderDialog
        title: "Add a folder with videos"
        onAccepted: browser.addFolder(selectedFolder)
    }
}
