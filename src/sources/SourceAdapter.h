#pragma once
// The interface every source adapter implements: Stremio add-ons and local
// files in v1, YouTube later. Adapters never talk to a playback target; they
// only produce catalogs, details and stream choices, and every stream choice
// is one PlayableSource.

#include "SourceTypes.h"

#include <QObject>

#include <functional>
#include <optional>

namespace cj {

class SourceAdapter : public QObject
{
    Q_OBJECT

public:
    using QObject::QObject;
    ~SourceAdapter() override = default;

    virtual QString id() const = 0;    // stable, e.g. "stremio", "local"
    virtual QString name() const = 0;  // for the UI

    virtual QList<CatalogRef> catalogs() const = 0;

    // Each fetch answers exactly once, on the thread that asked, with either
    // results or an error message. Nothing blocks.
    using CatalogDone = std::function<void(const QList<MetaPreview>& items, const QString& error)>;
    virtual void fetchCatalog(const CatalogRef& catalog, const QString& search, int skip, CatalogDone done) = 0;

    using MetaDone = std::function<void(const std::optional<MetaDetail>& meta, const QString& error)>;
    virtual void fetchMeta(const MetaPreview& item, MetaDone done) = 0;

    // Streams for one video (a movie's id or an episode's id). An adapter
    // with several providers answers once per provider as each one answers
    // (`provider` names it, `error` says why it failed), then calls
    // `finished`. An adapter that has nothing for this id calls only
    // `finished`.
    using StreamsPart = std::function<void(const QList<StreamOption>& streams, const QString& provider,
                                           const QString& error)>;
    virtual void fetchStreams(const QString& type, const QString& videoId, StreamsPart part,
                              std::function<void()> finished) = 0;

    // True when this adapter's ids name things on this computer (a local
    // file's path, say). Such an id is never sent to another adapter: an
    // add-on would learn the user's folders and file names.
    virtual bool idsArePrivate() const { return false; }

    // Extra subtitle files for a video, from subtitle add-ons. Default: none.
    using SubtitlesDone = std::function<void(const QList<SubtitleSource>& subtitles)>;
    virtual void fetchSubtitles(const QString& /*type*/, const QString& /*videoId*/, SubtitlesDone done)
    {
        done({});
    }

signals:
    void catalogsChanged();
};

} // namespace cj
