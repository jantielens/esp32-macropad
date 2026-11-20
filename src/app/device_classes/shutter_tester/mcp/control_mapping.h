#pragma once

#include <ArduinoJson.h>
#include <string.h>

struct ShutterControlMapping {
    const char* command;
    const char* operation;
    const char* argument;
};

inline const ShutterControlMapping* shutter_control_mapping(const char* command) {
    static const ShutterControlMapping commands[] = {
        {"set", "shutter.target.set", "speed"},
        {"adjust", "shutter.target.adjust", "direction"},
        {"toggle_lock", "shutter.target.toggle_lock", nullptr},
        {"sess_start", "shutter.session.start", "camera"},
        {"sess_stop", "shutter.session.stop", nullptr},
        {"sess_toggle", "shutter.session.toggle", "camera"},
        {"sess_discard", "shutter.session.discard", nullptr},
        {"guide_start", "shutter.guide.start", "test"},
        {"guide_stop", "shutter.guide.stop", nullptr},
        {"guide_skip", "shutter.guide.skip", nullptr},
        {"guide_redo", "shutter.guide.redo", nullptr},
        {"align_start", "shutter.alignment.start", nullptr},
        {"align_stop", "shutter.alignment.stop", nullptr},
        {"recalibrate", "shutter.recalibrate", nullptr},
    };
    if (!command) return nullptr;
    for (const ShutterControlMapping& candidate : commands)
        if (!strcmp(candidate.command, command)) return &candidate;
    return nullptr;
}

inline void shutter_control_request(JsonObject doc, const ShutterControlMapping& mapping, const char* value) {
    doc["op"] = mapping.operation;
    JsonObject args = doc["args"].to<JsonObject>();
    if (mapping.argument) {
        if (value[0]) args[mapping.argument]["$template"] = value;
        else args[mapping.argument] = "";
    }
}
