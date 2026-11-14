import QtQuick

// The ways to play an episode. A movie lists them on its details page.
Column {
    id: streamsPage
    required property Item nav   // the Pages: go(), back(), openItem() and the picked types
    function takeFocus() { streamList.takeFocus() }

    spacing: 8
    PageTitle { width: parent.width; text: browser.streamsTitle }
    StreamList {
        id: streamList
        nav: streamsPage.nav
        width: parent.width
        height: parent.height - y
    }
}
