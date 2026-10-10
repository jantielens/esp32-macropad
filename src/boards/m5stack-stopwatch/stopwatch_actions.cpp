#include "action_registry.h"
#include "stopwatch_hardware.h"
#include <string.h>
#include <stdio.h>

#if HAS_DISPLAY || HAS_BUTTON
namespace {

uint8_t charging_mode_value(const char* mode) {
    if (mode && strcmp(mode, "off") == 0) return 0;
    if (mode && strcmp(mode, "low") == 0) return 1;
    if (mode && strcmp(mode, "high") == 0) return 2;
    return 255;
}

void parse_charging_mode(const JsonObject& action, ButtonAction& act) {
    act.payload.device_class[0] = charging_mode_value(action["mode"] | "");
}

void serialize_charging_mode(const ButtonAction& act, JsonObject action) {
    const uint8_t mode = act.payload.device_class[0];
    if (mode <= 2) action["mode"] = mode == 0 ? "off" : mode == 1 ? "low" : "high";
}

ActionResult dispatch_charging_mode(const ButtonAction& act, const char*, uint32_t) {
    return m5stack_stopwatch_set_charging_mode(
        static_cast<StopWatchChargingMode>(act.payload.device_class[0])) ? ACTION_COMPLETE : ACTION_FAILED;
}

const char* validate_charging_mode(JsonObjectConst action) {
    return action["mode"].is<const char*>() &&
        charging_mode_value(action["mode"].as<const char*>()) <= 2 ? nullptr :
        "charging_mode mode must be off, low, or high";
}

void describe_charging_mode(JsonObject& action) {
    action["group"] = "M5Stack StopWatch";
    action["label"] = "Set charging mode";
    JsonArray commands = action.createNestedArray("commands");
    const char* modes[] = {"off", "low", "high"};
    const char* labels[] = {"Off", "Low (185 mA)", "High (425 mA)"};
    for (uint8_t mode = 0; mode < 3; ++mode) {
        JsonObject command = commands.createNestedObject();
        command["id"] = modes[mode];
        command["label"] = labels[mode];
    }
    JsonArray fields = action.createNestedArray("fields");
    JsonObject mode = fields.createNestedObject();
    mode["name"] = "mode";
    mode["description"] = "off, low (nominal 185 mA), or high (nominal 425 mA); reboot restores the boot setting";
    JsonArray editor = action.createNestedArray("editor_fields");
    JsonObject select = editor.createNestedObject();
    select["name"] = "mode";
    select["label"] = "Charging mode";
    select["type"] = "select";
    select["command_options"] = true;
}

uint8_t status_led_value(const char* mode) {
    if (mode && strcmp(mode, "off") == 0) return 0;
    if (mode && strcmp(mode, "on") == 0) return 1;
    return 255;
}

void parse_status_led(const JsonObject& action, ButtonAction& act) {
    act.payload.device_class[0] = status_led_value(action["mode"] | "");
}

void serialize_status_led(const ButtonAction& act, JsonObject action) {
    const uint8_t mode = act.payload.device_class[0];
    if (mode <= 1) action["mode"] = mode ? "on" : "off";
}

ActionResult dispatch_status_led(const ButtonAction& act, const char*, uint32_t) {
    const uint8_t mode = act.payload.device_class[0];
    return mode <= 1 && m5stack_stopwatch_set_status_led(mode != 0) ? ACTION_COMPLETE : ACTION_FAILED;
}

const char* validate_status_led(JsonObjectConst action) {
    return action["mode"].is<const char*>() && status_led_value(action["mode"].as<const char*>()) <= 1
        ? nullptr : "status_led mode must be on or off";
}

void describe_status_led(JsonObject& action) {
    action["group"] = "M5Stack StopWatch";
    action["label"] = "Status LED";
    JsonArray commands = action.createNestedArray("commands");
    for (const char* mode : {"on", "off"}) {
        JsonObject command = commands.createNestedObject();
        command["id"] = mode;
        command["label"] = strcmp(mode, "on") == 0 ? "On" : "Off";
    }
    JsonObject field = action.createNestedArray("fields").createNestedObject();
    field["name"] = "mode";
    field["description"] = "on or off; reboot restores the boot LED level; PMIC status signalling may override it";
    JsonObject editor = action.createNestedArray("editor_fields").createNestedObject();
    editor["name"] = "mode";
    editor["label"] = "Status LED";
    editor["type"] = "select";
    editor["command_options"] = true;
}

struct VibrationPayload {
    uint16_t steps[STOPWATCH_VIBRATION_MAX_STEPS];
    uint8_t count;
};
static_assert(sizeof(VibrationPayload) <= sizeof(ButtonAction{}.payload.device_class),
    "Vibration pattern must fit the action payload");

bool parse_vibration_pattern(const char* text, VibrationPayload& payload) {
    if (!text) return false;
    uint32_t total = 0;
    payload.count = 0;
    for (;;) {
        while (*text == ' ' || *text == '\t' || *text == '\r' || *text == '\n') ++text;
        if (*text < '0' || *text > '9' || payload.count == STOPWATCH_VIBRATION_MAX_STEPS) return false;
        uint32_t duration = 0;
        do {
            duration = duration * 10 + (*text++ - '0');
            if (duration > 5000) return false;
        } while (*text >= '0' && *text <= '9');
        if ((!duration && payload.count) || total + duration > 5000) return false;
        total += duration;
        payload.steps[payload.count++] = static_cast<uint16_t>(duration);
        while (*text == ' ' || *text == '\t' || *text == '\r' || *text == '\n') ++text;
        if (!*text) return (payload.count & 1) != 0;
        if (!duration) return false;
        if (*text++ != ',') return false;
    }
}

const char* validate_vibrate(JsonObjectConst action) {
    if (action.containsKey("duration_ms")) return "vibrate accepts pattern, not duration_ms";
    if (!action.containsKey("pattern")) return nullptr;
    VibrationPayload payload{};
    return action["pattern"].is<const char*>() &&
        parse_vibration_pattern(action["pattern"].as<const char*>(), payload) ? nullptr :
        "vibrate pattern needs 1-15 odd-count positive millisecond durations, comma-separated, total <=5000; 0 stops";
}

void parse_vibrate(const JsonObject& action, ButtonAction& act) {
    VibrationPayload payload{};
    if (!validate_vibrate(action)) {
        parse_vibration_pattern(action["pattern"] | "200", payload);
    }
    memcpy(act.payload.device_class, &payload, sizeof(payload));
}

VibrationPayload vibration_payload(const ButtonAction& act) {
    VibrationPayload payload;
    memcpy(&payload, act.payload.device_class, sizeof(payload));
    return payload;
}

void serialize_vibrate(const ButtonAction& act, JsonObject action) {
    const VibrationPayload payload = vibration_payload(act);
    if (!payload.count || payload.count > STOPWATCH_VIBRATION_MAX_STEPS) {
        action["pattern"] = "";
        return;
    }
    char pattern[STOPWATCH_VIBRATION_MAX_STEPS * 5] = {};
    size_t used = 0;
    for (uint8_t index = 0; index < payload.count; ++index) {
        used += snprintf(pattern + used, sizeof(pattern) - used, "%s%u",
            index ? "," : "", static_cast<unsigned>(payload.steps[index]));
    }
    action["pattern"] = pattern;
}

ActionResult dispatch_vibrate(const ButtonAction& act, const char*, uint32_t) {
    const VibrationPayload payload = vibration_payload(act);
    const bool accepted = m5stack_stopwatch_vibrate_pattern(payload.steps, payload.count);
    return accepted ? ACTION_COMPLETE : ACTION_FAILED;
}

void describe_vibrate(JsonObject& action) {
    action["group"] = "M5Stack StopWatch";
    action["label"] = "Vibrate";
    JsonObject field = action.createNestedArray("fields").createNestedObject();
    field["name"] = "pattern";
    field["description"] = "comma-separated vibrate/pause durations in ms, e.g. 20,100,20; 1-15 odd-count positive steps, total <=5000; default 200; 0 stops; replaces active sequence";
    JsonObject editor = action.createNestedArray("editor_fields").createNestedObject();
    editor["name"] = "pattern";
    editor["label"] = "Pattern (vibrate, pause, ... in ms)";
    editor["type"] = "text";
    editor["default"] = "200";
}

ActionResult dispatch_shutdown(const ButtonAction&, const char*, uint32_t continuation_token) {
    return m5stack_stopwatch_request_shutdown(continuation_token) ? ACTION_PENDING : ACTION_FAILED;
}

void describe_shutdown(JsonObject& action) {
    action["group"] = "M5Stack StopWatch";
    action["label"] = "Shutdown";
    action["max_pending_actions"] = ACTION_CONTINUATION_SLOTS;
    action.createNestedArray("fields");
    action.createNestedArray("editor_fields");
}

DEFINE_AND_REGISTER_ACTION_TYPE(kStopWatchChargingActionType, "charging_mode",
    parse_charging_mode, serialize_charging_mode, dispatch_charging_mode, nullptr,
    describe_charging_mode, nullptr, validate_charging_mode, nullptr, ACTION_EXECUTION_SYNC, false);
DEFINE_AND_REGISTER_ACTION_TYPE(kStopWatchStatusLedActionType, "status_led",
    parse_status_led, serialize_status_led, dispatch_status_led, nullptr,
    describe_status_led, nullptr, validate_status_led, nullptr, ACTION_EXECUTION_SYNC, false);
DEFINE_AND_REGISTER_ACTION_TYPE(kStopWatchVibrateActionType, "vibrate",
    parse_vibrate, serialize_vibrate, dispatch_vibrate, nullptr,
    describe_vibrate, nullptr, validate_vibrate, nullptr, ACTION_EXECUTION_SYNC, false);
DEFINE_AND_REGISTER_ACTION_TYPE(kStopWatchShutdownActionType, "shutdown",
    nullptr, nullptr, dispatch_shutdown, nullptr,
    describe_shutdown, nullptr, nullptr, nullptr, ACTION_EXECUTION_PAUSABLE, false);

}
#endif