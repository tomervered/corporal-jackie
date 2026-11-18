import QtQuick
import QtQuick.Controls.Basic

// The app's drop-down. Shows its entries as plain text: catalog and genre
// names come from add-ons (see README).
ComboBox {
    id: control
    font.pixelSize: Theme.bodySize
    implicitHeight: Theme.controlHeight
    leftPadding: 12
    rightPadding: 34
    Accessible.name: displayText
    contentItem: Text {
        textFormat: Text.PlainText
        text: control.displayText
        font: control.font
        color: Theme.text
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
    indicator: Icon {
        x: control.width - width - 12
        y: (control.height - height) / 2
        name: "chevronDown"
        size: 15
        color: Theme.muted
    }
    background: Rectangle {
        implicitWidth: 140
        radius: Theme.radius
        color: control.down ? Theme.pressed : control.hovered ? Theme.raised : Theme.panel
        border.width: control.visualFocus ? 2 : 1
        border.color: control.visualFocus ? Theme.accent : Theme.line
        opacity: control.enabled ? 1 : 0.6
    }
    delegate: ItemDelegate {
        id: entry
        required property var modelData
        required property int index
        width: ListView.view ? ListView.view.width : implicitWidth
        highlighted: control.highlightedIndex === index
        contentItem: Text {
            textFormat: Text.PlainText
            text: Array.isArray(control.model) ? entry.modelData : (control.textRole ? entry.modelData[control.textRole] : entry.modelData)
            font: control.font
            color: control.currentIndex === entry.index ? Theme.accent : Theme.text
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle { color: entry.highlighted ? Theme.raised : "transparent" }
    }
    popup: Popup {
        y: control.height + 4
        width: Math.max(control.width, 200)
        height: Math.min(contentItem.implicitHeight + 8, control.Window.height - 40)
        padding: 4
        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: control.popup.visible ? control.delegateModel : null
            currentIndex: control.highlightedIndex
            ScrollIndicator.vertical: ScrollIndicator {}
        }
        background: Rectangle { color: Theme.panel; radius: Theme.radius; border.color: Theme.line }
    }
}
