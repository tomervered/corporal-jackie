import QtQuick

// Grey poster shapes where a list is still loading, so the page keeps its
// layout instead of jumping when the posters arrive.
Row {
    id: placeholders
    property int count: 6
    spacing: 14
    clip: true
    Repeater {
        model: placeholders.count
        Column {
            spacing: 8
            Rectangle {
                width: Theme.posterWidth; height: Theme.posterWidth * 1.5
                radius: Theme.radius
                color: Theme.card
                SequentialAnimation on opacity {
                    running: placeholders.visible
                    loops: Animation.Infinite
                    NumberAnimation { from: 1; to: 0.55; duration: 700; easing.type: Easing.InOutQuad }
                    NumberAnimation { from: 0.55; to: 1; duration: 700; easing.type: Easing.InOutQuad }
                }
            }
            Rectangle { width: Theme.posterWidth * 0.7; height: 10; radius: 5; color: Theme.card }
        }
    }
}
