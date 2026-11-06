#include "CrashHandler.h"

#include <QDir>
#include <QFile>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <exception>

namespace cj::crash {
namespace {

// Kept as a plain buffer so the handler does not touch Qt. Writing with fopen
// from a signal handler is best effort only; Crashpad replaces this later.
char g_reportPath[4096] = {};

void writeReport(const char* reason)
{
    if (!g_reportPath[0])
        return;
    if (std::FILE* f = std::fopen(g_reportPath, "w")) {
        std::fprintf(f, "reason: %s\ntime: %lld\n", reason,
                     static_cast<long long>(std::time(nullptr)));
        std::fclose(f);
    }
}

void onSignal(int sig)
{
    const char* name = "unknown signal";
    switch (sig) {
    case SIGSEGV: name = "SIGSEGV (invalid memory access)"; break;
    case SIGABRT: name = "SIGABRT (abort)"; break;
    case SIGFPE:  name = "SIGFPE (arithmetic error)"; break;
    case SIGILL:  name = "SIGILL (illegal instruction)"; break;
    }
    writeReport(name);
    std::signal(sig, SIG_DFL);
    std::raise(sig);
}

void onTerminate()
{
    writeReport("uncaught C++ exception (std::terminate)");
    std::abort();
}

QString reportPath(const QString& crashDir)
{
    return QDir(crashDir).filePath(QStringLiteral("last-crash.txt"));
}

} // namespace

void install(const QString& crashDir)
{
    QDir().mkpath(crashDir);
    const QByteArray path = QFile::encodeName(reportPath(crashDir));
    std::strncpy(g_reportPath, path.constData(), sizeof(g_reportPath) - 1);

    for (int sig : {SIGSEGV, SIGABRT, SIGFPE, SIGILL})
        std::signal(sig, onSignal);
    std::set_terminate(onTerminate);
}

QString takePreviousReport(const QString& crashDir)
{
    QFile file(reportPath(crashDir));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    const QString report = QString::fromUtf8(file.readAll()).trimmed();
    file.close();
    file.remove();
    return report;
}

} // namespace cj::crash
