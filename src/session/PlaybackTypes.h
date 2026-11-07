#pragma once
// Plain types shared by the Session, every playback target, and the UI.
// See docs/TARGET_CONTRACT.md.

#include <QFlags>
#include <QList>
#include <QMap>
#include <QMetaType>
#include <QObject>
#include <QString>
#include <QUrl>

namespace cj {
Q_NAMESPACE

enum class PlaybackState { Idle, Loading, Playing, Paused, Ended };
Q_ENUM_NS(PlaybackState)

// Rule 6: typed errors, so the UI can explain each case differently.
enum class PlaybackError { UnsupportedFile, LoadFailed, TargetUnavailable, Internal };
Q_ENUM_NS(PlaybackError)

// Optional features. A target lists the ones it has; the Session never sends
// the others and the UI hides them. "Local only" ones need the video to be
// decoded on this computer, so a Cast target will not offer them.
enum class Capability : unsigned {
    None             = 0,
    SubtitleDelay    = 1u << 0,
    SubtitleStyle    = 1u << 1,
    AudioDelay       = 1u << 2,
    Chapters         = 1u << 3,
    FrameStep        = 1u << 4,   // local only
    Screenshot       = 1u << 5,   // local only
    AbLoop           = 1u << 6,
    AspectRatio      = 1u << 7,
    Crop             = 1u << 8,   // local only
    VideoEqualizer   = 1u << 9,   // local only
    HardwareDecoding = 1u << 10,  // local only
};
Q_ENUM_NS(Capability)
Q_DECLARE_FLAGS(Capabilities, Capability)

// Something a target can play. Source adapters (Step 5) produce these.
struct PlayableSource {
    QUrl url;
    QString title;          // shown in the UI; the target may report a better one
    bool operator==(const PlayableSource&) const = default;
};

struct Track {
    enum class Type { Video, Audio, Subtitle };
    Type type = Type::Video;
    int id = 0;             // unique within its type
    QString title;
    QString language;
    QString codec;
    bool selected = false;
    bool external = false;  // loaded from a separate file
    bool isDefault = false;
};

struct Chapter {
    double start = 0;       // seconds
    QString title;
};

// Each value -100…100, 0 = unchanged.
struct VideoEqualizer {
    int brightness = 0;
    int contrast = 0;
    int saturation = 0;
    int gamma = 0;
    int hue = 0;
    bool operator==(const VideoEqualizer&) const = default;
};

} // namespace cj

Q_DECLARE_OPERATORS_FOR_FLAGS(cj::Capabilities)
Q_DECLARE_METATYPE(cj::PlayableSource)
Q_DECLARE_METATYPE(cj::Track)
Q_DECLARE_METATYPE(cj::Chapter)
