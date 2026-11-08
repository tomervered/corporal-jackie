#pragma once
// The Stremio add-on protocol, as plain functions: parse a manifest, build
// request URLs, and turn responses into source types. No network here, so
// every rule is unit-tested. Protocol: github.com/Stremio/stremio-addon-sdk
// (docs/protocol.md and docs/api/responses).

#include "SourceTypes.h"

#include <QJsonObject>
#include <QUrl>

#include <optional>

namespace cj::stremio {

struct Resource
{
    QString name;           // "catalog", "meta", "stream", "subtitles"
    QStringList types;      // empty = the manifest's types
    QStringList idPrefixes; // empty = the manifest's prefixes
};

struct Catalog
{
    QString type;
    QString id;
    QString name;
    bool searchable = false;
    bool searchRequired = false;
    QStringList genres;          // the "genre" filter's options; empty = no filter
    bool genreRequired = false;  // answers only with a genre picked
};

struct Manifest
{
    QString id;
    QString version;
    QString name;
    QString description;
    QString logo;
    QStringList types;
    QStringList idPrefixes;  // empty = any id
    QList<Resource> resources;
    QList<Catalog> catalogs;
    bool configurationRequired = false;

    // Does this add-on answer `resource` for this type and id?
    bool serves(const QString& resource, const QString& type, const QString& id) const;
};

std::optional<Manifest> parseManifest(const QJsonObject& json, QString* error);

// "stremio://host/path/manifest.json" -> "https://host/path/manifest.json".
// Returns an invalid URL for anything that is not a manifest link.
QUrl normalizeManifestUrl(const QString& text);

// The add-on's base: the manifest URL without "/manifest.json".
QString baseOf(const QUrl& manifestUrl);

// {base}/{resource}/{type}/{id}.json, or with extras
// {base}/{resource}/{type}/{id}/{name=value&...}.json
QUrl resourceUrl(const QString& base, const QString& resource, const QString& type, const QString& id,
                 const QList<QPair<QString, QString>>& extras = {});

QList<MetaPreview> parseMetas(const QJsonObject& json);
std::optional<MetaDetail> parseMeta(const QJsonObject& json);

// Streams become playable sources: infoHash (+ fileIdx, + "tracker:" sources)
// becomes a magnet link with "so" set to the file, url stays a web link with
// any request headers the add-on gives. Others (YouTube ids, external links)
// are skipped: nothing in v1 plays them.
QList<StreamOption> parseStreams(const QJsonObject& json, const QString& addonId, const QString& addonName);

QList<SubtitleSource> parseSubtitles(const QJsonObject& json);

} // namespace cj::stremio
