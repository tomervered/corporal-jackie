#include "PlaybackSession.h"
#include "Logging.h"

#include <algorithm>

namespace cj {

namespace {
// The shapes offered for Aspect ratio and Crop (empty: automatic / none),
// widest last.
const QStringList kPictureShapes = {QString(), QStringLiteral("16:9"), QStringLiteral("4:3"),
                                   QStringLiteral("1:1"), QStringLiteral("1.85:1"), QStringLiteral("2:1"),
                                   QStringLiteral("21:9"), QStringLiteral("2.35:1"), QStringLiteral("2.39:1")};

QString nextShape(const QStringList& list, const QString& current)
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
    for (const QString& name : kPictureShapes)
        if (!name.isEmpty() && qAbs(ratioOf(name) - ratio) < 0.01)
            return name;
    return QString::number(ratio, 'f', 2);
}

} // namespace

void PlaybackSession::cycleAspectRatio()
{
    if (!hasMedia() || !require(Capability::AspectRatio, "Aspect ratio"))
        return;
    m_aspect = nextShape(kPictureShapes, m_aspect);
    m_target->setAspectRatio(m_aspect);
    emit aspectRatioChanged();
}

void PlaybackSession::setAspectRatio(const QString& ratio)
{
    if (!hasMedia() || !kPictureShapes.contains(ratio) || !require(Capability::AspectRatio, "Aspect ratio"))
        return;
    m_aspect = ratio;
    m_target->setAspectRatio(m_aspect);
    emit aspectRatioChanged();
}

void PlaybackSession::setCrop(const QString& ratio)
{
    if (!hasMedia() || !kPictureShapes.contains(ratio) || !require(Capability::Crop, "Crop"))
        return;
    m_crop = ratio;
    m_target->setCrop(m_crop);
    emit cropChanged();
}

QStringList PlaybackSession::aspectRatios() const { return kPictureShapes; }
QStringList PlaybackSession::crops() const { return kPictureShapes; }

void PlaybackSession::cycleCrop()
{
    if (!hasMedia() || !require(Capability::Crop, "Crop"))
        return;
    m_crop = nextShape(kPictureShapes, m_crop);
    m_target->setCrop(m_crop);
    emit cropChanged();
}

void PlaybackSession::setEqualizer(const QString& name, int value)
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

void PlaybackSession::resetEqualizer()
{
    if (!require(Capability::VideoEqualizer, "Video equalizer") || m_eq == VideoEqualizer{})
        return;
    m_eq = {};
    m_target->setVideoEqualizer(m_eq);
    emit equalizerChanged();
}

void PlaybackSession::setFullScreen(bool on)
{
    if (on == m_fullScreen)
        return;
    m_fullScreen = on;
    m_target->setFullScreen(on);
    emit fullScreenChanged();
}

bool PlaybackSession::keyEvent(int key, int modifiers, const QString& text, bool pressed)
{
    if (!m_target->supports(Capability::NativeKeys))
        return false;
    return m_target->keyEvent(key, Qt::KeyboardModifiers(modifiers), text, pressed);
}

// The target changed a setting by itself (mpv's own keys did): take its word
// for it, without sending it back.
void PlaybackSession::applyTargetSetting(const QString& name, const QVariant& value)
{
    const auto update = [](auto& field, auto v, auto signal, PlaybackSession* self) {
        if (field == v)
            return;
        field = v;
        emit (self->*signal)();
    };
    if (name == QLatin1String("speed"))
        update(m_speed, value.toDouble(), &PlaybackSession::speedChanged, this);
    else if (name == QLatin1String("subtitleDelay"))
        update(m_subtitleDelay, value.toDouble(), &PlaybackSession::subtitleDelayChanged, this);
    else if (name == QLatin1String("audioDelay"))
        update(m_audioDelay, value.toDouble(), &PlaybackSession::audioDelayChanged, this);
    else if (name == QLatin1String("subtitleScale"))
        update(m_subtitleScale, value.toDouble(), &PlaybackSession::subtitleScaleChanged, this);
    else if (name == QLatin1String("abLoopA"))
        update(m_abA, value.toDouble() < 0 ? -1.0 : value.toDouble(), &PlaybackSession::abLoopChanged, this);
    else if (name == QLatin1String("abLoopB"))
        update(m_abB, value.toDouble() < 0 ? -1.0 : value.toDouble(), &PlaybackSession::abLoopChanged, this);
    else if (name == QLatin1String("aspectRatio"))
        update(m_aspect, aspectName(value.toDouble()), &PlaybackSession::aspectRatioChanged, this);
    else if (name == QLatin1String("brightness"))
        update(m_eq.brightness, value.toInt(), &PlaybackSession::equalizerChanged, this);
    else if (name == QLatin1String("contrast"))
        update(m_eq.contrast, value.toInt(), &PlaybackSession::equalizerChanged, this);
    else if (name == QLatin1String("saturation"))
        update(m_eq.saturation, value.toInt(), &PlaybackSession::equalizerChanged, this);
    else if (name == QLatin1String("gamma"))
        update(m_eq.gamma, value.toInt(), &PlaybackSession::equalizerChanged, this);
    else if (name == QLatin1String("hue"))
        update(m_eq.hue, value.toInt(), &PlaybackSession::equalizerChanged, this);
    else if (name == QLatin1String("hardwareDecoding"))
        update(m_hardwareDecoding, value.toBool(), &PlaybackSession::hardwareDecodingChanged, this);
    else if (name == QLatin1String("fullScreen"))
        update(m_fullScreen, value.toBool(), &PlaybackSession::fullScreenChanged, this);
    else if (name == QLatin1String("upscaler")) {
        const QString wanted = value.toString();
        Upscaler u = Upscaler::Off;
        for (const Upscaler candidate : {Upscaler::Sgsr, Upscaler::Cas, Upscaler::Fsr, Upscaler::Anime4k, Upscaler::NvidiaVsr, Upscaler::IntelVsr, Upscaler::MetalFx})
            if (upscalerName(candidate) == wanted)
                u = candidate;
        update(m_upscaler, u, &PlaybackSession::upscalerChanged, this);
    }
    else
        qCInfo(lcSession) << "Unknown setting from the target:" << name;
}

void PlaybackSession::setHardwareDecoding(bool enabled)
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

QString PlaybackSession::upscalerName(Upscaler u)
{
    for (const UpscalerInfo& info : kUpscalers)
        if (info.upscaler == u)
            return QString::fromLatin1(info.name);
    return QStringLiteral("off");
}

QStringList PlaybackSession::upscalers() const
{
    QStringList names{QStringLiteral("off")};
    for (const UpscalerInfo& info : kUpscalers)
        if (m_target->supports(info.capability))
            names << QString::fromLatin1(info.name);
    return names;
}

void PlaybackSession::setUpscaler(const QString& name)
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

void PlaybackSession::setUpscaleHeight(int height)
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

void PlaybackSession::setIntelVsrCopyback(bool on)
{
    if (on == m_intelVsrCopyback)
        return;
    m_intelVsrCopyback = on;
    m_target->setIntelVsrCopyback(on);
    emit intelVsrCopybackChanged();
}

} // namespace cj
