#include "Library.h"

#include "Browser.h"
#include "Logging.h"
#include "Session.h"

#include <QFile>
#include <QSaveFile>

#include <algorithm>

namespace cj {

namespace {

QString pathOf(const QUrl& file) { return file.isLocalFile() ? file.toLocalFile() : file.toString(); }

QString episodeLabel(const HistoryEntry& e)
{
    QString label;
    if (e.season > 0 || e.episode > 0)
        label = QStringLiteral("S%1 E%2").arg(e.season).arg(e.episode);
    if (!e.videoTitle.isEmpty() && e.videoTitle != e.itemName)
        label += (label.isEmpty() ? QString() : QStringLiteral(" · ")) + e.videoTitle;
    return label;
}

} // namespace

Library::Library(Storage& storage, Session& session, Browser& browser, QObject* parent)
    : QObject(parent)
    , m_storage(storage)
    , m_session(session)
    , m_browser(browser)
{
    connect(&storage, &Storage::historyChanged, this, &Library::historyChanged);
    connect(&storage, &Storage::libraryChanged, this, &Library::itemsChanged);

    // A pick in the browse panel says what the next item is; the Session
    // then asks where to start it, and loads it.
    connect(&browser, &Browser::aboutToPlay, this, [this](const PlayContext& context) {
        m_pending = entryFor(context);
    });
    session.setResumeLookup([this](const PlayableSource& source) { return resumeAt(source); });
    connect(&session, &Session::sourceChanged, this, &Library::bindPending);

    connect(&session, &Session::resumePoint, this,
            [this](const PlayableSource& source, double position, double duration) {
        save(source, position, duration, false);
    });
    connect(&session, &Session::stateChanged, this, [this] {
        if (m_session.state() == PlaybackState::Ended)
            save(m_session.playableSource(), m_session.position(), m_session.duration(), true);
    });
}

bool Library::isFinished(double position, double duration)
{
    // The last 5% (at least 20 seconds) is credits for nearly everything.
    return duration > 0 && duration - position <= std::max(20.0, duration * 0.05);
}

HistoryEntry Library::entryFor(const PlayContext& context)
{
    HistoryEntry e;
    e.adapter = context.item.adapter;
    e.provider = context.item.provider;
    e.itemId = context.item.id;
    e.itemType = context.item.type;
    e.itemName = context.item.name;
    e.poster = context.item.poster;
    e.videoId = context.video.id.isEmpty() ? context.item.id : context.video.id;
    e.videoTitle = context.video.title;
    e.season = context.video.season;
    e.episode = context.video.episode;
    e.key = e.adapter + QLatin1Char(':') + e.itemId + QLatin1Char(':') + e.videoId;
    return e;
}

double Library::resumeAt(const PlayableSource& source) const
{
    std::optional<HistoryEntry> e;
    if (m_pending)
        e = m_storage.find(m_pending->key);
    if (!e)
        e = m_storage.findBySource(source);
    return e && !e->finished && e->position >= Storage::kMinResumeSeconds ? e->position : 0;
}

void Library::bindPending()
{
    const QUrl url = m_session.source();
    if (m_pending && !url.isEmpty())
        m_current = Current{*m_pending, url};
    else if (!m_current || m_current->url != url)
        m_current.reset();  // opened some other way: a file, a link
    m_pending.reset();
}

void Library::save(const PlayableSource& source, double position, double duration, bool ended)
{
    if (source.url.isEmpty() || (position <= 0 && duration <= 0))
        return;
    HistoryEntry e;
    if (m_current && m_current->url == source.url) {
        e = m_current->entry;
    } else {
        e.key = QStringLiteral("url:") + Storage::sourceKey(source);
        e.itemName = source.title;
    }
    e.source = source;
    e.position = position;
    e.duration = duration;
    e.finished = ended || isFinished(position, duration);
    e.updatedAt = 0;  // now
    m_storage.record(e);
}

QVariantList Library::continueWatching() const
{
    QVariantList out;
    for (const HistoryEntry& e : m_storage.continueWatching()) {
        out.append(QVariantMap{
            {"key", e.key},
            {"itemKey", e.itemKey()},
            {"name", e.itemName.isEmpty() ? e.source.title : e.itemName},
            {"subtitle", episodeLabel(e)},
            {"poster", e.poster},
            {"position", e.position},
            {"duration", e.duration},
            {"progress", e.duration > 0 ? std::clamp(e.position / e.duration, 0.0, 1.0) : 0.0},
            {"canOpen", !e.adapter.isEmpty()},
        });
    }
    return out;
}

QVariantList Library::items() const
{
    QVariantList out;
    for (const LibraryItem& i : m_storage.library()) {
        out.append(QVariantMap{
            {"key", i.key()}, {"adapter", i.adapter}, {"provider", i.provider}, {"id", i.itemId},
            {"type", i.type}, {"name", i.name}, {"poster", i.poster}, {"releaseInfo", i.releaseInfo},
        });
    }
    return out;
}

void Library::resume(const QString& key)
{
    const auto e = m_storage.find(key);
    if (!e)
        return;
    qCInfo(lcStorage).noquote() << "Resuming" << e->key << "at" << e->position;
    if (!e->adapter.isEmpty())
        m_pending = *e;
    // An item finished or barely started plays from the beginning.
    const double start = !e->finished && e->position >= Storage::kMinResumeSeconds ? e->position : 0;
    m_session.playSource(e->source, start);
}

void Library::saveNow()
{
    if (m_session.hasMedia())
        save(m_session.playableSource(), m_session.position(), m_session.duration(),
             m_session.state() == PlaybackState::Ended);
}

void Library::forget(const QString& itemKey) { m_storage.forget(itemKey); }

void Library::clearHistory() { m_storage.clearHistory(); }

bool Library::contains(const QString& key) const { return m_storage.inLibrary(key); }

double Library::progress(const QString& key) const
{
    const auto e = m_storage.find(key);
    if (!e)
        return -1;
    if (e->finished)
        return 1;
    return e->duration > 0 ? std::clamp(e->position / e->duration, 0.0, 1.0) : 0;
}

void Library::add(const QVariantMap& item)
{
    LibraryItem i;
    i.adapter = item.value("adapter").toString();
    i.provider = item.value("provider").toString();
    i.itemId = item.value("id").toString();
    i.type = item.value("type").toString();
    i.name = item.value("name").toString();
    i.poster = item.value("poster").toString();
    i.releaseInfo = item.value("releaseInfo").toString();
    if (i.adapter.isEmpty() || i.itemId.isEmpty())
        return;
    m_storage.addToLibrary(i);
}

void Library::remove(const QString& key) { m_storage.removeFromLibrary(key); }

void Library::open(const QString& key)
{
    MetaPreview preview;
    const QList<LibraryItem> saved = m_storage.library();
    const auto item = std::find_if(saved.begin(), saved.end(), [&](const LibraryItem& i) { return i.key() == key; });
    if (item != saved.end()) {
        preview = {item->adapter, item->provider, item->itemId, item->type, item->name, item->poster,
                   item->releaseInfo, {}};
    } else {
        const QList<HistoryEntry> history = m_storage.history();
        const auto e = std::find_if(history.begin(), history.end(), [&](const HistoryEntry& h) {
            return !h.adapter.isEmpty() && (h.itemKey() == key || h.key == key);
        });
        if (e == history.end())
            return;
        preview = {e->adapter, e->provider, e->itemId, e->itemType, e->itemName, e->poster, {}, {}};
    }
    m_browser.openPreview(preview);
    emit openRequested();
}

QString Library::exportTo(const QUrl& file) const
{
    QSaveFile out(pathOf(file));
    if (!out.open(QIODevice::WriteOnly) || out.write(m_storage.exportJson()) < 0 || !out.commit())
        return QStringLiteral("Could not save the file (%1).").arg(out.errorString());
    qCInfo(lcStorage).noquote() << "Exported to" << pathOf(file);
    return {};
}

QString Library::importFrom(const QUrl& file)
{
    QFile in(pathOf(file));
    if (!in.open(QIODevice::ReadOnly))
        return QStringLiteral("Could not open the file (%1).").arg(in.errorString());
    if (in.size() > 256 * 1024 * 1024)
        return QStringLiteral("That file is too large to be an export.");
    return m_storage.importJson(in.readAll());
}

} // namespace cj
