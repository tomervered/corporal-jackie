#pragma once
// Reads quality, size and seeders out of an add-on's free-text stream labels
// ("Torrentio\n4k HDR", "👤 41 💾 15.2 GB"), and orders stream choices.

#include "SourceTypes.h"

namespace cj::ranking {

// 2160, 1440, 1080, 720, 576, 480, or 0 if the text does not say.
int qualityOf(const QString& text);
// "15.2 GB" -> bytes; -1 if no size is given.
qint64 sizeOf(const QString& text);
// "👤 41" or "Seeders: 41" -> 41; -1 if not given.
int seedersOf(const QString& text);

// Fills quality, size and seeders from name and description, where the add-on
// did not give them in structured form.
void fillFromLabels(StreamOption& stream);

// Best first: higher quality, then more seeders, then larger size (a bigger
// file of the same quality usually has a higher bitrate). Unknown values go
// last within their group. The order is stable for equal streams.
void sortBest(QList<StreamOption>& streams);

// A short label for a quality group: "4K", "1080p", "720p", "SD", "Other".
QString qualityLabel(int quality);

} // namespace cj::ranking
