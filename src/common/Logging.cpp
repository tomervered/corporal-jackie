#include "Logging.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QMutex>
#include <QMutexLocker>
#include <cstdio>

Q_LOGGING_CATEGORY(lcApp,        "cj.app")
Q_LOGGING_CATEGORY(lcUi,         "cj.ui")
Q_LOGGING_CATEGORY(lcSession,    "cj.session")
Q_LOGGING_CATEGORY(lcTargets,    "cj.targets")
Q_LOGGING_CATEGORY(lcSources,    "cj.sources")
Q_LOGGING_CATEGORY(lcTransports, "cj.transports")
Q_LOGGING_CATEGORY(lcBroker,     "cj.broker")
Q_LOGGING_CATEGORY(lcStorage,    "cj.storage")

namespace cj::logging {
namespace {

QMutex g_mutex;
QFile* g_file = nullptr;
QtMessageHandler g_previous = nullptr;

const char* levelName(QtMsgType type)
{
    switch (type) {
    case QtDebugMsg:    return "DEBUG";
    case QtInfoMsg:     return "INFO ";
    case QtWarningMsg:  return "WARN ";
    case QtCriticalMsg: return "ERROR";
    case QtFatalMsg:    return "FATAL";
    }
    return "?????";
}

void handler(QtMsgType type, const QMessageLogContext& ctx, const QString& msg)
{
    const QByteArray line =
        formatLine(type, ctx.category ? ctx.category : "default", msg).toUtf8() + '\n';

    QMutexLocker lock(&g_mutex);
    std::fwrite(line.constData(), 1, size_t(line.size()), stderr);
    if (g_file) {
        g_file->write(line);
        g_file->flush();
    }
}

} // namespace

QString formatLine(QtMsgType type, const char* category, const QString& message)
{
    return QStringLiteral("%1 %2 [%3] %4")
        .arg(QDateTime::currentDateTime().toString(Qt::ISODateWithMs),
             QString::fromLatin1(levelName(type)),
             QString::fromLatin1(category),
             message);
}

bool init(const QString& logDir)
{
    if (!QDir().mkpath(logDir))
        return false;

    const QString current  = QDir(logDir).filePath(QStringLiteral("corporal-jackie.log"));
    const QString previous = QDir(logDir).filePath(QStringLiteral("corporal-jackie.1.log"));

    QMutexLocker lock(&g_mutex);
    if (!g_file) {
        QFile::remove(previous);
        QFile::rename(current, previous);
        auto* file = new QFile(current);
        if (!file->open(QIODevice::WriteOnly | QIODevice::Text)) {
            delete file;
            return false;
        }
        g_file = file;
        g_previous = qInstallMessageHandler(handler);
    }
    return true;
}

void shutdown()
{
    QMutexLocker lock(&g_mutex);
    if (!g_file)
        return;
    qInstallMessageHandler(g_previous);
    g_previous = nullptr;
    delete g_file;
    g_file = nullptr;
}

QString currentLogFile()
{
    QMutexLocker lock(&g_mutex);
    return g_file ? g_file->fileName() : QString();
}

} // namespace cj::logging
