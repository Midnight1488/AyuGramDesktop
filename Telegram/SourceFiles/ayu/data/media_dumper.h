#pragma once

#include <QString>

class HistoryItem;

namespace AyuMediaDumper {

// Returns sanitized folder path for the peer / user who sent the message:
// e.g., "<cWorkingDir>/saved_deleted/<user_id>_<username>/"
QString getUserDirectory(HistoryItem *item);

// Dumps deleted message: logs text to deleted_messages.txt and dumps any cached/available media
void dumpDeletedMessage(HistoryItem *item);

// Dumps temporary / TTL / view-once photo or media to the user's folder
void dumpTtlMedia(HistoryItem *item);

} // namespace AyuMediaDumper
