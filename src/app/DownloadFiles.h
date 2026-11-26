#pragma once
#include <QString>

namespace cj::downloadFiles {

// A .part file reserves the name until every byte is on disk.
QString begin(const QString& folder, const QString& name);
QString finish(const QString& part, const QString& wantedName = {});

} // namespace cj::downloadFiles
