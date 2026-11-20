#pragma once

#include "action_registry.h"
#include <string.h>

namespace dialog_action {
inline bool timing_valid(uint32_t ms) {
    return ms == 0 || (ms >= 1000 && ms <= 300000);
}

inline void describe(JsonObject& action, bool confirmation) {
    action["group"] = "Display";
    action["label"] = confirmation ? "Ask for confirmation" : "Show notification";
    action["note"] = confirmation
        ? "Approval resumes the sequence; cancellation or timeout stops it."
        : "The sequence continues immediately while the notification is displayed.";
}
}
