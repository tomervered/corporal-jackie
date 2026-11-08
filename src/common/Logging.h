#pragma once
#include <QLoggingCategory>
#include <QString>

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

} // namespace cj::logging
