import QtQuick
import QtQuick.Window
import QtQuick.Controls.Basic
import CorporalJackie.Player   // KeyedList

// Home: the film to go on with, large; the rest of Continue Watching; and a
// row from each list of every add-on and folder, each with See all. With
// nothing to go on with, the ways to open something instead.
Flickable {
    id: homePage
    required property Item nav   // the Pages: go(), back(), openItem() and the picked types
    // The keyboard starts on Resume (or the first row), so Enter and the
    // arrows work at once, and B (and the other letters) still reach the pages.
    function takeFocus() {
        if (hero.visible) resumeButton.forceActiveFocus()
        else if (homeContinue.count > 0) homeContinue.forceActiveFocus()
        else if (homeRows.count > 0) homeRows.itemAt(0).takeFocus()
        else openVideoButton.forceActiveFocus()
    }

    readonly property var latest: library.continueWatching.length > 0 ? library.continueWatching[0] : null
    function remaining(item) {
        const left = Math.max(0, item.duration - item.position)
        return left >= 3600 ? Math.floor(left / 3600) + " h " + Math.round((left % 3600) / 60) + " min left"
                            : Math.max(1, Math.round(left / 60)) + " min left"
    }
    function clock(seconds) {
        const s = Math.floor(seconds), h = Math.floor(s / 3600), m = Math.floor((s % 3600) / 60), r = s % 60
        return (h > 0 ? h + ":" + String(m).padStart(2, "0") : m) + ":" + String(r).padStart(2, "0")
    }
    readonly property real columnsWidth: width - 14

    contentHeight: homeColumn.implicitHeight + 24
    clip: true
    boundsBehavior: Flickable.StopAtBounds
    ScrollBar.vertical: ScrollBar {}
    Column {
        id: homeColumn
        width: homePage.columnsWidth
        spacing: 26

        // --- Go on watching ----------------------------------------------------
        Rectangle {
            id: hero
            objectName: "homeHero"
            visible: homePage.latest !== null
            width: parent.width
            height: 250
            radius: 12
            color: Theme.panel
            clip: true
            // Layers, so it always looks full and nothing is cut off: the
            // episode's still or the title's wide art on the right (the first
            // that loads), or else the poster blurred over the whole card;
            // then a fade into the panel behind the words; then the whole
            // poster as a card on the left.
            readonly property var arts: homePage.latest ? [homePage.latest.still, homePage.latest.backdrop].filter(u => !!u) : []
            property int artIndex: 0
            onArtsChanged: artIndex = 0
            Image {
                id: heroBlur
                objectName: "heroBlur"
                anchors.fill: parent
                visible: heroArt.status !== Image.Ready
                source: homePage.latest ? homePage.latest.poster : ""
                // Decoded tiny and drawn large: the smooth scaling blurs it.
                sourceSize.width: 24
                fillMode: Image.PreserveAspectCrop
                smooth: true
                asynchronous: true
                opacity: 0.8
            }
            Image {
                id: heroArt
                objectName: "heroArt"
                anchors { right: parent.right; top: parent.top; bottom: parent.bottom }
                width: parent.width * 0.68
                source: hero.artIndex < hero.arts.length ? hero.arts[hero.artIndex] : ""
                fillMode: Image.PreserveAspectCrop
                sourceSize.width: 960
                asynchronous: true
                onStatusChanged: if (status === Image.Error) hero.artIndex++
            }
            Rectangle {
                anchors.fill: parent
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0.0; color: Qt.rgba(Theme.panel.r, Theme.panel.g, Theme.panel.b, 0.96) }
                    GradientStop { position: 0.42; color: Qt.rgba(Theme.panel.r, Theme.panel.g, Theme.panel.b, 0.88) }
                    GradientStop { position: 0.75; color: Qt.rgba(Theme.panel.r, Theme.panel.g, Theme.panel.b, 0.25) }
                    GradientStop { position: 1.0; color: Qt.rgba(Theme.panel.r, Theme.panel.g, Theme.panel.b, 0.05) }
                }
            }
            // The whole poster, never cropped, with how far in under it.
            Item {
                id: heroPoster
                x: 24
                y: 22
                height: parent.height - 44
                width: Math.round(height / 1.5)
                visible: heroPosterImage.status === Image.Ready
                Rectangle { anchors { fill: parent; margins: -1 } radius: 7; color: "transparent"; border.color: Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.18) }
                Image {
                    id: heroPosterImage
                    objectName: "heroPoster"
                    anchors.fill: parent
                    source: homePage.latest ? homePage.latest.poster : ""
                    fillMode: Image.PreserveAspectFit
                    sourceSize.width: Math.ceil(width * Screen.devicePixelRatio)
                    asynchronous: true
                }
                Rectangle {
                    anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                    height: 4
                    color: "#a0000000"
                    Rectangle { width: parent.width * (homePage.latest ? Math.min(1, homePage.latest.progress) : 0); height: parent.height; color: Theme.accent }
                }
            }
            Column {
                anchors { left: heroPoster.visible ? heroPoster.right : parent.left; leftMargin: heroPoster.visible ? 28 : 30; verticalCenter: parent.verticalCenter }
                width: parent.width * 0.55
                spacing: 4
                Text { textFormat: Text.PlainText; text: "Continue watching"; color: Theme.muted; font.pixelSize: Theme.smallSize }
                Text {
                    textFormat: Text.PlainText
                    width: parent.width
                    text: homePage.latest ? homePage.latest.name : ""
                    color: Theme.text
                    font.pixelSize: 34
                    font.weight: Font.Bold
                    font.letterSpacing: -0.8
                    elide: Text.ElideRight
                }
                Text {
                    textFormat: Text.PlainText
                    width: parent.width
                    text: !homePage.latest ? ""
                        : (homePage.latest.subtitle !== "" ? homePage.latest.subtitle + " · " : "Paused at " + homePage.clock(homePage.latest.position) + " · ")
                          + homePage.remaining(homePage.latest)
                    color: Theme.muted
                    font.pixelSize: Theme.smallSize + 1
                    elide: Text.ElideRight
                    bottomPadding: 14
                }
                Row {
                    spacing: 8
                    AppButton {
                        id: resumeButton
                        objectName: "resumeButton"
                        primary: true
                        iconName: "play"
                        text: "Resume"
                        onClicked: { library.resume(homePage.latest.key); nav.closeRequested() }
                    }
                    AppButton {
                        visible: homePage.latest !== null && homePage.latest.canOpen === true
                        text: "Details"
                        onClicked: { const k = homePage.latest.itemKey; library.open(k) }
                    }
                }
            }
            // Rounded corners over the pictures.
            CornerMask { anchors.fill: parent; radius: hero.radius }
        }

        // --- Nothing to go on with: how to open something ---------------------
        Rectangle {
            visible: !hero.visible
            width: parent.width
            height: openColumn.implicitHeight + 48
            radius: 10
            color: Theme.panel
            Column {
                id: openColumn
                anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter; margins: 30 }
                spacing: 6
                Text { textFormat: Text.PlainText; text: "Watch something"; color: Theme.text; font.pixelSize: 26; font.weight: Font.Bold }
                Muted {
                    width: parent.width
                    text: "Open a file or paste a magnet link, search above, or pick from the lists below. You can also drop a video, a subtitle file or a magnet link anywhere on the window."
                    bottomPadding: 12
                }
                Row {
                    spacing: 8
                    AppButton { id: openVideoButton; primary: true; iconName: "file"; text: "Open a video…"; onClicked: nav.openFileRequested() }
                    AppButton { iconName: "link"; text: "Open a link…"; onClicked: nav.openLinkRequested() }
                }
            }
        }

        Column {
            width: parent.width
            visible: homeContinue.count > 0
            spacing: 12
            Heading { text: "Also in progress" }
            ContinueWatching {
                id: homeContinue
                skip: 1
                width: parent.width
                tileWidth: homeRows.count > 0 ? homeRows.itemAt(0).tileWidth - 2 : 148
                height: implicitHeight
                activeFocusOnTab: true
                onResumed: nav.closeRequested()
            }
        }

        Muted {
            width: parent.width
            visible: browser.catalogs.length === 0
            text: "Nothing to browse yet. Install an add-on under Add-ons, or add a folder with videos under Local."
        }
        // Updated in place: a row that finishes loading leaves the others be.
        KeyedList { id: homeRowsModel; keyRole: "catalog"; values: browser.homeRows }
        Repeater {
            id: homeRows
            model: homeRowsModel
            PosterRow {
                id: homeRow
                required property var entry
                required property int index
                title: entry.name
                items: entry.items
                loading: entry.loading
                error: entry.error
                canSeeAll: true
                // Within half a screen of the view. (Every row made its tiles
                // and kept their posters, about 2.5 MB a row.)
                near: y + height >= homePage.contentY - homePage.height / 2
                      && y <= homePage.contentY + homePage.height * 1.5
                onOpened: i => nav.openItem(() => browser.openRowItem(false, index, i))
                onSeeAll: { browser.openCatalog(entry.catalog); nav.discoverType = ""; nav.go("discover") }
                onFocusedIn: nav.reveal(homePage, homeRow)
            }
        }
    }
}
