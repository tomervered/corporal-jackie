import QtQuick
import "Format.js" as Format

SettingsGroup {
    id: subtitleSettings

    function setSubtitleStyle(key, value) {
        const style = session.subtitleStyle
        style[key] = value
        session.subtitleStyle = style
    }

    title: "Subtitles"
    // A sample line, drawn the way the player will draw it, over a
    // drawing from Gertie the Dinosaur (Winsor McCay, 1914, public
    // domain; images/README.md).
    Item { width: 1; height: 12 }
    Loader {
        width: Math.min(parent.width, 560)
        height: active ? Math.round(width * 9 / 16) : 0
        active: subtitleSettings.visible && subtitleSettings.expanded
        sourceComponent: SubtitlePreview {}
    }
    Item { width: 1; height: 12 }
    SettingChoice {
        title: "Font"
        enabledHere: session.supports("SubtitleStyle")
        options: subtitleSettings.visible && subtitleSettings.expanded
                 ? [["", "Default"]].concat(Qt.fontFamilies().map(font => [font, font])) : []
        value: session.subtitleStyle.font
        onPicked: v => subtitleSettings.setSubtitleStyle("font", v)
    }
    SettingChoice {
        title: "Size"
        detail: "While watching, G makes subtitles bigger and Shift+G smaller."
        options: Format.subtitleSizes
        value: Math.round(session.subtitleScale * 100) / 100
        onPicked: v => session.subtitleScale = v
    }
    SettingChoice {
        title: "Color"
        options: Format.subtitleColors
        value: session.subtitleStyle.color.toUpperCase()
        onPicked: v => subtitleSettings.setSubtitleStyle("color", v)
    }
    SettingChoice {
        title: "Outline"
        options: Format.subtitleOutlines
        value: session.subtitleStyle.outline
        onPicked: v => subtitleSettings.setSubtitleStyle("outline", v)
    }
    SettingChoice {
        title: "Background"
        options: Format.subtitleBackgrounds
        value: session.subtitleStyle.background
        onPicked: v => subtitleSettings.setSubtitleStyle("background", v)
    }
    SettingSwitch {
        title: "Also restyle subtitles that have their own look"
        detail: "Some subtitles (often anime) come with their own fonts and placement. Off keeps those as they are."
        checked: session.subtitleStyle.restyleAll
        onToggled: on => subtitleSettings.setSubtitleStyle("restyleAll", on)
    }
}
