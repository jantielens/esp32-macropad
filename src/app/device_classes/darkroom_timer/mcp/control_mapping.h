#pragma once

#include "../darkroom_timer_payload.h"
#include <ArduinoJson.h>
#include <string.h>

struct DrControlMapping {
    const char* type;
    const char* command;
    const char* operation;
    const char* argument;
    bool boolean;
};

inline const DrControlMapping* dr_control_mapping(const char* type, const char* command) {
    static const DrControlMapping commands[] = {
        {ACTION_TYPE_EXPOSE, "toggle", "exposure.toggle", nullptr, false},
        {ACTION_TYPE_EXPOSE, "start", "exposure.start", nullptr, false},
        {ACTION_TYPE_EXPOSE, "stop", "exposure.stop", nullptr, false},
        {ACTION_TYPE_EXPOSE, "pause", "exposure.pause", nullptr, false},
        {ACTION_TYPE_EXPOSE, "resume", "exposure.resume", nullptr, false},
        {ACTION_TYPE_EXPOSE, "reset", "exposure.reset", nullptr, false},
        {ACTION_TYPE_EXPOSE, "focus", "exposure.focus.on", nullptr, false},
        {ACTION_TYPE_EXPOSE, "focus_off", "exposure.focus.off", nullptr, false},
        {ACTION_TYPE_EXPOSE, "focus_toggle", "exposure.focus.toggle", nullptr, false},
        {ACTION_TYPE_EXPOSE, "set_time", "exposure.time.set", "seconds", false},
        {ACTION_TYPE_EXPOSE, "adjust_seconds", "exposure.time.adjust", "seconds", false},
        {ACTION_TYPE_EXPOSE, "adjust_stops", "exposure.stops.adjust", "stops", false},
        {ACTION_TYPE_EXPOSE, "set_dry_down", "exposure.dry_down.set", "percent", false},
        {ACTION_TYPE_EXPOSE, "adjust_dry_down", "exposure.dry_down.adjust", "percent", false},
        {ACTION_TYPE_STRIP, "start", "strip.start", nullptr, false},
        {ACTION_TYPE_STRIP, "cancel", "strip.cancel", nullptr, false},
        {ACTION_TYPE_STRIP, "step_up", "strip.step.increase", nullptr, false},
        {ACTION_TYPE_STRIP, "step_down", "strip.step.decrease", nullptr, false},
        {ACTION_TYPE_STRIP, "set_base", "strip.base.set", "seconds", false},
        {ACTION_TYPE_STRIP, "adjust_base", "strip.base.adjust", "seconds", false},
        {ACTION_TYPE_STRIP, "adjust_segments", "strip.segments.adjust", "segments", false},
        {ACTION_TYPE_STRIP, "set_segments", "strip.segments.set", "segments", false},
        {ACTION_TYPE_STRIP, "set_countdown", "strip.countdown.set", "seconds", false},
        {ACTION_TYPE_STRIP, "adjust_countdown", "strip.countdown.adjust", "seconds", false},
        {ACTION_TYPE_STRIP, "set_pause", "strip.pause.set", "seconds", false},
        {ACTION_TYPE_STRIP, "adjust_pause", "strip.pause.adjust", "seconds", false},
        {ACTION_TYPE_STRIP, "set_tick", "strip.tick.set", "enabled", true},
        {ACTION_TYPE_METER, "read_lref", "meter.reference.read", nullptr, false},
        {ACTION_TYPE_METER, "read_bright", "meter.bright.read", nullptr, false},
        {ACTION_TYPE_METER, "read_dark", "meter.dark.read", nullptr, false},
        {ACTION_TYPE_METER, "mag_measure_a", "meter.magnification.measure_a", nullptr, false},
        {ACTION_TYPE_METER, "mag_measure_b", "meter.magnification.measure_b", nullptr, false},
        {ACTION_TYPE_METER, "mag_clear", "meter.magnification.clear", nullptr, false},
        {ACTION_TYPE_METER, "set_lref", "meter.reference.set", "lux", false},
        {ACTION_TYPE_METER, "adjust_lref", "meter.reference.adjust", "lux", false},
        {ACTION_TYPE_METER, "set_zone5", "meter.zone5.set", "seconds", false},
        {ACTION_TYPE_METER, "adjust_zone5", "meter.zone5.adjust", "seconds", false},
        {ACTION_TYPE_PRINT, "toggle_star", "print.star.toggle", nullptr, false},
        {ACTION_TYPE_PRINT, "set_star", "print.star.set", "starred", true},
    };
    if (!type || !command) return nullptr;
    for (const DrControlMapping& candidate : commands)
        if (!strcmp(candidate.type, type) && !strcmp(candidate.command, command)) return &candidate;
    return nullptr;
}

// For boolean commands, value is already resolved under the caller's locks.
inline bool dr_control_request(JsonObject doc, const DrControlMapping& mapping, const char* value) {
    doc["op"] = mapping.operation;
    JsonObject args = doc["args"].to<JsonObject>();
    if (mapping.argument && mapping.boolean) {
        const bool strip = !strcmp(mapping.type, ACTION_TYPE_STRIP);
        const char* enabled = strip ? "on" : "1";
        const char* disabled = strip ? "off" : "0";
        if (strcmp(value, enabled) && strcmp(value, disabled)) return false;
        args[mapping.argument] = !strcmp(value, enabled);
    } else if (mapping.argument) {
        args[mapping.argument]["$template"] = value;
    }
    return true;
}
