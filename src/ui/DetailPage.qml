import QtQuick
import QtQuick.Controls.Basic

// An item's details and, for a series, its episodes. For a movie, the ways
// to play it are listed here at once: opening a movie is asking for them.
Item {
    id: detailPage
    required property Item nav   // the Pages: go(), back(), openItem() and the picked types
    readonly property bool isMovie: !browser.detail.isSeries && browser.detail.type !== "series"
    function takeFocus() {
        if (browser.detail.isSeries) episodes.forceActiveFocus()
        else if (isMovie) movieStreams.takeFocus()
        else libraryButton.forceActiveFocus()
    }

    // Asks for a movie's ways to play once its details are in. The key
    // keeps a return to the page (after playing) from asking again.
    property string streamsAskedFor: ""
    function askForStreams() {
        if (browser.detailLoading || !browser.detail.key) {
            streamsAskedFor = ""
            return
        }
        if (isMovie && streamsAskedFor !== browser.detail.key) {
            streamsAskedFor = browser.detail.key
            browser.openVideo(-1)
        }
    }
    Connections {
        target: browser
        function onDetailChanged() { detailPage.askForStreams() }
    }


    // The item's backdrop, faded into the page
    Image {
        id: backdrop
        anchors { left: parent.left; right: parent.right; top: parent.top }
        height: Math.min(parent.height * 0.5, width * 9 / 16)
        source: browser.detail.background || ""
        fillMode: Image.PreserveAspectCrop
        asynchronous: true
        opacity: 0.35
        sourceSize.width: 1280
    }
    Rectangle {
        anchors.fill: backdrop
        visible: backdrop.status === Image.Ready
        gradient: Gradient {
            GradientStop { position: 0.0; color: "transparent" }
            GradientStop { position: 1.0; color: Theme.background }
        }
    }

    Row {
        id: detailHeader
        width: parent.width
        spacing: 18
        Image {
            id: detailPoster
            width: 150; height: 225
            source: browser.detail.poster || ""
            visible: status === Image.Ready
            fillMode: Image.PreserveAspectCrop
            sourceSize.width: 300
            asynchronous: true
        }
        Column {
            width: parent.width - (detailPoster.visible ? detailPoster.width + 18 : 0)
            spacing: 8
            PageTitle { width: parent.width; text: browser.detail.name || "" }
            Muted {
                width: parent.width
                visible: text !== ""
                text: [browser.detail.releaseInfo, browser.detail.runtime, browser.detail.genres].filter(s => s).join("  ·  ")
            }
            Muted {
                width: parent.width
                visible: text !== ""
                text: browser.detail.description || ""
                color: Theme.text
                maximumLineCount: 6
                elide: Text.ElideRight
            }
            Row {
                spacing: 8
                Button {
                    id: libraryButton
                    // Reads library.items so it updates when the library changes.
                    readonly property bool saved: library.items.length >= 0 && library.contains(browser.detail.key || "")
                    visible: (browser.detail.id || "") !== ""
                    text: saved ? "✓ In your library" : "+ Add to library"
                    onClicked: saved ? library.remove(browser.detail.key) : library.add(browser.detail)
                }
            }
            Muted {
                width: parent.width
                visible: text !== ""
                text: browser.detailLoading ? "Loading details…" : browser.detailError !== "" ? "Details could not be loaded: " + browser.detailError : ""
            }
        }
    }
    StreamList {
        id: movieStreams
        objectName: "movieStreams"
        nav: detailPage.nav
        anchors { left: parent.left; right: parent.right; top: detailHeader.bottom; bottom: parent.bottom; topMargin: 16 }
        visible: detailPage.isMovie && !browser.detailLoading
    }
    ListView {
        id: episodes
        anchors { left: parent.left; right: parent.right; top: detailHeader.bottom; bottom: parent.bottom; topMargin: 16 }
        visible: browser.detail.isSeries === true
        clip: true
        activeFocusOnTab: true
        keyNavigationEnabled: true
        model: browser.videos
        ScrollBar.vertical: ScrollBar {}
        section.property: "season"
        section.delegate: SectionLabel {}
        delegate: RowItem {
            required property var modelData
            required property int index
            primary: modelData.label
            secondary: modelData.released
            progress: library.historyCount >= 0 ? library.progress(browser.detail.adapter + ":" + browser.detail.id + ":" + modelData.id) : -1
            onClicked: { episodes.currentIndex = index; browser.openVideo(index); nav.go("streams") }
        }
        Keys.onReturnPressed: { browser.openVideo(currentIndex); nav.go("streams") }
        Keys.onEnterPressed: { browser.openVideo(currentIndex); nav.go("streams") }
    }
}
