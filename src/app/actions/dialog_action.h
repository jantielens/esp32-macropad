#pragma once

#include "action_registry.h"
#include <string.h>

namespace dialog_action {
inline bool timing_valid(uint32_t ms) {
    return ms == 0 || (ms >= 1000 && ms <= 300000);
}

inline const char* validate(JsonObjectConst action, bool confirmation) {
    if (!action["message"].is<const char*>() || !action["message"].as<const char*>()[0])
        return "dialog message must be nonempty text";
    if (strlen(action["message"].as<const char*>()) >= CONFIG_DIALOG_MESSAGE_MAX_LEN)
        return "dialog message exceeds 191 UTF-8 bytes";
    const char* timing = confirmation ? "timeout_ms" : "duration_ms";
    if (action.containsKey(timing) &&
        (!action[timing].is<uint32_t>() || !timing_valid(action[timing].as<uint32_t>())))
        return "dialog timing must be 0 or 1000-300000 milliseconds";
    if (confirmation) {
        for (const char* field : {"approve_text", "cancel_text"}) {
            if (action.containsKey(field) &&
                (!action[field].is<const char*>() || !action[field].as<const char*>()[0] ||
                 strlen(action[field].as<const char*>()) >= CONFIG_DIALOG_CAPTION_MAX_LEN))
                return "dialog captions must be nonempty text of at most 27 UTF-8 bytes";
        }
    }
    return nullptr;
}

inline void describe(JsonObject& action, bool confirmation) {
    action["group"] = "Display";
    action["label"] = confirmation ? "Ask for confirmation" : "Show notification";
    JsonArray fields = action.createNestedArray("fields");
    JsonObject message = fields.createNestedObject();
    message["name"] = "message";
    message["description"] = "Required multiline message; bindings resolved once; max 191 UTF-8 bytes";
    JsonObject timing = fields.createNestedObject();
    timing["name"] = confirmation ? "timeout_ms" : "duration_ms";
    timing["description"] = confirmation
        ? "0 = no timeout, otherwise 1000-300000 ms; default 10000; approval resumes, all other outcomes stop"
        : "0 = until dismissed, otherwise 1000-300000 ms; default 3000; sequence continues immediately";
    if (confirmation) {
        for (const char* name : {"approve_text", "cancel_text"}) {
            JsonObject caption = fields.createNestedObject();
            caption["name"] = name;
            caption["description"] = "Literal button caption, max 27 UTF-8 bytes; defaults Continue / Cancel";
        }
    }
}
}
