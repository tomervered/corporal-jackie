#include "LibraryBackupFormat.h"
#include "LinkRules.h"

#include <cmath>

namespace cj {
namespace backup {

QJsonObject writeWatchHistoryEntry(const HistoryEntry& entry)
{
    return {{"key", entry.key}, {"adapter", entry.adapter}, {"provider", entry.provider}, {"itemId", entry.itemId},
            {"itemType", entry.itemType}, {"itemName", entry.itemName}, {"poster", entry.poster}, {"videoId", entry.videoId},
            {"videoTitle", entry.videoTitle}, {"season", entry.season}, {"episode", entry.episode},
            {"source", Storage::sourceToJson(entry.source)}, {"position", entry.position}, {"duration", entry.duration},
            {"updatedAt", double(entry.updatedAt)}, {"finished", entry.finished}};
}

// A time in ms since 1970 from an export. Converting a double outside
// qint64's range is undefined, so anything not finite, negative or past the
// year 9999 comes back as -1, which import refuses.
static qint64 readTimestamp(const QJsonValue& v)
{
    constexpr double kLatest = 253402300800000.0;  // 10000-01-01
    const double ms = v.toDouble();
    if (!std::isfinite(ms) || ms < 0 || ms > kLatest)
        return -1;
    return qint64(ms);
}

HistoryEntry readWatchHistoryEntry(const QJsonObject& json)
{
    HistoryEntry entry;
    entry.key = json.value("key").toString();
    entry.adapter = json.value("adapter").toString();
    entry.provider = json.value("provider").toString();
    entry.itemId = json.value("itemId").toString();
    entry.itemType = json.value("itemType").toString();
    entry.itemName = json.value("itemName").toString();
    entry.poster = links::imageLinkOrEmpty(json.value("poster").toString());  // web images only: see LinkRules.h
    entry.videoId = json.value("videoId").toString();
    entry.videoTitle = json.value("videoTitle").toString();
    entry.season = json.value("season").toInt();
    entry.episode = json.value("episode").toInt();
    entry.source = Storage::sourceFromJson(json.value("source").toObject());
    entry.position = json.value("position").toDouble();
    entry.duration = json.value("duration").toDouble();
    entry.updatedAt = readTimestamp(json.value("updatedAt"));
    entry.finished = json.value("finished").toBool();
    return entry;
}

QJsonObject writeLibraryItem(const LibraryItem& item)
{
    return {{"adapter", item.adapter}, {"provider", item.provider}, {"itemId", item.itemId}, {"type", item.type},
            {"name", item.name}, {"poster", item.poster}, {"releaseInfo", item.releaseInfo}, {"addedAt", double(item.addedAt)}};
}

LibraryItem readLibraryItem(const QJsonObject& json)
{
    LibraryItem item;
    item.adapter = json.value("adapter").toString();
    item.provider = json.value("provider").toString();
    item.itemId = json.value("itemId").toString();
    item.type = json.value("type").toString();
    item.name = json.value("name").toString();
    item.poster = links::imageLinkOrEmpty(json.value("poster").toString());
    item.releaseInfo = json.value("releaseInfo").toString();
    item.addedAt = readTimestamp(json.value("addedAt"));
    return item;
}

} // namespace backup

QJsonObject Storage::sourceToJson(const PlayableSource& source)
{
    QJsonObject headers;
    for (auto it = source.headers.begin(); it != source.headers.end(); ++it)
        headers.insert(it.key(), it.value());
    QJsonArray subtitles;
    for (const SubtitleSource& subtitle : source.subtitles)
        subtitles.append(QJsonObject{{"url", subtitle.url.toString()}, {"language", subtitle.language}, {"label", subtitle.label}});
    const char* kinds[] = {"file", "http", "torrent"};
    QJsonObject json{{"url", source.url.toString()}, {"title", source.title}, {"kind", kinds[int(source.kind)]}};
    if (!source.origin.isEmpty())
        json.insert("origin", source.origin);
    if (!headers.isEmpty())
        json.insert("headers", headers);
    if (!subtitles.isEmpty())
        json.insert("subtitles", subtitles);
    if (!source.adapter.isEmpty())
        json.insert("adapter", source.adapter);
    return json;
}

PlayableSource Storage::sourceFromJson(const QJsonObject& json)
{
    PlayableSource source;
    source.url = QUrl(json.value("url").toString());
    source.title = json.value("title").toString();
    const QString kind = json.value("kind").toString();
    source.kind = kind == QLatin1String("torrent") ? PlayableSource::Kind::Torrent
           : kind == QLatin1String("http")    ? PlayableSource::Kind::Http
                                              : PlayableSource::Kind::LocalFile;
    source.origin = json.value("origin").toString();
    const QJsonObject headers = json.value("headers").toObject();
    for (auto it = headers.begin(); it != headers.end(); ++it)
        source.headers.insert(it.key(), it.value().toString());
    for (const QJsonValue& value : json.value("subtitles").toArray()) {
        const QJsonObject subtitle = value.toObject();
        source.subtitles.append({QUrl(subtitle.value("url").toString()), subtitle.value("language").toString(),
                            subtitle.value("label").toString()});
    }
    source.adapter = json.value("adapter").toString();
    return source;
}

} // namespace cj
