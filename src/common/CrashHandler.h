#pragma once
#include <QString>

namespace cj::crash {

// Skeleton crash reporting. On a fatal signal or an uncaught C++ exception it
// writes <crashDir>/last-crash.txt with the reason and time, then lets the
// process die as it normally would. The next launch reports and clears it.
//
// Later this is where a real reporter (e.g. Crashpad minidumps) plugs in.
void install(const QString& crashDir);

// Returns the previous run's crash report and removes it, or an empty string
// if the previous run exited cleanly.
QString takePreviousReport(const QString& crashDir);

} // namespace cj::crash
