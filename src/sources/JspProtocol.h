#pragma once
// The Jackie Source Protocol (docs/PROTOCOL.md) as plain functions: parse a
// manifest, build request paths, unwrap the answer envelope and turn each
// answer into the Stremio-shaped JSON the rest of the add-on code already
// reads (so the stream, header and link checks of StremioProtocol apply to
// both). No network here.

#include "StremioProtocol.h"

#include <QJsonObject>
#include <QStringList>
#include <QUrl>

#include <optional>

namespace cj::jsp {

// "protocol": 1 in the manifest.
bool isJsp(const QJsonObject& manifest);

// A manifest of either kind: JSP when it says so, else Stremio's.
std::optional<stremio::Manifest> parseAnyManifest(const QJsonObject& json, QString* error);
std::optional<stremio::Manifest> parseManifest(const QJsonObject& json, QString* error);

// The one address rule of a Jackie source: https, or http on 127.0.0.1/localhost for
// development; a host, and no user name or password in the address. The primary
// address, the mirrors and every redirect target are held to it.
bool allowedUrl(const QUrl& url);

// Other bases of the same source, https only (or 127.0.0.1 for development), without a trailing slash.
QStringList mirrors(const QJsonObject& manifest);

// What an answer carried.
struct Envelope
{
    bool ok = false;
    QString code;        // bad_request, not_found, rate_limited, upstream_down, unavailable
    QString message;
    int retryAfter = 0;  // seconds, for rate_limited
    int ttl = 60;        // seconds the answer may be reused
    QJsonObject data;
};
// Anything that is not an envelope is `unavailable`.
Envelope parseEnvelope(const QJsonObject& json);
// Whether `data` has the shape its resource promises (docs/PROTOCOL.md): `items`
// for a catalog or search, `streams`, `subtitles`, a title with an `id`. An empty
// list is fine; a missing or wrongly typed one is a broken answer, so it is never
// cached and never wins over a healthy mirror. Paths of other resources pass.
bool validData(const QString& path, const QJsonObject& data);
// The words for an error, for the add-ons page.
QString describe(const Envelope& e);
// Definitive: asking another mirror cannot change it.
bool isDefinitive(const Envelope& e);

// Paths relative to the base, ready to append: "/catalog/movie/trending?q=…".
QString catalogPath(const QString& type, const QString& id, const QString& search, const QString& genre,
                    const QString& year, int skip);
QString metaPath(const QString& type, const QString& id);
QString streamsPath(const QString& type, const QString& videoId);
QString subtitlesPath(const QString& type, const QString& videoId);

// The answer's data as the Stremio-shaped JSON that stremio::parseMetas,
// parseMeta, parseStreams and parseSubtitles read.
QJsonObject catalogAsStremio(const QJsonObject& data);
QJsonObject metaAsStremio(const QJsonObject& data);
QJsonObject streamsAsStremio(const QJsonObject& data);
QJsonObject subtitlesAsStremio(const QJsonObject& data);

} // namespace cj::jsp
