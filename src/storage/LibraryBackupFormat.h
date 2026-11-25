#pragma once
#include "Storage.h"

namespace cj::backup {

QJsonObject writeWatchHistoryEntry(const HistoryEntry& entry);
HistoryEntry readWatchHistoryEntry(const QJsonObject& json);
QJsonObject writeLibraryItem(const LibraryItem& item);
LibraryItem readLibraryItem(const QJsonObject& json);

} // namespace cj::backup
