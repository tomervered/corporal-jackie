#include "DependencyInfo.h"

#include <QtGlobal>
#include <libtorrent/version.hpp>
#include <mpv/client.h>
#include <sqlite3.h>

namespace cj {

QList<DependencyVersion> dependencyVersions()
{
    const unsigned long mpvApi = mpv_client_api_version();
    return {
        {QStringLiteral("Qt"), QString::fromLatin1(qVersion())},
        {QStringLiteral("libmpv client API"),
         QStringLiteral("%1.%2").arg(mpvApi >> 16).arg(mpvApi & 0xffff)},
        {QStringLiteral("libtorrent-rasterbar"), QString::fromLatin1(lt::version())},
        {QStringLiteral("SQLite"), QString::fromLatin1(sqlite3_libversion())},
    };
}

} // namespace cj
