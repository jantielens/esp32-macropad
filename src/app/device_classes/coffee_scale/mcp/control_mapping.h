#pragma once

#include "../coffee_scale_payload.h"
#include <ArduinoJson.h>
#include <string.h>

struct CsControlMapping {
    const char* type;
    const char* command;
    const char* operation;
    const char* argument;
    bool number;
};

inline const CsControlMapping* cs_control_mapping(const char* type, const char* command) {
    static const CsControlMapping commands[] = {
        {ACTION_TYPE_SCALE, "tare", "scale.tare", nullptr, false},
        {ACTION_TYPE_SCALE, "calibrate", "scale.calibrate", nullptr, false},
        {ACTION_TYPE_SCALE, "cal_weight", "scale.calibration.adjust_weight", "grams", true},
        {ACTION_TYPE_SCALE, "cal_weight_set", "scale.calibration.set_weight", "grams", true},
        {ACTION_TYPE_BREW, "advance", "brew.advance", nullptr, false},
        {ACTION_TYPE_BREW, "start", "brew.start", nullptr, false},
        {ACTION_TYPE_BREW, "next", "brew.next", nullptr, false},
        {ACTION_TYPE_BREW, "stop", "brew.stop", nullptr, false},
        {ACTION_TYPE_BREW, "reset", "brew.reset", nullptr, false},
        {ACTION_TYPE_BREW, "tare", "brew.tare", nullptr, false},
        {ACTION_TYPE_BREW, "set_template", "brew.template.set", "template", false},
    };
    if (!type || !command) return nullptr;
    for (const CsControlMapping& candidate : commands)
        if (!strcmp(candidate.type, type) && !strcmp(candidate.command, command)) return &candidate;
    return nullptr;
}

inline void cs_control_request(JsonObject doc, const CsControlMapping& mapping, const char* value) {
    doc["op"] = mapping.operation;
    JsonObject args = doc["args"].to<JsonObject>();
    if (mapping.argument) {
        if (mapping.number) args[mapping.argument]["$template"] = value;
        else args[mapping.argument] = value;
    }
}
