#include "PlaybackSession.h"

#include "Logging.h"

#include <QFileInfo>
#include <QMetaEnum>
#include <QRegularExpression>

#include <algorithm>

namespace cj {
PlaybackSession::PlaybackSession(PlaybackTarget* target, QObject* parent)
    : QObject(parent)
    , m_target(target)
{
    Q_ASSERT(target);
    m_resumeTimer.setInterval(5000);
    connect(&m_resumeTimer, &QTimer::timeout, this, &PlaybackSession::emitResumePoint);

    connect(target, &PlaybackTarget::stateChanged, this, [this](PlaybackState s) {
        setState(s);
        if (s == PlaybackState::Ended)
            emit ended();
    });
    connect(target, &PlaybackTarget::positionChanged, this, [this](double seconds) {
        m_position = seconds;
        if (m_seekTarget >= 0 && qAbs(seconds - m_seekTarget) < 1.0)
            m_seekTarget = -1;  // arrived
        emit positionChanged();
    });
    connect(target, &PlaybackTarget::durationChanged, this, [this](double seconds) {
        m_duration = seconds;
        emit durationChanged();
    });
    connect(target, &PlaybackTarget::titleChanged, this, [this](const QString& title) {
        if (title.isEmpty() || title == m_title)
            return;
        m_title = title;
        emit titleChanged();
    });
    connect(target, &PlaybackTarget::tracksChanged, this, [this](const QList<Track>& tracks) {
        m_tracks = tracks;
        emit tracksChanged();
    });
    connect(target, &PlaybackTarget::chaptersChanged, this, [this](const QList<Chapter>& chapters) {
        m_chapters = chapters;
        emit chaptersChanged();
    });
    connect(target, &PlaybackTarget::bufferingChanged, this, [this](bool buffering, int percent) {
        m_buffering = buffering;
        m_bufferPercent = percent;
        emit bufferingChanged();
    });
    connect(target, &PlaybackTarget::volumeChanged, this, [this](double volume, bool muted) {
        m_volume = volume;
        m_muted = muted;
        emit volumeChanged();
    });
    connect(target, &PlaybackTarget::errorOccurred, this, [this](PlaybackError e, const QString& msg) {
        if (m_sourceErrorReported) {
            qCInfo(lcSession).noquote() << "Target error after a source error, not shown:" << e << msg;
            return;
        }
        qCWarning(lcSession).noquote() << "Playback error" << e << msg;
        emit errorOccurred(e, msg);
    });
    connect(target, &PlaybackTarget::statsChanged, this, [this](const QMap<QString, QString>& stats) {
        m_stats.clear();
        for (auto it = stats.begin(); it != stats.end(); ++it)
            m_stats.insert(it.key(), it.value());
        emit statsChanged();
    });
    connect(target, &PlaybackTarget::screenshotSaved, this, &PlaybackSession::screenshotSaved);
    connect(target, &PlaybackTarget::settingChanged, this, &PlaybackSession::applyTargetSetting);
    connect(target, &PlaybackTarget::upscalerStatusChanged, this, [this](const QString& status, bool problem) {
        if (status == m_upscalerStatus && problem == m_upscalerProblem)
            return;
        m_upscalerStatus = status;
        m_upscalerProblem = problem;
        emit upscalerStatusChanged();
    });
    connect(target, &PlaybackTarget::appActionRequested, this, [this](const QString& action) {
        if (action == QLatin1String("stop"))
            stop();
        else
            emit appActionRequested(action);
    });

    target->setVolume(m_volume);
    target->setSpeed(m_speed);
    qCInfo(lcSession).noquote() << "Session using target" << target->name();
}

bool PlaybackSession::hasMedia() const
{
    return m_state == PlaybackState::Playing || m_state == PlaybackState::Paused
        || m_state == PlaybackState::Ended;
}

bool PlaybackSession::supports(const QString& capability) const
{
    bool ok = false;
    const int value = QMetaEnum::fromType<Capability>().keyToValue(capability.toLatin1(), &ok);
    return ok && m_target->supports(static_cast<Capability>(value));
}

bool PlaybackSession::require(Capability c, const char* what) const
{
    if (m_target->supports(c))
        return true;
    qCInfo(lcSession) << what << "is not supported by" << m_target->name();
    return false;
}

void PlaybackSession::setState(PlaybackState s)
{
    if (s == m_state)
        return;
    const PlaybackState old = m_state;
    m_state = s;
    if (s == PlaybackState::Playing)
        m_resumeTimer.start();
    else
        m_resumeTimer.stop();
    if (old == PlaybackState::Playing && s == PlaybackState::Paused)
        emitResumePoint();
    emit stateChanged();
}

void PlaybackSession::emitResumePoint()
{
    if (m_source.url.isEmpty() || !hasMedia())
        return;
    emit resumePoint(m_source, m_position, m_duration);
}

// --- Items -------------------------------------------------------------------

std::optional<PlayableSource> PlaybackSession::resolve(const QString& input) const
{
    for (const SourceResolver& resolver : m_resolvers)
        if (auto source = resolver(input)) {
            if (source->origin.isEmpty())
                source->origin = input;
            return source;
        }
    return std::nullopt;
}

void PlaybackSession::open(const QUrl& url, double startSeconds)
{
    if (!url.isValid() || url.isEmpty())
        return;
    // A dropped .torrent file, say, is a local file that a resolver claims.
    if (auto source = resolve(url.isLocalFile() ? url.toLocalFile() : url.toString())) {
        load(*source, startSeconds);
        return;
    }
    load(PlayableSource::fromUrl(url, url.isLocalFile() ? QFileInfo(url.toLocalFile()).completeBaseName()
                                                        : url.fileName()),
         startSeconds);
}

void PlaybackSession::openPath(const QString& pathOrUrl, double startSeconds)
{
    // Magnet links and info hashes have no "://", so ask the resolvers first.
    if (auto source = resolve(pathOrUrl.trimmed())) {
        load(*source, startSeconds);
        return;
    }
    // "scheme://..." is a URL; anything else, including C:\..., is a local path.
    static const QRegularExpression scheme(QStringLiteral("^[A-Za-z][A-Za-z0-9+.-]+://"));
    if (scheme.match(pathOrUrl).hasMatch())
        open(QUrl(pathOrUrl), startSeconds);
    else
        open(QUrl::fromLocalFile(QFileInfo(pathOrUrl).absoluteFilePath()), startSeconds);
}

bool PlaybackSession::playSource(const PlayableSource& source, double startSeconds, const std::function<void()>& onAccepted)
{
    // A magnet link from an add-on still goes through the torrent resolver;
    // everything else the adapter said (title, subtitles, headers) is kept.
    // A source played before keeps what it was opened from: resolve that
    // again, since the stream URL it got last time is gone.
    PlayableSource s = source;
    if (auto resolved = resolve(source.origin.isEmpty() ? source.url.toString() : source.origin)) {
        s.url = resolved->url;
        s.kind = resolved->kind;
        s.origin = resolved->origin;
        if (s.title.isEmpty())
            s.title = resolved->title;
    }
    return startPlayback(s, startSeconds, onAccepted);
}

void PlaybackSession::addSubtitleTracks(const QList<SubtitleSource>& subtitles, const QUrl& forSource)
{
    if (forSource != m_source.url || !(hasMedia() || m_state == PlaybackState::Loading))
        return;  // the user has moved on
    for (const SubtitleSource& sub : subtitles)
        m_target->addSubtitleTrack(sub);
}

void PlaybackSession::load(const PlayableSource& source, double startSeconds)
{
    startPlayback(source, startSeconds, {});
}

bool PlaybackSession::startPlayback(const PlayableSource& source, double startSeconds, const std::function<void()>& onAccepted)
{
    if (m_sourceCheck) {
        if (const auto refusal = m_sourceCheck(source)) {
            qCInfo(lcSession).noquote() << "Not loading" << logging::redactUrl(source.url) << ":" << refusal->second;
            emit errorOccurred(refusal->first, refusal->second);
            return false;
        }
    }
    emitResumePoint();  // remember where the previous item was left

    // History needs the chosen episode before it can look up where we left off.
    if (onAccepted)
        onAccepted();
    if (startSeconds <= 0 && m_resumeLookup)
        startSeconds = std::max(0.0, m_resumeLookup(source));
    m_source = source;
    m_title = source.title;
    m_position = startSeconds;
    m_duration = 0;
    m_tracks.clear();
    m_chapters.clear();
    m_stats.clear();
    // Per-item settings start fresh; speed, volume and subtitle size carry over.
    m_subtitleDelay = 0;
    m_audioDelay = 0;
    m_abA = m_abB = -1;
    m_aspect.clear();
    m_crop.clear();
    m_seekTarget = -1;
    m_sourceErrorReported = false;
    const bool streamWasSet = m_stream.phase != StreamPhase::Inactive;
    m_stream = {};

    qCInfo(lcSession).noquote() << "Loading" << logging::redactUrl(source.url) << "at" << startSeconds;
    setState(PlaybackState::Loading);
    m_target->load(source, startSeconds);
    applyItemSettings();

    emit sourceChanged();
    emit titleChanged();
    emit positionChanged();
    emit durationChanged();
    emit tracksChanged();
    emit chaptersChanged();
    emit statsChanged();
    emit subtitleDelayChanged();
    emit audioDelayChanged();
    emit abLoopChanged();
    emit aspectRatioChanged();
    emit cropChanged();
    if (streamWasSet)
        emit streamStatusChanged();
    return true;
}

void PlaybackSession::setStreamStatus(const StreamStatus& status)
{
    // A report for the previous item can arrive after the next one loaded.
    if (status.source != m_source.url || status == m_stream)
        return;
    if (status.phase != m_stream.phase)
        qCInfo(lcSession) << "Stream" << status.phase << status.percent << "%" << status.peers << "peers";
    m_stream = status;
    emit streamStatusChanged();
}

void PlaybackSession::reportSourceError(PlaybackError error, const QString& message, const QUrl& source)
{
    if (source != m_source.url || m_sourceErrorReported)
        return;
    m_sourceErrorReported = true;
    qCWarning(lcSession).noquote() << "Source error" << error << message;
    emit errorOccurred(error, message);
}

void PlaybackSession::applyItemSettings()
{
    if (m_target->supports(Capability::SubtitleDelay)) m_target->setSubtitleDelay(m_subtitleDelay);
    if (m_target->supports(Capability::AudioDelay))    m_target->setAudioDelay(m_audioDelay);
    if (m_target->supports(Capability::AbLoop))        m_target->setAbLoop(-1, -1);
    if (m_target->supports(Capability::AspectRatio))   m_target->setAspectRatio(m_aspect);
    if (m_target->supports(Capability::Crop))          m_target->setCrop(m_crop);
}

void PlaybackSession::play()        { if (hasMedia()) m_target->play(); }
void PlaybackSession::pause()       { if (hasMedia()) m_target->pause(); }
void PlaybackSession::togglePause()
{
    if (paused() || m_state == PlaybackState::Ended)
        play();
    else
        pause();
}

void PlaybackSession::stop()
{
    emitResumePoint();
    m_target->stop();
}

void PlaybackSession::seek(double seconds)
{
    if (!hasMedia())
        return;
    seconds = qMax(0.0, m_duration > 0 ? qMin(seconds, m_duration) : seconds);
    m_seekTarget = seconds;
    m_seekClock.start();
    m_target->seek(seconds);
}

void PlaybackSession::seekRelative(double seconds)
{
    // A streamed seek can take a few seconds; a held arrow key must keep
    // jumping from where the last jump is going, not from where playback was.
    // After 10 s without arriving, trust the reported position again.
    const bool seeking = m_seekTarget >= 0 && m_seekClock.isValid() && m_seekClock.elapsed() < 10000;
    seek((seeking ? m_seekTarget : m_position) + seconds);
}

} // namespace cj
