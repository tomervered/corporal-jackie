#include "LocalNames.h"

#include "StreamRanking.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>

namespace cj::local {
namespace {

const QStringList kVideoExtensions = {"mkv", "mp4", "m4v", "avi", "mov", "webm", "ts", "m2ts", "wmv", "flv", "mpg", "mpeg", "ogv"};
const QStringList kSubtitleExtensions = {"srt", "ass", "ssa", "vtt", "sub", "sup"};
constexpr int kMaxSubtitles = 50;

// Where the title ends in a release name: quality, source, codec and the like.
const QRegularExpression kJunk(
    QStringLiteral(R"(\b(2160p|1440p|1080p|1080i|720p|576p|480p|4k|uhd|hdr10?|dv|bluray|blu-ray|bdrip|brrip|)"
                   R"(web-?dl|web-?rip|webrip|web|hdtv|dvdrip|dvd|hdrip|remux|x264|x265|h\.?264|h\.?265|hevc|avc|)"
                   R"(xvid|aac|ac3|dts|ddp?5\.1|atmos|proper|repack|extended|unrated|remastered|imax|multi|complete)\b)"),
    QRegularExpression::CaseInsensitiveOption);
const QRegularExpression kSxxEyy(QStringLiteral(R"(\bS(\d{1,2})[ ._-]?E(\d{1,3})(?:-?E\d{1,3})?\b)"),
                                 QRegularExpression::CaseInsensitiveOption);
const QRegularExpression kNxNN(QStringLiteral(R"(\b(\d{1,2})x(\d{2,3})\b)"), QRegularExpression::CaseInsensitiveOption);
const QRegularExpression kSeasonEpisodeWords(QStringLiteral(R"(\bSeason[ ._-]*(\d{1,2})[ ._-]*Episode[ ._-]*(\d{1,3})\b)"),
                                             QRegularExpression::CaseInsensitiveOption);
const QRegularExpression kYear(QStringLiteral(R"([\(\[]?\b(19[0-9]{2}|20[0-9]{2})\b[\)\]]?)"));
const QRegularExpression kSeasonFolder(QStringLiteral(R"(^(?:season|series|s)[ ._-]*(\d{1,2})\b)"),
                                       QRegularExpression::CaseInsensitiveOption);
const QRegularExpression kSpecialsFolder(QStringLiteral(R"(^(specials|extras)$)"), QRegularExpression::CaseInsensitiveOption);
// "03 - Title", "E03", "Episode 3", "Ep.3"
const QRegularExpression kBareEpisode(QStringLiteral(R"(^(?:e|ep|episode)?[ ._-]*(\d{1,3})\b(?:[ ._-]+(.*))?$)"),
                                      QRegularExpression::CaseInsensitiveOption);
const QRegularExpression kLeadingTag(QStringLiteral(R"(^\s*[\[\(][^\]\)]*[\]\)]\s*)"));

QString spaced(QString s)
{
    // Dots and underscores separate words in release names.
    s.replace(QLatin1Char('_'), QLatin1Char(' '));
    s.replace(QLatin1Char('.'), QLatin1Char(' '));
    return s.simplified();
}

QString cleanTitle(QString s)
{
    s = s.simplified();
    while (true) {
        const QString before = s;
        s.remove(kLeadingTag);   // "[Group] Name"
        s = s.trimmed();
        while (!s.isEmpty() && QStringLiteral("-–:,([").contains(s.back()))
            s.chop(1);
        s = s.trimmed();
        while (!s.isEmpty() && QStringLiteral("-–:,)]").contains(s.front()))
            s.remove(0, 1);
        s = s.trimmed();
        if (s == before)
            break;
    }
    return s;
}

// A name without episode markers: the title runs to the year or the first
// release word.
ParsedName parseTitle(const QString& text)
{
    ParsedName p;
    QString s = spaced(text);
    s.remove(kLeadingTag);
    int cut = s.size();
    // The last year that leaves a title in front ("2001 A Space Odyssey 1968").
    auto years = kYear.globalMatch(s);
    while (years.hasNext()) {
        const auto m = years.next();
        if (m.capturedStart() > 0) {
            p.year = m.captured(1).toInt();
            cut = m.capturedStart();
        }
    }
    if (const auto junk = kJunk.match(s); junk.hasMatch() && junk.capturedStart() > 0)
        cut = std::min<int>(cut, int(junk.capturedStart()));
    p.title = cleanTitle(s.left(cut));
    return p;
}

QString titleFromFolder(const QString& name)
{
    return parseTitle(name).title;
}

} // namespace

bool isVideoFile(const QString& fileName)
{
    return kVideoExtensions.contains(QFileInfo(fileName).suffix().toLower());
}

bool isSubtitleFile(const QString& fileName)
{
    return kSubtitleExtensions.contains(QFileInfo(fileName).suffix().toLower());
}

ParsedName parseVideoPath(const QString& path)
{
    const QFileInfo info(path);
    const QString base = info.completeBaseName();
    ParsedName p;
    p.quality = ranking::qualityOf(base);

    QRegularExpressionMatch m = kSxxEyy.match(base);
    if (!m.hasMatch())
        m = kSeasonEpisodeWords.match(base);
    if (!m.hasMatch()) {
        m = kNxNN.match(base);
        // "1920x1080" is not an episode; kNxNN allows two-digit seasons only.
        if (m.hasMatch() && m.captured(2).size() == 3 && m.captured(1).size() == 2 && m.captured(1).toInt() > 30)
            m = {};
    }

    const QDir parent = info.dir();
    if (m.hasMatch()) {
        p.season = m.captured(1).toInt();
        p.episode = m.captured(2).toInt();
        ParsedName title = parseTitle(base.left(int(m.capturedStart())));
        p.title = title.title;
        p.year = title.year;
        p.episodeTitle = cleanTitle(parseTitle(base.mid(int(m.capturedEnd()))).title);
    } else if (const auto season = kSeasonFolder.match(parent.dirName());
               season.hasMatch() || kSpecialsFolder.match(parent.dirName()).hasMatch()) {
        // "Show/Season 2/03 - Title.mkv": the episode number is the file's.
        const auto bare = kBareEpisode.match(spaced(base));
        if (bare.hasMatch()) {
            p.season = season.hasMatch() ? season.captured(1).toInt() : 0;
            p.episode = bare.captured(1).toInt();
            p.episodeTitle = cleanTitle(parseTitle(bare.captured(2)).title);
        } else {
            const ParsedName t = parseTitle(base);
            p.title = t.title;
            p.year = t.year;
        }
    } else {
        const ParsedName t = parseTitle(base);
        p.title = t.title;
        p.year = t.year;
    }

    if (p.isEpisode() && p.title.isEmpty()) {
        // The show's name is in the folders: skip "Season 2" and "Specials".
        QString dir = info.path();
        for (int depth = 0; depth < 3 && p.title.isEmpty() && !dir.isEmpty(); ++depth) {
            const QString name = QFileInfo(dir).fileName();
            if (!kSeasonFolder.match(name).hasMatch() && !kSpecialsFolder.match(name).hasMatch())
                p.title = titleFromFolder(name);
            const QString up = QFileInfo(dir).path();
            if (up == dir)
                break;
            dir = up;
        }
    }
    if (p.title.isEmpty())
        p.title = base;  // nothing better: show the file name
    return p;
}

QList<SubtitleSource> findSubtitlesFor(const QString& videoPath)
{
    const QFileInfo video(videoPath);
    const QString stem = video.completeBaseName();
    const QDir dir = video.dir();
    QList<SubtitleSource> out;

    auto add = [&](const QFileInfo& f, const QString& rest) {
        if (out.size() >= kMaxSubtitles)
            return;
        SubtitleSource s;
        s.url = QUrl::fromLocalFile(f.absoluteFilePath());
        // "en", "English.forced", "2_English" -> language "en"/"English", label the rest.
        QString tags = rest;
        tags.remove(QRegularExpression(QStringLiteral(R"(^[ ._-]+)")));
        tags.remove(QRegularExpression(QStringLiteral(R"(^\d+[ _-]+)")));
        const QStringList parts = tags.split(QRegularExpression(QStringLiteral("[ ._-]")), Qt::SkipEmptyParts);
        s.language = parts.value(0);
        s.label = tags.isEmpty() ? f.fileName() : tags;
        out << s;
    };

    // Next to the video: "Movie.srt", "Movie.en.srt", "Movie.English.forced.srt".
    for (const QFileInfo& f : dir.entryInfoList(QDir::Files | QDir::Readable, QDir::Name | QDir::IgnoreCase)) {
        if (!isSubtitleFile(f.fileName()) || !f.completeBaseName().startsWith(stem, Qt::CaseInsensitive))
            continue;
        add(f, f.completeBaseName().mid(stem.size()));
    }

    // A Subs/ folder: "Subs/<video name>/*.srt" for a season pack, or the
    // subtitle files directly inside when the folder holds a single video.
    for (const QFileInfo& sub : dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QString name = sub.fileName().toLower();
        if (name != QLatin1String("subs") && name != QLatin1String("subtitles") && name != QLatin1String("sub"))
            continue;
        const QDir subs(sub.absoluteFilePath());
        const QDir own(subs.filePath(stem));
        if (own.exists()) {
            for (const QFileInfo& f : own.entryInfoList(QDir::Files | QDir::Readable, QDir::Name | QDir::IgnoreCase))
                if (isSubtitleFile(f.fileName()))
                    add(f, f.completeBaseName());
            continue;
        }
        int videos = 0;
        for (const QFileInfo& f : dir.entryInfoList(QDir::Files))
            videos += isVideoFile(f.fileName()) ? 1 : 0;
        for (const QFileInfo& f : subs.entryInfoList(QDir::Files | QDir::Readable, QDir::Name | QDir::IgnoreCase)) {
            if (!isSubtitleFile(f.fileName()))
                continue;
            if (f.completeBaseName().startsWith(stem, Qt::CaseInsensitive))
                add(f, f.completeBaseName().mid(stem.size()));
            else if (videos <= 1)
                add(f, f.completeBaseName());
        }
    }
    return out;
}

} // namespace cj::local
