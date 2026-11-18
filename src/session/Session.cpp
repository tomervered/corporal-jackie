#include "Session.h"

#include "Logging.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QMetaEnum>
#include <QRegularExpression>
#include <QStandardPaths>

#include <algorithm>

namespace cj {
namespace {

const QStringList kAspectRatios = {QString(), QStringLiteral("16:9"), QStringLiteral("4:3"),
                                   QStringLiteral("2.35:1"), QStringLiteral("1:1")};
const QStringList kCrops = {QString(), QStringLiteral("16:9"), QStringLiteral("2.39:1"),
                            QStringLiteral("4:3")};

QString next(const QStringList& list, const QString& current)
{
    return list.at((list.indexOf(current) + 1) % list.size());
}

// "16:9" or "2.35:1" -> width/height.
double ratioOf(const QString& text)
{
    const QStringList parts = text.split(QLatin1Char(':'));
    const double b = parts.size() > 1 ? parts.at(1).toDouble() : 1.0;
    return b > 0 ? parts.at(0).toDouble() / b : 0.0;
}

// A ratio the target reports, as one of the names the app cycles through
// when it is one of them, else as a number.
QString aspectName(double ratio)
{
    if (ratio <= 0)
        return {};
    for (const QString& name : kAspectRatios)
        if (!name.isEmpty() && qAbs(ratioOf(name) - ratio) < 0.01)
            return name;
    return QString::number(ratio, 'f', 2);
}

} // namespace

Session::Session(PlaybackTarget* target, QObject* parent)
    : QObject(parent)
    , m_target(target)
{
    Q_ASSERT(target);
    m_resumeTimer.setInterval(5000);
    connect(&m_resumeTimer, &QTimer::timeout, this, &Session::emitResumePoint);

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
    connect(target, &PlaybackTarget::screenshotSaved, this, &Session::screenshotSaved);
    connect(target, &PlaybackTarget::settingChanged, this, &Session::applyTargetSetting);
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

bool Session::hasMedia() const
{
    return m_state == PlaybackState::Playing || m_state == PlaybackState::Paused
        || m_state == PlaybackState::Ended;
}

bool Session::supports(const QString& capability) const
{
    bool ok = false;
    const int value = QMetaEnum::fromType<Capability>().keyToValue(capability.toLatin1(), &ok);
    return ok && m_target->supports(static_cast<Capability>(value));
}

bool Session::require(Capability c, const char* what) const
{
    if (m_target->supports(c))
        return true;
    qCInfo(lcSession) << what << "is not supported by" << m_target->name();
    return false;
}

void Session::setState(PlaybackState s)
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

void Session::emitResumePoint()
{
    if (m_source.url.isEmpty() || !hasMedia())
        return;
    emit resumePoint(m_source, m_position, m_duration);
}

// --- Items -------------------------------------------------------------------

std::optional<PlayableSource> Session::resolve(const QString& input) const
{
    for (const SourceResolver& resolver : m_resolvers)
        if (auto source = resolver(input)) {
            if (source->origin.isEmpty())
                source->origin = input;
            return source;
        }
    return std::nullopt;
}

void Session::open(const QUrl& url, double startSeconds)
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

void Session::openPath(const QString& pathOrUrl, double startSeconds)
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

void Session::playSource(const PlayableSource& source, double startSeconds)
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
    load(s, startSeconds);
}

void Session::addSubtitleTracks(const QList<SubtitleSource>& subtitles, const QUrl& forSource)
{
    if (forSource != m_source.url || !(hasMedia() || m_state == PlaybackState::Loading))
        return;  // the user has moved on
    for (const SubtitleSource& sub : subtitles)
        m_target->addSubtitleTrack(sub);
}

void Session::load(const PlayableSource& source, double startSeconds)
{
    if (m_sourceCheck) {
        if (const auto refusal = m_sourceCheck(source)) {
            qCInfo(lcSession).noquote() << "Not loading" << logging::redactUrl(source.url) << ":" << refusal->second;
            emit errorOccurred(refusal->first, refusal->second);
            return;
        }
    }
    emitResumePoint();  // remember where the previous item was left

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
}

void Session::setStreamStatus(const StreamStatus& status)
{
    // A report for the previous item can arrive after the next one loaded.
    if (status.source != m_source.url || status == m_stream)
        return;
    if (status.phase != m_stream.phase)
        qCInfo(lcSession) << "Stream" << status.phase << status.percent << "%" << status.peers << "peers";
    m_stream = status;
    emit streamStatusChanged();
}

void Session::reportSourceError(PlaybackError error, const QString& message, const QUrl& source)
{
    if (source != m_source.url || m_sourceErrorReported)
        return;
    m_sourceErrorReported = true;
    qCWarning(lcSession).noquote() << "Source error" << error << message;
    emit errorOccurred(error, message);
}

void Session::applyItemSettings()
{
    if (m_target->supports(Capability::SubtitleDelay)) m_target->setSubtitleDelay(m_subtitleDelay);
    if (m_target->supports(Capability::AudioDelay))    m_target->setAudioDelay(m_audioDelay);
    if (m_target->supports(Capability::AbLoop))        m_target->setAbLoop(-1, -1);
    if (m_target->supports(Capability::AspectRatio))   m_target->setAspectRatio(m_aspect);
    if (m_target->supports(Capability::Crop))          m_target->setCrop(m_crop);
}

void Session::play()        { if (hasMedia()) m_target->play(); }
void Session::pause()       { if (hasMedia()) m_target->pause(); }
void Session::togglePause() { paused() || m_state == PlaybackState::Ended ? play() : pause(); }

void Session::stop()
{
    emitResumePoint();
    m_target->stop();
}

void Session::seek(double seconds)
{
    if (!hasMedia())
        return;
    seconds = qMax(0.0, m_duration > 0 ? qMin(seconds, m_duration) : seconds);
    m_seekTarget = seconds;
    m_seekClock.start();
    m_target->seek(seconds);
}

void Session::seekRelative(double seconds)
{
    // A streamed seek can take a few seconds; a held arrow key must keep
    // jumping from where the last jump is going, not from where playback was.
    // After 10 s without arriving, trust the reported position again.
    const bool seeking = m_seekTarget >= 0 && m_seekClock.isValid() && m_seekClock.elapsed() < 10000;
    seek((seeking ? m_seekTarget : m_position) + seconds);
}

// --- Tracks ------------------------------------------------------------------

QVariantList Session::tracksOf(Track::Type type) const
{
    QVariantList list;
    for (const Track& t : m_tracks) {
        if (t.type != type)
            continue;
        QString label = t.title;
        if (!t.language.isEmpty())
            label = label.isEmpty() ? t.language : QStringLiteral("%1 (%2)").arg(label, t.language);
        if (label.isEmpty())
            label = tr("Track %1").arg(t.id);
        if (t.external)
            label += tr(" (file)");
        list.append(QVariantMap{{QStringLiteral("id"), t.id},
                                {QStringLiteral("label"), label},
                                {QStringLiteral("language"), t.language},
                                {QStringLiteral("codec"), t.codec},
                                {QStringLiteral("selected"), t.selected}});
    }
    return list;
}

int Session::selectedOf(Track::Type type) const
{
    for (const Track& t : m_tracks)
        if (t.type == type && t.selected)
            return t.id;
    return -1;
}

void Session::setAudioTrack(int id)     { if (hasMedia()) m_target->setAudioTrack(id); }
void Session::setSubtitleTrack(int id)  { if (hasMedia()) m_target->setSubtitleTrack(id); }

void Session::addExternalSubtitle(const QUrl& url)
{
    if (hasMedia() && url.isValid())
        m_target->addExternalSubtitle(url);
}

// --- Output ------------------------------------------------------------------

void Session::setVolume(double volume)
{
    volume = qBound(0.0, volume, PlaybackTarget::kMaxVolume);
    if (qFuzzyCompare(volume, m_volume))
        return;
    m_volume = volume;
    m_target->setVolume(volume);
    emit volumeChanged();
}

void Session::setMuted(bool muted)
{
    if (muted == m_muted)
        return;
    m_muted = muted;
    m_target->setMuted(muted);
    emit volumeChanged();
}

void Session::setSpeed(double factor)
{
    factor = qBound(0.25, factor, 4.0);
    if (qFuzzyCompare(factor, m_speed))
        return;
    m_speed = factor;
    m_target->setSpeed(factor);
    emit speedChanged();
}

// --- Optional features ---------------------------------------------------------

void Session::setSubtitleDelay(double seconds)
{
    if (!require(Capability::SubtitleDelay, "Subtitle delay") || qFuzzyCompare(1 + seconds, 1 + m_subtitleDelay))
        return;
    m_subtitleDelay = seconds;
    m_target->setSubtitleDelay(seconds);
    emit subtitleDelayChanged();
}

void Session::setSubtitleScale(double factor)
{
    factor = qBound(0.3, factor, 3.0);
    if (!require(Capability::SubtitleStyle, "Subtitle size") || qFuzzyCompare(factor, m_subtitleScale))
        return;
    m_subtitleScale = factor;
    m_target->setSubtitleScale(factor);
    emit subtitleScaleChanged();
}

void Session::setSubtitleStyle(const SubtitleStyle& style)
{
    SubtitleStyle s = style;
    s.outline = qBound(0.0, s.outline, 10.0);
    s.background = qBound(0, s.background, 100);
    static const QRegularExpression rgb(QStringLiteral("^#[0-9A-Fa-f]{6}$"));
    if (!rgb.match(s.color).hasMatch())
        s.color = SubtitleStyle().color;
    if (!require(Capability::SubtitleStyle, "Subtitle style") || s == m_subtitleStyle)
        return;
    m_subtitleStyle = s;
    m_target->setSubtitleStyle(s);
    emit subtitleStyleChanged();
}

void Session::setAudioDelay(double seconds)
{
    if (!require(Capability::AudioDelay, "Audio delay") || qFuzzyCompare(1 + seconds, 1 + m_audioDelay))
        return;
    m_audioDelay = seconds;
    m_target->setAudioDelay(seconds);
    emit audioDelayChanged();
}

QVariantList Session::chapters() const
{
    QVariantList list;
    for (const Chapter& c : m_chapters)
        list.append(QVariantMap{{QStringLiteral("start"), c.start}, {QStringLiteral("title"), c.title}});
    return list;
}

int Session::currentChapter() const
{
    int current = -1;
    for (int i = 0; i < m_chapters.size(); ++i)
        if (m_chapters.at(i).start <= m_position + 0.01)
            current = i;
    return current;
}

void Session::nextChapter()
{
    if (!require(Capability::Chapters, "Chapters") || m_chapters.isEmpty())
        return;
    if (const int next = currentChapter() + 1; next < m_chapters.size())
        m_target->seekChapter(next);
}

void Session::goToChapter(int index)
{
    if (!require(Capability::Chapters, "Chapters") || index < 0 || index >= m_chapters.size())
        return;
    m_target->seekChapter(index);
}

void Session::previousChapter()
{
    if (!require(Capability::Chapters, "Chapters") || m_chapters.isEmpty())
        return;
    // Like a CD player: go to the start of this chapter, or the previous one
    // if we are already within its first 3 seconds.
    int index = currentChapter();
    if (index > 0 && m_position - m_chapters.at(index).start < 3)
        --index;
    m_target->seekChapter(qMax(0, index));
}

void Session::frameStep(bool forward)
{
    if (hasMedia() && require(Capability::FrameStep, "Frame step"))
        m_target->frameStep(forward);
}

QString Session::screenshotFolder()
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    if (dir.isEmpty())
        dir = QDir::homePath();
    return QDir(dir).filePath(QStringLiteral("Corporal Jackie"));
}

void Session::screenshot()
{
    if (!hasMedia() || !require(Capability::Screenshot, "Screenshots"))
        return;
    const QString dir = screenshotFolder();
    QDir().mkpath(dir);
    QString name = m_title.isEmpty() ? QStringLiteral("screenshot") : m_title;
    name.replace(QRegularExpression(QStringLiteral(R"([\\/:*?"<>|])")), QStringLiteral("_"));
    const int secs = int(m_position);
    const QString file = QStringLiteral("%1 %2-%3-%4.png")
                             .arg(name)
                             .arg(secs / 3600, 2, 10, QLatin1Char('0'))
                             .arg(secs / 60 % 60, 2, 10, QLatin1Char('0'))
                             .arg(secs % 60, 2, 10, QLatin1Char('0'));
    m_target->screenshot(QDir(dir).filePath(file));
}

void Session::toggleAbLoop()
{
    if (!hasMedia() || !require(Capability::AbLoop, "A-B repeat"))
        return;
    if (m_abA < 0) {
        m_abA = m_position;
    } else if (m_abB < 0 && m_position > m_abA) {
        m_abB = m_position;
        m_target->setAbLoop(m_abA, m_abB);
    } else {
        m_abA = m_abB = -1;
        m_target->setAbLoop(-1, -1);
    }
    emit abLoopChanged();
}

void Session::cycleAspectRatio()
{
    if (!hasMedia() || !require(Capability::AspectRatio, "Aspect ratio"))
        return;
    m_aspect = next(kAspectRatios, m_aspect);
    m_target->setAspectRatio(m_aspect);
    emit aspectRatioChanged();
}

void Session::cycleCrop()
{
    if (!hasMedia() || !require(Capability::Crop, "Crop"))
        return;
    m_crop = next(kCrops, m_crop);
    m_target->setCrop(m_crop);
    emit cropChanged();
}

void Session::setEqualizer(const QString& name, int value)
{
    if (!require(Capability::VideoEqualizer, "Video equalizer"))
        return;
    value = qBound(-100, value, 100);
    VideoEqualizer eq = m_eq;
    if (name == QLatin1String("brightness"))      eq.brightness = value;
    else if (name == QLatin1String("contrast"))   eq.contrast = value;
    else if (name == QLatin1String("saturation")) eq.saturation = value;
    else if (name == QLatin1String("gamma"))      eq.gamma = value;
    else if (name == QLatin1String("hue"))        eq.hue = value;
    else return;
    if (eq == m_eq)
        return;
    m_eq = eq;
    m_target->setVideoEqualizer(eq);
    emit equalizerChanged();
}

void Session::resetEqualizer()
{
    if (!require(Capability::VideoEqualizer, "Video equalizer") || m_eq == VideoEqualizer{})
        return;
    m_eq = {};
    m_target->setVideoEqualizer(m_eq);
    emit equalizerChanged();
}

void Session::setFullScreen(bool on)
{
    if (on == m_fullScreen)
        return;
    m_fullScreen = on;
    m_target->setFullScreen(on);
    emit fullScreenChanged();
}

bool Session::keyEvent(int key, int modifiers, const QString& text, bool pressed)
{
    if (!m_target->supports(Capability::NativeKeys))
        return false;
    return m_target->keyEvent(key, Qt::KeyboardModifiers(modifiers), text, pressed);
}

// The target changed a setting by itself (mpv's own keys did): take its word
// for it, without sending it back.
void Session::applyTargetSetting(const QString& name, const QVariant& value)
{
    const auto update = [](auto& field, auto v, auto signal, Session* self) {
        if (field == v)
            return;
        field = v;
        emit (self->*signal)();
    };
    if (name == QLatin1String("speed"))
        update(m_speed, value.toDouble(), &Session::speedChanged, this);
    else if (name == QLatin1String("subtitleDelay"))
        update(m_subtitleDelay, value.toDouble(), &Session::subtitleDelayChanged, this);
    else if (name == QLatin1String("audioDelay"))
        update(m_audioDelay, value.toDouble(), &Session::audioDelayChanged, this);
    else if (name == QLatin1String("subtitleScale"))
        update(m_subtitleScale, value.toDouble(), &Session::subtitleScaleChanged, this);
    else if (name == QLatin1String("abLoopA"))
        update(m_abA, value.toDouble() < 0 ? -1.0 : value.toDouble(), &Session::abLoopChanged, this);
    else if (name == QLatin1String("abLoopB"))
        update(m_abB, value.toDouble() < 0 ? -1.0 : value.toDouble(), &Session::abLoopChanged, this);
    else if (name == QLatin1String("aspectRatio"))
        update(m_aspect, aspectName(value.toDouble()), &Session::aspectRatioChanged, this);
    else if (name == QLatin1String("brightness"))
        update(m_eq.brightness, value.toInt(), &Session::equalizerChanged, this);
    else if (name == QLatin1String("contrast"))
        update(m_eq.contrast, value.toInt(), &Session::equalizerChanged, this);
    else if (name == QLatin1String("saturation"))
        update(m_eq.saturation, value.toInt(), &Session::equalizerChanged, this);
    else if (name == QLatin1String("gamma"))
        update(m_eq.gamma, value.toInt(), &Session::equalizerChanged, this);
    else if (name == QLatin1String("hue"))
        update(m_eq.hue, value.toInt(), &Session::equalizerChanged, this);
    else if (name == QLatin1String("hardwareDecoding"))
        update(m_hardwareDecoding, value.toBool(), &Session::hardwareDecodingChanged, this);
    else if (name == QLatin1String("fullScreen"))
        update(m_fullScreen, value.toBool(), &Session::fullScreenChanged, this);
    else if (name == QLatin1String("upscaler")) {
        const QString wanted = value.toString();
        Upscaler u = Upscaler::Off;
        for (const Upscaler candidate : {Upscaler::Sgsr, Upscaler::Cas, Upscaler::Fsr, Upscaler::Anime4k, Upscaler::NvidiaVsr, Upscaler::IntelVsr, Upscaler::MetalFx})
            if (upscalerName(candidate) == wanted)
                u = candidate;
        update(m_upscaler, u, &Session::upscalerChanged, this);
    }
    else
        qCInfo(lcSession) << "Unknown setting from the target:" << name;
}

void Session::setHardwareDecoding(bool enabled)
{
    if (!require(Capability::HardwareDecoding, "Hardware decoding switch") || enabled == m_hardwareDecoding)
        return;
    m_hardwareDecoding = enabled;
    m_target->setHardwareDecoding(enabled);
    emit hardwareDecodingChanged();
}

namespace {
struct UpscalerInfo {
    Upscaler upscaler;
    const char* name;
    Capability capability;
};
constexpr UpscalerInfo kUpscalers[] = {
    {Upscaler::Sgsr,      "sgsr",   Capability::UpscaleSgsr},  // lightest first
    {Upscaler::Cas,       "cas",    Capability::UpscaleCas},
    {Upscaler::Fsr,       "fsr",    Capability::UpscaleFsr},
    {Upscaler::Anime4k,   "anime4k", Capability::UpscaleAnime4k},
    {Upscaler::NvidiaVsr, "nvidia", Capability::UpscaleNvidiaVsr},
    {Upscaler::IntelVsr,  "intel",  Capability::UpscaleIntelVsr},
    {Upscaler::MetalFx,   "metalfx", Capability::UpscaleMetalFx},
};
} // namespace

QString Session::upscalerName(Upscaler u)
{
    for (const UpscalerInfo& info : kUpscalers)
        if (info.upscaler == u)
            return QString::fromLatin1(info.name);
    return QStringLiteral("off");
}

QStringList Session::upscalers() const
{
    QStringList names{QStringLiteral("off")};
    for (const UpscalerInfo& info : kUpscalers)
        if (m_target->supports(info.capability))
            names << QString::fromLatin1(info.name);
    return names;
}

void Session::setUpscaler(const QString& name)
{
    Upscaler wanted = Upscaler::Off;
    if (name != QLatin1String("off")) {
        const auto it = std::find_if(std::begin(kUpscalers), std::end(kUpscalers),
                                     [&](const UpscalerInfo& info) { return name == QLatin1String(info.name); });
        if (it == std::end(kUpscalers)) {
            qCInfo(lcSession) << "Unknown upscaler:" << name;
            return;
        }
        if (!require(it->capability, "This upscaler"))
            return;
        wanted = it->upscaler;
    }
    if (wanted == m_upscaler)
        return;
    m_upscaler = wanted;
    m_target->setUpscaler(m_upscaler, m_upscaleHeight);
    emit upscalerChanged();
}

void Session::setUpscaleHeight(int height)
{
    if (height != 0 && (height < 480 || height > 4320)) {
        qCInfo(lcSession) << "Upscale height out of range:" << height;
        return;
    }
    if (height == m_upscaleHeight)
        return;
    m_upscaleHeight = height;
    // Only an upscaler that is on needs to hear about it.
    if (m_upscaler != Upscaler::Off)
        m_target->setUpscaler(m_upscaler, m_upscaleHeight);
    emit upscalerChanged();
}

} // namespace cj
