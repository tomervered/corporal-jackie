import QtQuick
import QtQuick.Controls.Basic

// The ways to play the chosen item, best first, grouped by quality.
Column {
    required property Item nav   // the Pages: go(), back(), openItem() and the picked types
    function takeFocus() { streamList.forceActiveFocus() }

    spacing: 8
    PageTitle { width: parent.width; text: browser.streamsTitle }
    Muted {
        width: parent.width
        text: browser.streamsLoading ? "Asking your add-ons… (" + browser.streams.length + " found so far)"
            : browser.streams.length === 0 ? "No way to play this was found. Add-ons that find streams (for example torrent add-ons) have to be installed first."
            : browser.streams.length === 1 ? "1 way to play." : browser.streams.length + " ways to play. The best is first."
    }
    Muted {
        width: parent.width
        visible: browser.streamProblems.length > 0
        color: Theme.error
        text: "Didn't answer: " + browser.streamProblems.join("; ")
    }
    ListView {
        id: streamList
        width: parent.width
        height: parent.height - y
        clip: true
        activeFocusOnTab: true
        keyNavigationEnabled: true
        model: browser.streams
        ScrollBar.vertical: ScrollBar {}
        section.property: "group"
        section.delegate: SectionLabel {}
        delegate: RowItem {
            required property var modelData
            required property int index
            primary: modelData.provider + " · " + modelData.name
            secondary: (modelData.description ? modelData.description.replace(/\n/g, "  ") + "\n" : "") + modelData.facts
            onClicked: browser.play(index)
        }
        Keys.onReturnPressed: browser.play(currentIndex)
        Keys.onEnterPressed: browser.play(currentIndex)
    }
}
