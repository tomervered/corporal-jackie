#pragma once
// What source adapters hand to the browse UI: catalogs, items, episodes and
// stream choices. Every stream choice ends in one PlayableSource.

#include "PlaybackTypes.h"

#include <QList>
#include <QString>
#include <QStringList>

namespace cj {

// One list of items an adapter offers, e.g. "Popular movies" from an add-on
// or "Movies" from local folders.
struct CatalogRef
{
    QString adapter;    // the SourceAdapter's id
    QString provider;   // inside the adapter: the add-on id, or "" for local files
    QString id;         // the catalog's own id
    QString type;       // "movie", "series", ...
    QString name;
    bool searchable = false;
    bool searchOnly = false;  // only answers with a search term (no browse list)
};

struct MetaPreview
{
    QString adapter;
    QString provider;
    QString id;         // e.g. "tt1234567" or "local:/path/to/file.mkv"
    QString type;
    QString name;
    QString poster;     // image URL, may be empty
    QString releaseInfo;
    QString description;
};

// An episode of a series, or the single "video" of a movie.
struct VideoEntry
{
    QString id;         // what streams are requested for, e.g. "tt1234567:1:1"
    QString title;
    int season = 0;     // 0 when not a series episode
    int episode = 0;
    QString released;
};

struct MetaDetail
{
    MetaPreview preview;
    QString background;
    QString runtime;
    QStringList genres;
    QList<VideoEntry> videos;  // empty for a movie: stream the item's own id
};

// One way to play a video: a torrent, a web link or a local file.
struct StreamOption
{
    PlayableSource source;
    QString providerName;  // the add-on's name, or "This computer"
    QString name;          // the add-on's short label, e.g. "Example 4K"
    QString description;   // the add-on's details line(s)
    int quality = 0;       // vertical resolution: 2160, 1080, 720, 480; 0 = unknown
    qint64 sizeBytes = -1;
    int seeders = -1;
    QString bingeGroup;    // same group = same release, for the next episode later
};

// What is being played, from the browse panel: the item and its video (the
// episode, or the movie itself). Watch history is kept by this.
struct PlayContext
{
    MetaPreview item;
    VideoEntry video;
};

} // namespace cj
