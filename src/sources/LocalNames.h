#pragma once
// Reads what a video file is from its name and folders, the way people and
// release groups name them:
//   "The.Matrix.1999.1080p.BluRay.x264.mkv"        -> The Matrix (1999)
//   "Show.Name.S01E02.Episode.Title.720p.mkv"      -> Show Name, 1x2
//   "Show Name/Season 2/03 - Title.mkv"            -> Show Name, 2x3
// And finds subtitle files that belong to a video: next to it with the same
// name start ("Movie.en.srt"), or in a Subs/ or Subtitles/ folder beside it.

#include "PlaybackTypes.h"

#include <QString>

namespace cj::local {

struct ParsedName
{
    QString title;      // the movie's or the show's name
    int year = 0;
    int season = 0;     // 0 = not known (a movie, or a show without seasons)
    int episode = 0;    // 0 = not an episode
    QString episodeTitle;
    int quality = 0;    // 2160, 1080, 720... or 0

    bool isEpisode() const { return episode > 0; }
};

ParsedName parseVideoPath(const QString& path);

bool isVideoFile(const QString& fileName);
bool isSubtitleFile(const QString& fileName);

QList<SubtitleSource> findSubtitlesFor(const QString& videoPath);

} // namespace cj::local
