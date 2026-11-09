#pragma once
#include <QLoggingCategory>
#include <QString>
#include <QUrl>

// One logging category per layer (rule 8: log, don't guess).
// Turn a layer up with QT_LOGGING_RULES, e.g. "cj.broker.debug=true".
Q_DECLARE_LOGGING_CATEGORY(lcApp)
Q_DECLARE_LOGGING_CATEGORY(lcUi)
Q_DECLARE_LOGGING_CATEGORY(lcSession)
Q_DECLARE_LOGGING_CATEGORY(lcTargets)
Q_DECLARE_LOGGING_CATEGORY(lcSources)
Q_DECLARE_LOGGING_CATEGORY(lcTransports)
Q_DECLARE_LOGGING_CATEGORY(lcBroker)
Q_DECLARE_LOGGING_CATEGORY(lcStorage)

namespace cj::logging {

// Sends every Qt log message to stderr and to <logDir>/corporal-jackie.log,
// one line each: time, level, category, message. The previous run's file is
// kept as corporal-jackie.1.log. Thread-safe.
bool init(const QString& logDir);

// Restores Qt's default handler and closes the file.
void shutdown();

// Path of the current log file, or empty before init().
QString currentLogFile();

// Formats one log line. Exposed for tests.
QString formatLine(QtMsgType type, const char* category, const QString& message);

// A URL as it may appear in a log: what kind of link and which server, never
// the parts that can hold a reusable secret. Add-on links and signed video
// links carry tokens in the user:password, the path and the query, so a web
// link becomes "https://host:port/…". A local file keeps only its name, a
// magnet keeps only its info hash (a private tracker's link has a passkey),
// and torrent:// links (an info hash and a file number) stay whole.
QString redactUrl(const QUrl& url);

// The same for free text that may contain links, such as mpv's own messages.
QString redactText(const QString& text);

} // namespace cj::logging
