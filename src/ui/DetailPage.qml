import QtQuick
import QtQuick.Controls.Basic

// An item's details and, for a series, its episodes. For a movie, the ways
// to play it are listed here once the person presses Find streams: opening
// a movie asks the add-ons nothing.
Item {
    id: detailPage
    required property Item nav   // the Pages: go(), back(), openItem() and the picked types
    readonly property bool isMovie: !browser.detail.isSeries && browser.detail.type !== "series"
    function takeFocus() {
        if (browser.detail.isSeries) episodes.forceActiveFocus()
        else if (isMovie && streamsAsked) movieStreams.takeFocus()
        else if (isMovie) findStreamsButton.forceActiveFocus()
        else libraryButton.forceActiveFocus()
    }

    // The movie the person asked streams for. The key keeps the list on a
    // return to the page (after playing), and hides another movie's list.
    property string streamsAskedFor: ""
    readonly property bool streamsAsked: !browser.detailLoading && !!browser.detail.key
                                         && streamsAskedFor === browser.detail.key
    function askForStreams() {
        if (browser.detailLoading || !browser.detail.key || !isMovie)
            return
        streamsAskedFor = browser.detail.key
        browser.openVideo(-1)
        movieStreams.takeFocus()
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
        // The poster, or its place while it loads; left out when there is none.
        Rectangle {
            id: detailPoster
            readonly property bool small: detailPage.height < 520
            width: small ? 100 : 150; height: width * 1.5
            visible: (browser.detail.poster || "") !== "" && poster.status !== Image.Error
            radius: Theme.radius
            color: Theme.card
            clip: true
            Image {
                id: poster
                anchors.fill: parent
                source: browser.detail.poster || ""
                fillMode: Image.PreserveAspectCrop
                sourceSize.width: 300
                asynchronous: true
                opacity: status === Image.Ready ? 1 : 0
                Behavior on opacity { NumberAnimation { duration: 150 } }
            }
        }
        Column {
            width: parent.width - (detailPoster.visible ? detailPoster.width + 18 : 0)
            spacing: 10
            PageTitle {
                width: parent.width
                text: browser.detail.name || (browser.detailLoading ? "Loading…" : "")
                wrapMode: Text.WordWrap
                maximumLineCount: 2
            }
            Muted {
                width: parent.width
                visible: text !== ""
                text: [browser.detail.releaseInfo, browser.detail.runtime, browser.detail.genres].filter(s => s).join("  ·  ")
            }
            Muted {
                width: parent.width
                visible: text !== ""
                text: browser.detail.description || ""
                color: "#d0d0d8"
                font.pixelSize: 14
                lineHeight: 1.15
                // A short window leaves room for the list below.
                maximumLineCount: detailPage.height < 520 ? 2 : detailPage.height < 700 ? 4 : 6
                elide: Text.ElideRight
            }
            Row {
                spacing: 8
                AppButton {
                    id: findStreamsButton
                    objectName: "findStreams"
                    visible: detailPage.isMovie && !browser.detailLoading && !detailPage.streamsAsked
                    primary: true
                    text: "Find streams"
                    onClicked: detailPage.askForStreams()
                    Keys.onReturnPressed: detailPage.askForStreams()
                    Keys.onEnterPressed: detailPage.askForStreams()
                }
                AppButton {
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
                visible: findStreamsButton.visible
                text: "Find streams asks the add-ons you installed. Play only what you have the right to watch."
            }
            Muted {
                width: parent.width
                visible: text !== ""
                color: browser.detailError !== "" ? Theme.error : Theme.muted
                text: browser.detailLoading && (browser.detail.name || "") !== "" ? "Loading details…"
                    : browser.detailError !== "" ? "Details could not be loaded: " + browser.detailError + ". Go back and open it again to retry." : ""
            }
        }
    }
    StreamList {
        id: movieStreams
        objectName: "movieStreams"
        nav: detailPage.nav
        anchors { left: parent.left; right: parent.right; top: detailHeader.bottom; bottom: parent.bottom; topMargin: 16 }
        visible: detailPage.isMovie && detailPage.streamsAsked
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
