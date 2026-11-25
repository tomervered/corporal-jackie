import QtQuick
import "Format.js" as Format

SettingsGroup {
    title: "Picture"
    SettingSwitch {
        title: "Hardware decoding"
        detail: "Lets the graphics card decode the video. Turn off only if the picture shows glitches."
        enabledHere: session.supports("HardwareDecoding")
        checked: session.hardwareDecoding
        onToggled: on => session.hardwareDecoding = on
    }
    // Laptops with two graphics cards (Windows): which one the app runs on.
    SettingChoice {
        visible: graphics.canChoose
        title: "Graphics card"
        detail: (graphics.restartNeeded ? "Restart the app to switch. " : "")
              + "Power saving: " + graphics.savingName + ". High performance: " + graphics.performanceName + "."
              + (graphics.activeName !== "" ? " Now running on " + graphics.activeName + "." : "")
        options: [["auto", "Automatic"], ["saving", "Power saving"], ["performance", "High performance"]]
        value: graphics.preference
        onPicked: v => graphics.preference = v
    }
    SettingNote {
        visible: graphics.vsrHint !== ""
        text: graphics.vsrHint
    }
    SettingChoice {
        visible: session.upscalers.length > 1
        title: "Upscaling"
        detail: session.upscaler === "off"
              ? "Sharpens videos smaller than your screen. Snapdragon GSR and AMD CAS are the lightest, for weak graphics chips; AMD FSR is sharper; Anime4K is for cartoons and anime. Off uses no extra memory or graphics power."
              : "Uses extra graphics power while a video plays. Turn it off if playback stutters on this computer."
        options: session.upscalers.map(name => [name, Format.upscalerLabel(name)])
        value: session.upscaler
        onPicked: v => session.upscaler = v
    }
    SettingSwitch {
        visible: session.upscalers.indexOf("intel") >= 0
        objectName: "intelVsrCopybackToggle"
        title: "Intel VSR compatibility mode"
        detail: "On by default, because Intel VSR can tear or show blocks on some screens without it. Each frame is copied through memory instead of straight to the graphics card, which is slower: 1080p60 upscaled to 4K can stutter. Turn it off then, or use AMD FSR."
        checked: session.intelVsrCopyback
        onToggled: on => session.intelVsrCopyback = on
    }
    SettingChoice {
        visible: session.upscaler !== "off"
        title: "Upscale to"
        options: Format.upscaleTargets
        value: session.upscaleHeight
        onPicked: v => session.upscaleHeight = v
    }
    SettingSwitch {
        visible: session.upscalers.length > 1
        objectName: "upscalerBadgeToggle"
        title: "Show upscaling indicator during playback"
        detail: "The note at the top left of the video that says what the upscaler is doing."
        checked: preferences.showUpscalerBadge
        onToggled: on => preferences.showUpscalerBadge = on
    }
    SettingNote { text: "Press I while playing to see dropped and late frames." }
}
