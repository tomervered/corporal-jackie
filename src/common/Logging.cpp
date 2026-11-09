#include "Logging.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QRegularExpression>
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
    // Call sites already log links through redactUrl; this catches any that
    // slip through, so the file never keeps a token.
    const QByteArray line =
        formatLine(type, ctx.category ? ctx.category : "default", redactText(msg)).toUtf8() + '\n';

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

QString redactUrl(const QUrl& url)
{
    if (url.isLocalFile())
        return QStringLiteral("file:…/") + QFileInfo(url.toLocalFile()).fileName();
    const QString scheme = url.scheme().toLower();
    if (scheme == QLatin1String("magnet")) {
        // Trackers (tr=) of private sites carry a personal passkey.
        static const QRegularExpression hash(QStringLiteral("xt=urn:btih:([0-9A-Za-z]+)"));
        const auto m = hash.match(url.toString());
        return QStringLiteral("magnet:?xt=urn:btih:") + (m.hasMatch() ? m.captured(1) : QStringLiteral("…"));
    }
    if (scheme == QLatin1String("torrent"))
        return url.toString(QUrl::RemoveUserInfo | QUrl::RemoveQuery | QUrl::RemoveFragment);
    if (scheme.isEmpty() || url.host().isEmpty())
        return QStringLiteral("(link)");
    QString out = scheme + QStringLiteral("://") + url.host().toLower();
    if (url.port() >= 0)
        out += QLatin1Char(':') + QString::number(url.port());
    if ((!url.path().isEmpty() && url.path() != QLatin1String("/")) || url.hasQuery())
        out += QStringLiteral("/…");
    return out;
}

QString redactText(const QString& text)
{
    // scheme://anything up to whitespace or a quote.
    static const QRegularExpression link(
        QStringLiteral(R"(\b(?:[A-Za-z][A-Za-z0-9+.-]*://|magnet:\?)[^\s'"<>]+)"));
    QString out;
    qsizetype last = 0;
    auto it = link.globalMatch(text);
    while (it.hasNext()) {
        const auto m = it.next();
        out += text.mid(last, m.capturedStart() - last);
        out += redactUrl(QUrl(m.captured()));
        last = m.capturedEnd();
    }
    return last == 0 ? text : out + text.mid(last);
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

void clearFiles()
{
    QMutexLocker lock(&g_mutex);
    if (!g_file)
        return;
    const QFileInfo current(g_file->fileName());
    QFile::remove(current.dir().filePath(QStringLiteral("corporal-jackie.1.log")));
    g_file->resize(0);
    g_file->seek(0);
}

QString currentLogFile()
{
    QMutexLocker lock(&g_mutex);
    return g_file ? g_file->fileName() : QString();
}

} // namespace cj::logging
