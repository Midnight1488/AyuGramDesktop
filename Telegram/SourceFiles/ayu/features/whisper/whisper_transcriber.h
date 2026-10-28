#pragma once

#include <QString>
#include <functional>

class HistoryItem;

namespace AyuWhisper {

// Initialize module and background model checker
void initialize();

// Check if model file exists
bool isModelReady();

// Download model asynchronously if missing
void ensureModel(std::function<void(bool success)> onDone);

// Main entry point for local transcription
void transcribe(not_null<HistoryItem*> item);

// Process incoming voice/video message
void processIncomingMessage(not_null<HistoryItem*> item);

} // namespace AyuWhisper
