#pragma once
#include <QString>

namespace cj {

// Where the app keeps its files on each OS. Call after QCoreApplication exists,
// because the paths come from the application and organization names.
//
//   Windows  %APPDATA%\Corporal Jackie                (settings, data, logs)
//            %LOCALAPPDATA%\Corporal Jackie\cache
//   macOS    ~/Library/Application Support/Corporal Jackie   (settings, data, logs)
//            ~/Library/Caches/Corporal Jackie
//   Linux    ~/.local/share/Corporal Jackie, ~/.cache/Corporal Jackie
struct AppPaths {
    QString data;   // database, settings
    QString cache;  // torrent piece cache, images; safe to delete
    QString logs;   // rotating log files and crash reports

    static AppPaths resolve();
    // Creates every folder that does not exist yet. Returns false on failure.
    bool ensureExist() const;
};

} // namespace cj
