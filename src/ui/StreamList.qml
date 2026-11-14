import QtQuick
import QtQuick.Controls.Basic

// The ways to play the chosen movie or episode: how to sort them, which
// add-ons to show them from, and the list. The sort and the hidden add-ons
// are kept on the Pages, so they stay the same from one video to the next.
Column {
    id: streamPane
    required property Item nav   // the Pages: streamSort, hiddenProviders
    function takeFocus() { streamList.forceActiveFocus() }

    spacing: 8

    readonly property var sorts: [
        { key: "best", label: "Best" },
        { key: "seeders", label: "Most seeders" },
        { key: "largest", label: "Largest" },
        { key: "smallest", label: "Smallest" }
    ]
    // The add-ons that found something, in the order they rank, with how many.
    readonly property var providers: {
        const counts = {}
        const order = []
        for (const s of browser.streams) {
            if (counts[s.provider] === undefined) { counts[s.provider] = 0; order.push(s.provider) }
            counts[s.provider]++
        }
        return order.map(name => ({ name: name, count: counts[name] }))
    }
    // The streams to list: the hidden add-ons left out, in the chosen order.
    // browser.streams comes best first; ties keep that order.
    readonly property var shown: {
        const hidden = nav.hiddenProviders
        const list = browser.streams.filter(s => hidden.indexOf(s.provider) < 0)
        const known = v => v > 0
        let compare = null
        switch (nav.streamSort) {
        case "seeders": compare = (a, b) => b.seeders - a.seeders; break
        case "largest": compare = (a, b) => b.sizeBytes - a.sizeBytes; break
        case "smallest": compare = (a, b) => known(a.sizeBytes) !== known(b.sizeBytes) ? (known(a.sizeBytes) ? -1 : 1)
                                           : a.sizeBytes - b.sizeBytes; break
        }
        if (compare) list.sort((a, b) => compare(a, b) || a.index - b.index)
        return list
    }
    readonly property int hiddenCount: browser.streams.length - shown.length

    function toggleProvider(name) {
        const hidden = nav.hiddenProviders
        nav.hiddenProviders = hidden.indexOf(name) < 0 ? hidden.concat([name]) : hidden.filter(p => p !== name)
    }

    Muted {
        width: parent.width
        text: (browser.streamsLoading ? "Asking your add-ons… (" + browser.streams.length + " found so far)"
               : browser.streams.length === 0 ? "No way to play this was found. Add-ons that find streams (for example torrent add-ons) have to be installed first."
               : browser.streams.length === 1 ? "1 way to play."
               : browser.streams.length + " ways to play.")
            + (streamPane.hiddenCount > 0 ? "  " + streamPane.hiddenCount + " hidden by the add-on filter." : "")
    }
    Muted {
        width: parent.width
        visible: browser.streamProblems.length > 0
        color: Theme.error
        text: "Didn't answer: " + browser.streamProblems.join("; ")
    }
    Flow {
        width: parent.width
        spacing: 6
        visible: browser.streams.length > 1
        Muted { text: "Sort"; height: 40; verticalAlignment: Text.AlignVCenter; rightPadding: 4 }
        Repeater {
            model: streamPane.sorts
            Chip {
                required property var modelData
                text: modelData.label
                selected: nav.streamSort === modelData.key
                onClicked: nav.streamSort = modelData.key
            }
        }
    }
    Flow {
        width: parent.width
        spacing: 6
        // Shown when there is a choice, or when a filter hides something.
        visible: streamPane.providers.length > 1 || streamPane.hiddenCount > 0
        Muted { text: "Add-ons"; height: 40; verticalAlignment: Text.AlignVCenter; rightPadding: 4 }
        Repeater {
            model: streamPane.providers
            Chip {
                required property var modelData
                text: modelData.name + " (" + modelData.count + ")"
                selected: nav.hiddenProviders.indexOf(modelData.name) < 0
                onClicked: streamPane.toggleProvider(modelData.name)
            }
        }
        Chip {
            visible: nav.hiddenProviders.length > 0
            text: "Show all"
            onClicked: nav.hiddenProviders = []
        }
    }
    ListView {
        id: streamList
        width: parent.width
        height: Math.max(0, parent.height - y)
        clip: true
        activeFocusOnTab: true
        keyNavigationEnabled: true
        model: streamPane.shown
        ScrollBar.vertical: ScrollBar {}
        // Quality groups only make sense in the best-first order.
        section.property: nav.streamSort === "best" ? "group" : ""
        section.delegate: SectionLabel {}
        delegate: RowItem {
            required property var modelData
            primary: modelData.provider + " · " + modelData.name
            secondary: (modelData.description ? modelData.description.replace(/\n/g, "  ") + "\n" : "") + modelData.facts
            onClicked: browser.play(modelData.index)
        }
        Keys.onReturnPressed: playCurrent()
        Keys.onEnterPressed: playCurrent()
        function playCurrent() {
            const s = streamPane.shown[currentIndex]
            if (s) browser.play(s.index)
        }
    }
}
