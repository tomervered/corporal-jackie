#include "Session.h"

#include <QDir>
#include <QHash>
#include <QLocale>
#include <QRegularExpression>
#include <QStandardPaths>

namespace cj {

namespace {
// A codec as people know it ("subrip" is SRT).
QString codecName(const QString& codec)
{
    static const QHash<QString, QString> names{
        {QStringLiteral("subrip"), QStringLiteral("SRT")}, {QStringLiteral("ass"), QStringLiteral("ASS")},
        {QStringLiteral("ssa"), QStringLiteral("SSA")}, {QStringLiteral("webvtt"), QStringLiteral("WebVTT")},
        {QStringLiteral("hdmv_pgs_subtitle"), QStringLiteral("PGS")}, {QStringLiteral("dvd_subtitle"), QStringLiteral("VobSub")},
        {QStringLiteral("mov_text"), QStringLiteral("Text")}, {QStringLiteral("eac3"), QStringLiteral("E-AC-3")},
        {QStringLiteral("ac3"), QStringLiteral("AC-3")}, {QStringLiteral("truehd"), QStringLiteral("TrueHD")},
        {QStringLiteral("opus"), QStringLiteral("Opus")}, {QStringLiteral("vorbis"), QStringLiteral("Vorbis")}};
    return names.value(codec.toLower(), codec.toUpper());
}

} // namespace

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
        // For the track lists: the language as words ("English"), with the
        // track's own title when it says more ("English · SDH"), and where
        // the track comes from underneath.
        const QLocale locale(t.language);
        const bool known = !t.language.isEmpty() && locale.language() != QLocale::C
                           && t.language != QLatin1String("und");
        const QString languageName = known ? QLocale::languageToString(locale.language()) : QString();
        QString name = languageName;
        if (!t.title.isEmpty() && t.title.compare(languageName, Qt::CaseInsensitive) != 0
            && t.title.compare(t.language, Qt::CaseInsensitive) != 0)
            name = name.isEmpty() ? t.title : name + QStringLiteral(" · ") + t.title;
        if (name.isEmpty())
            name = tr("Track %1").arg(t.id);
        QStringList detail{t.external ? tr("From a file") : tr("Embedded")};
        if (!t.codec.isEmpty())
            detail << codecName(t.codec);
        if (t.isDefault)
            detail << tr("Default");
        list.append(QVariantMap{{QStringLiteral("id"), t.id},
                                {QStringLiteral("label"), label},
                                {QStringLiteral("name"), name},
                                {QStringLiteral("detail"), detail.join(QStringLiteral(" · "))},
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

void Session::addExternalAudio(const QUrl& url)
{
    if (hasMedia() && url.isLocalFile())
        m_target->addExternalAudio(url);
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

} // namespace cj
