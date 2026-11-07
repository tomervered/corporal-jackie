#include "StremioProtocol.h"

#include "StreamRanking.h"

#include <QJsonArray>
#include <QUrlQuery>

#include <algorithm>

namespace cj::stremio {
namespace {

QStringList stringList(const QJsonValue& v)
{
    QStringList out;
    for (const QJsonValue& item : v.toArray())
        if (item.isString())
            out << item.toString();
    return out;
}

// encodeURIComponent, as Stremio clients use for ids and extra values.
QString encodeComponent(const QString& s)
{
    return QString::fromLatin1(QUrl::toPercentEncoding(s, "-_.!~*'()"));
}

MetaPreview previewOf(const QJsonObject& o)
{
    MetaPreview m;
    m.id = o.value("id").toString();
    m.type = o.value("type").toString();
    m.name = o.value("name").toString();
    m.poster = o.value("poster").toString();
    m.releaseInfo = o.value("releaseInfo").toVariant().toString();
    if (m.releaseInfo.isEmpty())
        m.releaseInfo = o.value("year").toVariant().toString();
    m.description = o.value("description").toString();
    return m;
}

} // namespace

bool Manifest::serves(const QString& resource, const QString& type, const QString& id) const
{
    for (const Resource& r : resources) {
        if (r.name != resource)
            continue;
        const QStringList& ts = r.types.isEmpty() ? types : r.types;
        if (!ts.isEmpty() && !ts.contains(type))
            continue;
        const QStringList& prefixes = r.idPrefixes.isEmpty() ? idPrefixes : r.idPrefixes;
        if (prefixes.isEmpty())
            return true;
        for (const QString& p : prefixes)
            if (id.startsWith(p))
                return true;
    }
    return false;
}

std::optional<Manifest> parseManifest(const QJsonObject& json, QString* error)
{
    Manifest m;
    m.id = json.value("id").toString();
    m.name = json.value("name").toString();
    m.version = json.value("version").toString();
    if (m.id.isEmpty() || m.name.isEmpty()) {
        if (error)
            *error = QStringLiteral("not a Stremio add-on manifest (no id or name)");
        return std::nullopt;
    }
    m.description = json.value("description").toString();
    m.logo = json.value("logo").toString();
    m.types = stringList(json.value("types"));
    m.idPrefixes = stringList(json.value("idPrefixes"));
    for (const QJsonValue& r : json.value("resources").toArray()) {
        Resource res;
        if (r.isString()) {
            res.name = r.toString();
        } else {
            const QJsonObject o = r.toObject();
            res.name = o.value("name").toString();
            res.types = stringList(o.value("types"));
            res.idPrefixes = stringList(o.value("idPrefixes"));
        }
        if (!res.name.isEmpty())
            m.resources << res;
    }
    for (const QJsonValue& c : json.value("catalogs").toArray()) {
        const QJsonObject o = c.toObject();
        Catalog cat;
        cat.type = o.value("type").toString();
        cat.id = o.value("id").toString();
        cat.name = o.value("name").toString();
        if (cat.name.isEmpty())
            cat.name = cat.id;
        // Current form: "extra": [{"name": "search", "isRequired": true}].
        for (const QJsonValue& e : o.value("extra").toArray()) {
            const QJsonObject eo = e.toObject();
            if (eo.value("name").toString() == QLatin1String("search")) {
                cat.searchable = true;
                cat.searchRequired = eo.value("isRequired").toBool();
            }
        }
        // Older form: "extraSupported" / "extraRequired" lists of names.
        if (stringList(o.value("extraSupported")).contains(QStringLiteral("search")))
            cat.searchable = true;
        if (stringList(o.value("extraRequired")).contains(QStringLiteral("search")))
            cat.searchRequired = true;
        if (!cat.type.isEmpty() && !cat.id.isEmpty())
            m.catalogs << cat;
    }
    // Catalogs imply the catalog resource, even if an add-on forgot to list it.
    if (!m.catalogs.isEmpty() && std::none_of(m.resources.begin(), m.resources.end(),
                                              [](const Resource& r) { return r.name == "catalog"; }))
        m.resources << Resource{QStringLiteral("catalog"), {}, {}};
    m.configurationRequired = json.value("behaviorHints").toObject().value("configurationRequired").toBool();
    return m;
}

QUrl normalizeManifestUrl(const QString& text)
{
    QString t = text.trimmed();
    if (t.startsWith(QLatin1String("stremio://"), Qt::CaseInsensitive))
        t = QStringLiteral("https://") + t.mid(10);
    QUrl url(t);
    const QString scheme = url.scheme().toLower();
    if (!url.isValid() || (scheme != QLatin1String("https") && scheme != QLatin1String("http")))
        return {};
    if (!url.path().endsWith(QLatin1String("/manifest.json")))
        return {};
    return url;
}

QString baseOf(const QUrl& manifestUrl)
{
    QString s = manifestUrl.toString(QUrl::FullyEncoded | QUrl::RemoveQuery | QUrl::RemoveFragment);
    s.chop(int(qstrlen("/manifest.json")));
    return s;
}

QUrl resourceUrl(const QString& base, const QString& resource, const QString& type, const QString& id,
                 const QList<QPair<QString, QString>>& extras)
{
    QString path = base + QLatin1Char('/') + resource + QLatin1Char('/') + encodeComponent(type) + QLatin1Char('/')
                 + encodeComponent(id);
    if (!extras.isEmpty()) {
        QStringList parts;
        for (const auto& [name, value] : extras)
            parts << encodeComponent(name) + QLatin1Char('=') + encodeComponent(value);
        path += QLatin1Char('/') + parts.join(QLatin1Char('&'));
    }
    return QUrl::fromEncoded(QString(path + QStringLiteral(".json")).toUtf8());
}

QList<MetaPreview> parseMetas(const QJsonObject& json)
{
    QList<MetaPreview> out;
    for (const QJsonValue& v : json.value("metas").toArray()) {
        MetaPreview m = previewOf(v.toObject());
        if (!m.id.isEmpty() && !m.name.isEmpty())
            out << m;
    }
    return out;
}

std::optional<MetaDetail> parseMeta(const QJsonObject& json)
{
    const QJsonObject o = json.value("meta").toObject();
    MetaDetail d;
    d.preview = previewOf(o);
    if (d.preview.id.isEmpty())
        return std::nullopt;
    d.background = o.value("background").toString();
    d.runtime = o.value("runtime").toString();
    d.genres = stringList(o.value("genres"));
    for (const QJsonValue& v : o.value("videos").toArray()) {
        const QJsonObject vo = v.toObject();
        VideoEntry e;
        e.id = vo.value("id").toString();
        e.title = vo.value("title").toString();
        if (e.title.isEmpty())
            e.title = vo.value("name").toString();
        e.season = vo.value("season").toInt();
        e.episode = vo.value("episode").toInt(vo.value("number").toInt());
        e.released = vo.value("released").toString().left(10);
        if (!e.id.isEmpty())
            d.videos << e;
    }
    // Episodes in order; specials (season 0) last.
    std::stable_sort(d.videos.begin(), d.videos.end(), [](const VideoEntry& a, const VideoEntry& b) {
        const int sa = a.season == 0 ? 1 << 20 : a.season, sb = b.season == 0 ? 1 << 20 : b.season;
        return sa != sb ? sa < sb : a.episode < b.episode;
    });
    return d;
}

QList<StreamOption> parseStreams(const QJsonObject& json, const QString& addonId, const QString& addonName)
{
    QList<StreamOption> out;
    for (const QJsonValue& v : json.value("streams").toArray()) {
        const QJsonObject o = v.toObject();
        const QJsonObject hints = o.value("behaviorHints").toObject();
        StreamOption s;
        s.providerName = addonName;
        s.name = o.value("name").toString();
        s.description = o.value("description").toString();
        if (s.description.isEmpty())
            s.description = o.value("title").toString();  // the older field
        s.bingeGroup = hints.value("bingeGroup").toString();
        const QString filename = hints.value("filename").toString();
        if (hints.contains("videoSize"))
            s.sizeBytes = qint64(hints.value("videoSize").toDouble());
        s.source.adapter = QStringLiteral("stremio:") + addonId;

        const QString infoHash = o.value("infoHash").toString();
        const QString url = o.value("url").toString();
        if (!infoHash.isEmpty()) {
            QUrlQuery q;
            q.addQueryItem(QStringLiteral("xt"), QStringLiteral("urn:btih:") + infoHash.toLower());
            const QString dn = filename.isEmpty() ? s.description.section(QLatin1Char('\n'), 0, 0) : filename;
            if (!dn.isEmpty())
                q.addQueryItem(QStringLiteral("dn"), QString::fromLatin1(QUrl::toPercentEncoding(dn)));
            for (const QString& src : stringList(o.value("sources")))
                if (src.startsWith(QLatin1String("tracker:")))
                    q.addQueryItem(QStringLiteral("tr"), QString::fromLatin1(QUrl::toPercentEncoding(src.mid(8))));
            if (o.contains("fileIdx") && !o.value("fileIdx").isNull())
                q.addQueryItem(QStringLiteral("so"), QString::number(o.value("fileIdx").toInt()));
            s.source.url = QUrl(QStringLiteral("magnet:?") + q.toString(QUrl::FullyEncoded));
            s.source.kind = PlayableSource::Kind::Torrent;
        } else if (url.startsWith(QLatin1String("http://")) || url.startsWith(QLatin1String("https://"))) {
            s.source.url = QUrl(url);
            s.source.kind = PlayableSource::Kind::Http;
            const QJsonObject request = hints.value("proxyHeaders").toObject().value("request").toObject();
            for (auto it = request.begin(); it != request.end(); ++it)
                s.source.headers.insert(it.key(), it.value().toString());
        } else {
            continue;  // ytId, externalUrl, or nothing playable
        }
        for (const SubtitleSource& sub : parseSubtitles(o))
            s.source.subtitles << sub;
        ranking::fillFromLabels(s);
        out << s;
    }
    return out;
}

QList<SubtitleSource> parseSubtitles(const QJsonObject& json)
{
    QList<SubtitleSource> out;
    for (const QJsonValue& v : json.value("subtitles").toArray()) {
        const QJsonObject o = v.toObject();
        const QUrl url(o.value("url").toString());
        if (!url.isValid() || url.isEmpty())
            continue;
        SubtitleSource s;
        s.url = url;
        s.language = o.value("lang").toString();
        s.label = o.value("label").toString();
        out << s;
    }
    return out;
}

} // namespace cj::stremio
