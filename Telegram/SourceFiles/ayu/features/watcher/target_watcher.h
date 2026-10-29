#pragma once

#include <QString>
#include <unordered_set>
#include <vector>

class HistoryItem;

namespace AyuTargetWatcher {

// Check if specific user is on the watchlist
bool isWatched(uint64 userId);

// Toggle watch state for a user
bool toggleTarget(uint64 userId, const QString &username = QString(), const QString &name = QString());

// Add target explicitly
void addTarget(uint64 userId, const QString &username = QString(), const QString &name = QString());

// Remove target explicitly
void removeTarget(uint64 userId);

// Get list of all currently watched user IDs
std::vector<uint64> getWatchedList();

// Get directory for target: "<cWorkingDir>/targets/<userId>/"
QString getTargetDirectory(uint64 userId);

// Log deleted message for target
void logDeletedMessage(HistoryItem *item);

// Log message edit ("Was: ... -> Now: ...")
void logMessageEdit(HistoryItem *item, const QString &oldText, const QString &newText);

// Log activity (typing, online status changes, reactions)
void logActivity(uint64 userId, const QString &actionText);

// Backup entire chat when peer attempts to delete chat for both
void backupChatBeforeDelete(uint64 userId, const QString &chatTitle, const QString &fullLog);

} // namespace AyuTargetWatcher
