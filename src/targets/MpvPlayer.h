#pragma once
#include <QObject>
#include <QString>
#include <QUrl>
#include <cstdint>

struct mpv_handle;
struct mpv_event_property;

namespace cj {

// The mpv playback target. Owns the app's single mpv instance (rule 3): create
// it once at startup and reuse it for every item.
//
// All methods must be called on the thread that created the object. mpv's own
// events are delivered to that thread through a queued call, and turned into
// the Qt properties and signals below.
class MpvPlayer : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QUrl source READ source NOTIFY sourceChanged)
    Q_PROPERTY(bool fileLoaded READ fileLoaded NOTIFY fileLoadedChanged)
    Q_PROPERTY(bool paused READ paused WRITE setPaused NOTIFY pausedChanged)
    Q_PROPERTY(double position READ position NOTIFY positionChanged)
    Q_PROPERTY(double duration READ duration NOTIFY durationChanged)
    Q_PROPERTY(double volume READ volume WRITE setVolume NOTIFY volumeChanged)
    Q_PROPERTY(bool muted READ muted WRITE setMuted NOTIFY mutedChanged)
    Q_PROPERTY(bool hwdecEnabled READ hwdecEnabled WRITE setHwdecEnabled NOTIFY hwdecEnabledChanged)
    Q_PROPERTY(QString hwdecCurrent READ hwdecCurrent NOTIFY statsChanged)
    Q_PROPERTY(QString videoCodec READ videoCodec NOTIFY statsChanged)
    Q_PROPERTY(int videoWidth READ videoWidth NOTIFY statsChanged)
    Q_PROPERTY(int videoHeight READ videoHeight NOTIFY statsChanged)
    Q_PROPERTY(double fps READ fps NOTIFY statsChanged)
    Q_PROPERTY(int droppedFrames READ droppedFrames NOTIFY statsChanged)
    Q_PROPERTY(QString title READ title NOTIFY titleChanged)

public:
    // Rule 6: errors are typed so the UI can show each one differently.
    enum class Error {
        UnsupportedFile,  // mpv opened it but found nothing it can play
        LoadFailed,       // missing file, no access, broken stream
        Internal,         // a bug or an mpv failure we did not expect
    };
    Q_ENUM(Error)

    enum class Mode {
        Video,     // renders through MpvVideoItem (vo=libmpv)
        Headless,  // no video or audio output; for tests
    };

    explicit MpvPlayer(Mode mode = Mode::Video, QObject* parent = nullptr);
    ~MpvPlayer() override;

    MpvPlayer(const MpvPlayer&) = delete;
    MpvPlayer& operator=(const MpvPlayer&) = delete;

    // The raw handle, only for the render bridge in this layer.
    mpv_handle* handle() const { return m_mpv; }

    // Called by MpvVideoItem once its render context exists. In Video mode mpv
    // cannot open a file before that, so earlier open() calls wait for it.
    Q_INVOKABLE void markRenderReady();

    QUrl source() const { return m_source; }
    bool fileLoaded() const { return m_fileLoaded; }
    bool paused() const { return m_paused; }
    double position() const { return m_position; }
    double duration() const { return m_duration; }
    double volume() const { return m_volume; }
    bool muted() const { return m_muted; }
    bool hwdecEnabled() const { return m_hwdecEnabled; }
    QString hwdecCurrent() const { return m_hwdecCurrent; }
    QString videoCodec() const { return m_videoCodec; }
    int videoWidth() const { return m_videoWidth; }
    int videoHeight() const { return m_videoHeight; }
    double fps() const { return m_fps; }
    int droppedFrames() const { return m_droppedFrames; }
    QString title() const { return m_title; }

    void setPaused(bool paused);
    void setVolume(double volume);   // 0 to 100
    void setMuted(bool muted);
    void setHwdecEnabled(bool enabled);

    // Opens a local file URL, a path, or anything mpv understands (e.g. av://lavfi:...).
    Q_INVOKABLE void open(const QUrl& url);
    Q_INVOKABLE void openPath(const QString& pathOrUrl);
    Q_INVOKABLE void togglePause();
    Q_INVOKABLE void seek(double seconds);          // absolute
    Q_INVOKABLE void seekRelative(double seconds);
    Q_INVOKABLE void stop();

signals:
    void sourceChanged();
    void fileLoadedChanged();
    void pausedChanged();
    void positionChanged();
    void durationChanged();
    void volumeChanged();
    void mutedChanged();
    void hwdecEnabledChanged();
    void statsChanged();
    void titleChanged();
    void endOfFile();
    void errorOccurred(cj::MpvPlayer::Error error, const QString& message);

private:
    Q_INVOKABLE void processEvents();
    void handlePropertyChange(uint64_t id, const ::mpv_event_property* prop);
    void command(std::initializer_list<const char*> args);
    void load(const QByteArray& target, const QUrl& source);
    void setFileLoaded(bool loaded);

    mpv_handle* m_mpv = nullptr;
    Mode m_mode;
    bool m_renderReady = false;
    QByteArray m_pendingTarget;
    QUrl m_source;
    bool m_fileLoaded = false;
    bool m_paused = false;
    double m_position = 0;
    double m_duration = 0;
    double m_volume = 100;
    bool m_muted = false;
    bool m_hwdecEnabled = true;
    QString m_hwdecCurrent;
    QString m_videoCodec;
    int m_videoWidth = 0;
    int m_videoHeight = 0;
    double m_fps = 0;
    int m_droppedFrames = 0;
    QString m_title;
};

} // namespace cj
