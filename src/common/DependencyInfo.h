#pragma once
#include <QList>
#include <QString>

namespace cj {

struct DependencyVersion {
    QString name;
    QString version;   // version of the library actually loaded at runtime
};

// Versions of the native libraries this build links. Calling it also proves
// at startup that every library loads, which is what Step 0's gate checks.
QList<DependencyVersion> dependencyVersions();

} // namespace cj
