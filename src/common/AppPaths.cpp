#include "AppPaths.h"

#include <QDir>
#include <QStandardPaths>

namespace cj {

AppPaths AppPaths::resolve()
{
    AppPaths p;
    p.data  = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    p.cache = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    p.logs  = QDir(p.data).filePath(QStringLiteral("logs"));
    return p;
}

bool AppPaths::ensureExist() const
{
    for (const QString& dir : {data, cache, logs}) {
        if (dir.isEmpty() || !QDir().mkpath(dir))
            return false;
    }
    return true;
}

} // namespace cj
