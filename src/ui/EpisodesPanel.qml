import QtQuick
import QtQuick.Controls.Basic

// The episodes of the series that is playing, over the video. Picking one
// lists its streams here; picking a stream plays it. Binds to `browser`
// (playingEpisodes, streams) and `library` (how far each was watched).
Rectangle {
    id: panel

    signal closeRequested()
    signal streamPicked()

    // -1: the episode list; otherwise the episode whose streams are shown.
    property int pickedEpisode: -1
    // Read once here, not once per row: each read builds the whole list.
    readonly property bool hasEpisodes: browser.playingEpisodes.length > 0

    color: Theme.panel
    border.color: "#30ffffff"
    radius: 12

    // Clicks and scrolls on the panel stay here, away from the video below.
    MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons; onWheel: wheel => wheel.accepted = true }
    Keys.onEscapePressed: {
        if (panel.pickedEpisode >= 0) panel.pickedEpisode = -1
        else panel.closeRequested()
    }
    onVisibleChanged: {
        if (!visible) return
        panel.pickedEpisode = -1
        episodeList.currentIndex = browser.playingEpisodeIndex
        episodeList.positionViewAtIndex(Math.max(0, browser.playingEpisodeIndex), ListView.Center)
        episodeList.forceActiveFocus()
    }

    function pick(index) {
        if (index < 0) return
        panel.pickedEpisode = index
        browser.openPlayingEpisode(index)
        streamList.currentIndex = 0
        streamList.forceActiveFocus()
    }

    function play(index) {
        if (index < 0) return
        browser.play(index)
        panel.streamPicked()
    }

    // Title row: the show, or the episode with a way back.
    Item {
        id: header
        anchors { left: parent.left; right: parent.right; top: parent.top; margins: 16 }
        height: 36
        BarButton {
            id: backButton
            visible: panel.pickedEpisode >= 0
            anchors { left: parent.left; verticalCenter: parent.verticalCenter }
            text: "‹ Episodes"
            onClicked: { panel.pickedEpisode = -1; episodeList.forceActiveFocus() }
        }
        Text {
            textFormat: Text.PlainText
            anchors { left: backButton.visible ? backButton.right : parent.left; right: closeButton.left
                      leftMargin: backButton.visible ? 8 : 0; verticalCenter: parent.verticalCenter }
            text: panel.pickedEpisode >= 0 ? browser.streamsTitle : browser.playingShowName
            color: Theme.text
            font.pixelSize: 17
            font.weight: Font.DemiBold
            elide: Text.ElideRight
        }
        BarButton {
            id: closeButton
            anchors { right: parent.right; verticalCenter: parent.verticalCenter }
            text: "✕"
            onClicked: panel.closeRequested()
        }
    }

    ListView {
        id: episodeList
        objectName: "episodeList"
        visible: panel.pickedEpisode < 0
        anchors { left: parent.left; right: parent.right; top: header.bottom; bottom: parent.bottom; margins: 12 }
        clip: true
        keyNavigationEnabled: true
        model: browser.playingEpisodes
        ScrollBar.vertical: ScrollBar {}
        section.property: "season"
        section.delegate: SectionLabel {}
        delegate: RowItem {
            required property var modelData
            required property int index
            readonly property bool playing: index === browser.playingEpisodeIndex
            primary: (playing ? "▶  " : "") + modelData.label
            secondary: playing ? "Playing now" : ""
            progress: library.historyCount >= 0 && panel.hasEpisodes
                      ? library.progress(browser.playingShowKey + ":" + modelData.id) : -1
            onClicked: panel.pick(index)
        }
        Keys.onReturnPressed: panel.pick(currentIndex)
        Keys.onEnterPressed: panel.pick(currentIndex)
    }

    Column {
        id: streamsView
        visible: panel.pickedEpisode >= 0
        anchors { left: parent.left; right: parent.right; top: header.bottom; bottom: parent.bottom; margins: 12 }
        spacing: 6
        Muted {
            width: parent.width
            text: browser.streamsLoading ? "Asking your add-ons… (" + browser.streams.length + " found so far)"
                : browser.streams.length === 0 ? "No way to play this episode was found."
                : browser.streams.length === 1 ? "1 way to play." : browser.streams.length + " ways to play. The best is first."
        }
        ListView {
            id: streamList
            objectName: "episodeStreamList"
            width: parent.width
            height: parent.height - y
            clip: true
            keyNavigationEnabled: true
            model: browser.streams
            ScrollBar.vertical: ScrollBar {}
            section.property: "group"
            section.delegate: SectionLabel {}
            delegate: RowItem {
                required property var modelData
                required property int index
                primary: modelData.provider + " · " + modelData.name
                secondary: modelData.facts
                onClicked: panel.play(index)
            }
            Keys.onReturnPressed: panel.play(currentIndex)
            Keys.onEnterPressed: panel.play(currentIndex)
        }
    }
}
