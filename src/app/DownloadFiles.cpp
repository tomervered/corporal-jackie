#include "DownloadFiles.h"
#include "Downloads.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <mutex>

namespace cj {
namespace {

constexpr int kMaxNameLength = 180;
std::mutex nameLock;

// Names Windows keeps for devices, with or without an extension.
bool reservedOnWindows(const QString& name)
{
    static const QRegularExpression re(QStringLiteral("^(con|prn|aux|nul|com[0-9]|lpt[0-9])(\\..*)?$"),
                                       QRegularExpression::CaseInsensitiveOption);
    return re.match(name).hasMatch();
}

// If a name is taken, try "Movie (2).mkv", then "Movie (3).mkv".
QString availablePath(const QDir& folder, const QString& name, const QString& ourPart = {})
{
    const QFileInfo info(name);
    QString base = info.completeBaseName();
    QString extension;
    if (base.isEmpty())
        base = name;
    else if (!info.suffix().isEmpty())
        extension = QLatin1Char('.') + info.suffix();

    for (int copy = 1; copy < 1000; ++copy) {
        const QString filename = copy == 1 ? base + extension : QStringLiteral("%1 (%2)%3").arg(base).arg(copy).arg(extension);
        const QString path = folder.filePath(filename);
        const QString part = path + QStringLiteral(".part");
        if (!QFileInfo::exists(path) && (part == ourPart || !QFileInfo::exists(part)))
            return path;
    }
    return {};
}

} // namespace

QString downloadFiles::begin(const QString& folder, const QString& name)
{
    std::lock_guard lock(nameLock);
    const QString path = availablePath(QDir(folder), name);
    if (path.isEmpty())
        return {};
    QFile part(path + QStringLiteral(".part"));
    return part.open(QIODevice::WriteOnly | QIODevice::NewOnly) ? part.fileName() : QString();
}

QString downloadFiles::finish(const QString& part, const QString& wantedName)
{
    std::lock_guard lock(nameLock);
    const QFileInfo info(part);
    const QString name = wantedName.isEmpty() ? info.completeBaseName() : wantedName;
    const QString path = availablePath(info.dir(), name, part);
    return !path.isEmpty() && QFile::rename(part, path) ? path : QString();
}

QString Downloads::safeFileName(const QString& name, const QString& fallback)
{
    QString n = name;
    n.replace(QLatin1Char('\\'), QLatin1Char('/'));
    n = n.section(QLatin1Char('/'), -1);
    QString out;
    out.reserve(n.size());
    for (const QChar c : n) {
        if (c.unicode() < 32 || c == QChar(0x7f) || QStringLiteral("<>:\"|?*").contains(c))
            out += QLatin1Char('_');
        else
            out += c;
    }
    // Windows drops trailing dots and spaces, which would change the name.
    while (!out.isEmpty() && (out.endsWith(QLatin1Char('.')) || out.endsWith(QLatin1Char(' '))))
        out.chop(1);
    out = out.trimmed();
    if (out.isEmpty() || out == QLatin1String("..") || out == QLatin1String("."))
        out = fallback;
    if (reservedOnWindows(out))
        out.prepend(QLatin1Char('_'));
    if (out.size() > kMaxNameLength) {
        // Keep the extension.
        const QFileInfo info(out);
        const QString ext = info.suffix().size() <= 8 && !info.suffix().isEmpty() ? QLatin1Char('.') + info.suffix() : QString();
        out = out.left(kMaxNameLength - ext.size()).trimmed() + ext;
    }
    return out;
}

} // namespace cj
