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
enum class PlaybackError {
    UnsupportedFile,    // not a format the player understands
    LoadFailed,         // could not be opened: missing, no access, broken stream
    TargetUnavailable,  // the playback device stopped responding
    Internal,           // a bug or an unexpected failure inside the player
    NoPeers,            // a torrent: nobody sharing it was found in time
    StreamFailed,       // a torrent stopped with an error (a full disk, say)
};
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

// Where a streamed item (a torrent) is, for the buffering display. Local
// files and plain links stay at None.
enum class StreamPhase {
    Inactive,          // not a stream, or nothing loaded
    FindingPeers,      // looking for anyone who has it
    FetchingMetadata,  // peers found, getting the file list
    Buffering,         // downloading the part the player needs next
    Stalled,           // nothing arriving for a while; still retrying
    Ready,             // the player has what it needs
};
Q_ENUM_NS(StreamPhase)

struct StreamStatus {
    StreamPhase phase = StreamPhase::Inactive;
    int percent = 0;          // of the part being waited for, while buffering
    int peers = 0;
    qint64 downloadRate = 0;  // bytes per second
    double bufferedSeconds = 0;  // verified video ahead of the read position
    QUrl source;              // the item this is about; stale reports are ignored
    bool operator==(const StreamStatus&) const = default;
};

// A subtitle file that goes with a source: next to a local video, or from a
// subtitle add-on.
struct SubtitleSource {
    QUrl url;
    QString language;       // as given: "en", "eng", "English"...
    QString label;          // shown in the track menu; may be empty
    bool operator==(const SubtitleSource&) const = default;
};

// The one record every source adapter produces (Step 5), so the Session never
// cares where an item came from.
struct PlayableSource {
    enum class Kind { LocalFile, Http, Torrent };

    QUrl url;               // file://, http(s)://, magnet: or torrent://
    QString title;          // shown in the UI; the target may report a better one
    Kind kind = Kind::LocalFile;
    QMap<QString, QString> headers;   // HTTP request headers the server needs
    QList<SubtitleSource> subtitles;  // added as tracks, not selected
    // Hints for a Cast target later; filled from add-on hints when known.
    QString container;
    QString videoCodec;
    QString audioCodec;
    QString adapter;        // which source adapter made it, e.g. "stremio:org.example"
    QString origin;         // the text it was opened from (a magnet link, say) when a
                            // resolver turned it into another URL; empty otherwise

    // A source for a plain link or path: the kind follows from the URL.
    static PlayableSource fromUrl(const QUrl& url, const QString& title)
    {
        PlayableSource s;
        s.url = url;
        s.title = title;
        const QString scheme = url.scheme().toLower();
        s.kind = url.isLocalFile() ? Kind::LocalFile
               : (scheme == QLatin1String("magnet") || scheme == QLatin1String("torrent")) ? Kind::Torrent
               : Kind::Http;
        return s;
    }
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
Q_DECLARE_METATYPE(cj::SubtitleSource)
Q_DECLARE_METATYPE(cj::Track)
Q_DECLARE_METATYPE(cj::Chapter)
Q_DECLARE_METATYPE(cj::StreamStatus)
