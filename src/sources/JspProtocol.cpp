#include "JspProtocol.h"

#include <QJsonArray>
#include <algorithm>
#include <QUrl>
#include <QUrlQuery>

namespace cj::jsp {

namespace {

QStringList stringList(const QJsonValue& v)
{
    QStringList out;
    for (const QJsonValue& e : v.toArray())
        if (e.isString() && !e.toString().isEmpty())
            out << e.toString();
    return out;
}

QString enc(const QString& s)
{
    return QString::fromLatin1(QUrl::toPercentEncoding(s));
}

QString withQuery(QString path, const QList<QPair<QString, QString>>& items)
{
    QStringList parts;
    for (const auto& [k, v] : items)
        if (!v.isEmpty())
            parts << k + QLatin1Char('=') + enc(v);
    return parts.isEmpty() ? path : path + QLatin1Char('?') + parts.join(QLatin1Char('&'));
}

bool goodBase(const QString& s)
{
    return cj::jsp::allowedUrl(QUrl(s));
}

} // namespace

bool allowedUrl(const QUrl& u)
{
    if (!u.isValid() || u.host().isEmpty() || !u.userInfo().isEmpty())
        return false;
    const QString scheme = u.scheme().toLower();
    return scheme == QLatin1String("https")
        || (scheme == QLatin1String("http") && (u.host() == QLatin1String("127.0.0.1") || u.host() == QLatin1String("localhost")));
}

bool isJsp(const QJsonObject& manifest)
{
    return manifest.value(QStringLiteral("protocol")).toInt() == 1;
}

std::optional<stremio::Manifest> parseManifest(const QJsonObject& json, QString* error)
{
    stremio::Manifest m;
    m.id = json.value("id").toString();
    m.name = json.value("name").toString();
    m.version = json.value("version").toString();
    if (m.id.isEmpty() || m.name.isEmpty()) {
        if (error)
            *error = QStringLiteral("not a Jackie source manifest (no id or name)");
        return std::nullopt;
    }
    m.description = json.value("description").toString();
    m.logo = json.value("logo").toString();
    m.types = stringList(json.value("types"));
    m.idPrefixes = stringList(json.value("idPrefixes"));
    for (const QString& name : stringList(json.value("resources"))) {
        stremio::Resource r;
        r.name = name == QLatin1String("streams") ? QStringLiteral("stream") : name;   // the app's own word
        m.resources << r;
    }
    for (const QJsonValue& c : json.value("catalogs").toArray()) {
        const QJsonObject o = c.toObject();
        stremio::Catalog cat;
        cat.type = o.value("type").toString();
        cat.id = o.value("id").toString();
        cat.name = o.value("name").toString();
        if (cat.name.isEmpty())
            cat.name = cat.id;
        cat.searchable = o.value("search").toBool();
        cat.genres = stringList(o.value("genres"));
        cat.years = stringList(o.value("years"));
        if (!cat.type.isEmpty() && !cat.id.isEmpty())
            m.catalogs << cat;
    }
    if (!m.catalogs.isEmpty() && std::none_of(m.resources.begin(), m.resources.end(),
                                              [](const stremio::Resource& r) { return r.name == QLatin1String("catalog"); }))
        m.resources << stremio::Resource{QStringLiteral("catalog"), {}, {}};
    return m;
}

std::optional<stremio::Manifest> parseAnyManifest(const QJsonObject& json, QString* error)
{
    return isJsp(json) ? parseManifest(json, error) : stremio::parseManifest(json, error);
}

QStringList mirrors(const QJsonObject& manifest)
{
    QStringList out;
    for (QString s : stringList(manifest.value("mirrors"))) {
        while (s.endsWith(QLatin1Char('/')))
            s.chop(1);
        if (goodBase(s) && !out.contains(s) && out.size() < 5)
            out << s;
    }
    return out;
}

Envelope parseEnvelope(const QJsonObject& json)
{
    Envelope e;
    if (!json.contains(QStringLiteral("ok")) || json.value("v").toInt() != 1) {
        e.code = QStringLiteral("unavailable");
        e.message = QStringLiteral("not a Jackie answer");
        return e;
    }
    e.ok = json.value("ok").toBool();
    if (e.ok) {
        e.data = json.value("data").toObject();
        e.ttl = qBound(0, json.value("ttl").toInt(60), 3600);
        return e;
    }
    const QJsonObject err = json.value("error").toObject();
    e.code = err.value("code").toString(QStringLiteral("unavailable"));
    e.message = err.value("message").toString();
    e.retryAfter = qBound(0, err.value("retryAfter").toInt(), 3600);
    return e;
}

QString describe(const Envelope& e)
{
    return e.message.isEmpty() ? e.code : e.code + QStringLiteral(": ") + e.message;
}

bool isDefinitive(const Envelope& e)
{
    return e.code == QLatin1String("not_found") || e.code == QLatin1String("bad_request");
}

QString catalogPath(const QString& type, const QString& id, const QString& search, const QString& genre,
                    const QString& year, int skip)
{
    return withQuery(QStringLiteral("/catalog/") + enc(type) + QLatin1Char('/') + enc(id),
                     {{QStringLiteral("q"), search},
                      {QStringLiteral("genre"), genre},
                      {QStringLiteral("year"), year},
                      {QStringLiteral("skip"), skip > 0 ? QString::number(skip) : QString()}});
}

QString metaPath(const QString& type, const QString& id)
{
    return QStringLiteral("/meta/") + enc(type) + QLatin1Char('/') + enc(id);
}

QString streamsPath(const QString& type, const QString& videoId)
{
    return QStringLiteral("/streams/") + enc(type) + QLatin1Char('/') + enc(videoId);
}

QString subtitlesPath(const QString& type, const QString& videoId)
{
    return QStringLiteral("/subtitles/") + enc(type) + QLatin1Char('/') + enc(videoId);
}

namespace {

QJsonObject withReleaseInfo(QJsonObject o)
{
    if (o.contains("year") && !o.contains("releaseInfo"))
        o.insert("releaseInfo", QString::number(o.value("year").toInt()));
    return o;
}

} // namespace

QJsonObject catalogAsStremio(const QJsonObject& data)
{
    QJsonArray metas;
    for (const QJsonValue& v : data.value("items").toArray())
        metas.append(withReleaseInfo(v.toObject()));
    return QJsonObject{{"metas", metas}};
}

QJsonObject metaAsStremio(const QJsonObject& data)
{
    return QJsonObject{{"meta", withReleaseInfo(data)}};
}

QJsonObject streamsAsStremio(const QJsonObject& data)
{
    QJsonArray out;
    for (const QJsonValue& v : data.value("streams").toArray()) {
        const QJsonObject s = v.toObject();
        QJsonObject hints;
        if (s.contains("size"))
            hints.insert("videoSize", s.value("size"));
        if (!s.value("group").toString().isEmpty())
            hints.insert("bingeGroup", s.value("group"));
        // Typed fields beat what a label says (StremioProtocol reads these two).
        if (s.contains("quality"))
            hints.insert("cjQuality", s.value("quality"));
        if (s.contains("seeders"))
            hints.insert("cjSeeders", s.value("seeders"));
        QJsonObject o{{"name", s.value("name")}, {"description", s.value("detail")}};
        const QString kind = s.value("kind").toString();
        if (kind == QLatin1String("torrent")) {
            o.insert("infoHash", s.value("infoHash"));
            if (s.contains("fileIdx"))
                o.insert("fileIdx", s.value("fileIdx"));
            QJsonArray sources;
            for (const QString& t : stringList(s.value("trackers")))
                sources.append(QStringLiteral("tracker:") + t);
            o.insert("sources", sources);
        } else if (kind == QLatin1String("http")) {
            o.insert("url", s.value("url"));
            if (const QJsonObject h = s.value("headers").toObject(); !h.isEmpty())
                hints.insert("proxyHeaders", QJsonObject{{"request", h}});
        } else {
            continue;   // a kind this version does not know: only that item is dropped
        }
        if (!hints.isEmpty())
            o.insert("behaviorHints", hints);
        out.append(o);
    }
    return QJsonObject{{"streams", out}};
}

QJsonObject subtitlesAsStremio(const QJsonObject& data)
{
    return QJsonObject{{"subtitles", data.value("subtitles")}};
}

} // namespace cj::jsp
