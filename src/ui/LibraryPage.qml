import QtQuick
import QtQuick.Controls.Basic
import CorporalJackie.Player   // KeyedList

// Library: what to go on watching and what the user saved.
Item {
    required property Item nav   // the Pages: go(), back(), openItem() and the picked types
    function takeFocus() {
        if (library.items.length > 0) libraryGrid.forceActiveFocus()
        else if (libraryContinue.count > 0) libraryContinue.forceActiveFocus()
        else libraryGrid.forceActiveFocus()
    }

    Column {
        id: libraryTop
        width: parent.width
        spacing: 12
        PageTitle { text: "Library" }
        Heading { visible: libraryContinue.count > 0; text: "Continue watching" }
        ContinueWatching {
            id: libraryContinue
            visible: count > 0
            width: parent.width
            height: implicitHeight
            activeFocusOnTab: true
            onResumed: nav.closeRequested()
        }
        Heading { text: "Saved"; topPadding: libraryContinue.visible ? 8 : 0 }
        Row {
            visible: library.items.length === 0
            spacing: 12
            Muted {
                anchors.verticalCenter: parent.verticalCenter
                text: "Nothing saved yet. Open a film or show and press Add to library."
            }
            AppButton {
                anchors.verticalCenter: parent.verticalCenter
                text: "Discover something"
                onClicked: {
                    if (browser.catalogIndex < 0 && browser.catalogs.length > 0) browser.openCatalog(0)
                    nav.goTop("discover")
                }
            }
        }
    }
    // Updated in place, like the other poster lists.
    KeyedList { id: libraryModel; keyRole: "key"; values: library.items }
    PosterGrid {
        id: libraryGrid
        anchors { left: parent.left; right: parent.right; top: libraryTop.bottom; bottom: parent.bottom; topMargin: 10 }
        model: libraryModel
        onOpened: index => { const item = library.items[index]; if (item) library.open(item.key) }
    }
}
