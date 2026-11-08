#pragma once
// Watch history, Continue Watching and the saved library, for the UI
// (exposed to QML as `library`). Records where the user got to from the
// Session's resume points, tells the Session where to start an item, and
// keeps the Storage up to date.

#include "SourceTypes.h"
#include "Storage.h"

#include <QObject>
#include <QUrl>
#include <QVariantList>

#include <optional>

namespace cj {

class Browser;
class Session;

class Library : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList continueWatching READ continueWatching NOTIFY historyChanged)
    Q_PROPERTY(int historyCount READ historyCount NOTIFY historyChanged)
    Q_PROPERTY(QVariantList items READ items NOTIFY itemsChanged)

public:
    Library(Storage& storage, Session& session, Browser& browser, QObject* parent = nullptr);

    // Watched to the end, or close enough that the rest is credits.
    static bool isFinished(double position, double duration);

    QVariantList continueWatching() const;
    int historyCount() const { return int(m_storage.history().size()); }
    QVariantList items() const;

    // Plays a history entry again from where it was left.
    Q_INVOKABLE void resume(const QString& key);
    // Removes a show or movie from Continue Watching and the history.
    Q_INVOKABLE void forget(const QString& itemKey);
    Q_INVOKABLE void clearHistory();
    // Records where the current item is now (the app is closing).
    void saveNow();

    Q_INVOKABLE bool contains(const QString& key) const;
    // An item as the browse panel shows it (Browser::detail()).
    Q_INVOKABLE void add(const QVariantMap& item);
    Q_INVOKABLE void remove(const QString& key);
    // Shows a saved item, or the show of a history entry, in the browse panel.
    Q_INVOKABLE void open(const QString& key);

    // Every list as one file, and back. Each returns an error in words, or "".
    Q_INVOKABLE QString exportTo(const QUrl& file) const;
    Q_INVOKABLE QString importFrom(const QUrl& file);

signals:
    void historyChanged();
    void itemsChanged();
    // The browse panel should open to show an item.
    void openRequested();

private:
    struct Current
    {
        HistoryEntry entry;  // everything but the position
        QUrl url;
    };
    static HistoryEntry entryFor(const PlayContext& context);
    void bindPending();
    void save(const PlayableSource& source, double position, double duration, bool ended);
    double resumeAt(const PlayableSource& source) const;

    Storage& m_storage;
    Session& m_session;
    Browser& m_browser;
    std::optional<HistoryEntry> m_pending;  // picked, not yet loaded
    std::optional<Current> m_current;       // what is loaded now, when picked
};

} // namespace cj
