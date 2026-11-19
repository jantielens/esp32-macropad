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

void describe_charging_mode(JsonObject& action) {
    action["group"] = "M5Stack StopWatch";
    action["label"] = "Set charging mode";
    action["note"] = "Reboot restores the boot charging setting.";
}

const char* const charging_modes[] = {"off", "low", "high"};
const char* const charging_mode_labels[] = {"Off", "Low (185 mA)", "High (425 mA)"};
const ActionArgumentDef charging_arguments[] = {
    {"mode", "Charging mode", ACTION_ARGUMENT_ENUM, 0, 0, 4, true,
     "[var:charging_mode]", "select", charging_modes, 3, false, nullptr, charging_mode_labels},
};
ActionResult run_charging_mode(const ActionResolvedArgument* args, const char*, uint32_t) {
    const uint8_t mode = charging_mode_value(args[0].text);
    return mode <= 2 && m5stack_stopwatch_set_charging_mode(
        static_cast<StopWatchChargingMode>(mode)) ? ACTION_COMPLETE : ACTION_FAILED;
}
const ActionOperationDef charging_operations[] = {
    {"power.charging.set_mode", "Set charging mode", charging_arguments, 1, run_charging_mode},
};

uint8_t status_led_value(const char* mode) {
    if (mode && strcmp(mode, "off") == 0) return 0;
    if (mode && strcmp(mode, "on") == 0) return 1;
    return 255;
}

void describe_status_led(JsonObject& action) {
    action["group"] = "M5Stack StopWatch";
    action["label"] = "Status LED";
    action["note"] = "Reboot restores the boot LED level; PMIC status signalling may override it.";
}

const char* const led_modes[] = {"on", "off"};
const char* const led_mode_labels[] = {"On", "Off"};
const ActionArgumentDef led_arguments[] = {
    {"mode", "Status LED", ACTION_ARGUMENT_ENUM, 0, 0, 3, true,
     "[var:led_mode]", "select", led_modes, 2, false, nullptr, led_mode_labels},
};
ActionResult run_status_led(const ActionResolvedArgument* args, const char*, uint32_t) {
    const uint8_t mode = status_led_value(args[0].text);
    return mode <= 1 && m5stack_stopwatch_set_status_led(mode != 0) ? ACTION_COMPLETE : ACTION_FAILED;
}
const ActionOperationDef led_operations[] = {
    {"device.status_led.set", "Set status LED", led_arguments, 1, run_status_led},
};

struct VibrationPayload {
    uint16_t steps[STOPWATCH_VIBRATION_MAX_STEPS];
    uint8_t count;
};
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

const char* validate_pattern(const char* text) {
    VibrationPayload payload{};
    return parse_vibration_pattern(text, payload) ? nullptr :
        "pattern needs 1-15 odd-count positive millisecond durations, total <=5000; 0 stops";
}
const ActionArgumentDef vibration_arguments[] = {
    {"pattern", "Pattern (vibrate, pause, ... in ms)", ACTION_ARGUMENT_STRING, 0, 0,
     STOPWATCH_VIBRATION_MAX_STEPS * 5 - 1, true, "[var:vibration_pattern]", "text",
     nullptr, 0, false, validate_pattern, nullptr, 0, "200"},
};
ActionResult run_vibration(const ActionResolvedArgument* args, const char*, uint32_t) {
    VibrationPayload payload{};
    if (!parse_vibration_pattern(args[0].text, payload)) return ACTION_FAILED;
    return m5stack_stopwatch_vibrate_pattern(payload.steps, payload.count) ? ACTION_COMPLETE : ACTION_FAILED;
}
const ActionOperationDef vibration_operations[] = {
    {"device.vibrate", "Vibrate", vibration_arguments, 1, run_vibration},
};

void describe_vibrate(JsonObject& action) {
    action["group"] = "M5Stack StopWatch";
    action["label"] = "Vibrate";
    action["note"] = "Use comma-separated durations, for example 20,100,20. 0 stops vibration; a new pattern replaces the active sequence.";
}

ActionResult run_shutdown(const ActionResolvedArgument*, const char*, uint32_t token) {
    return m5stack_stopwatch_request_shutdown(token) ? ACTION_PENDING : ACTION_FAILED;
}
const ActionOperationDef shutdown_operations[] = {
    {"device.shutdown", "Shutdown", nullptr, 0, run_shutdown, ACTION_EXECUTION_PAUSABLE},
};

void describe_shutdown(JsonObject& action) {
    action["group"] = "M5Stack StopWatch";
    action["label"] = "Shutdown";
    action["max_pending_actions"] = ACTION_CONTINUATION_SLOTS;
}

DEFINE_AND_REGISTER_ACTION_TYPE(kStopWatchChargingActionType, "charging_mode", describe_charging_mode, nullptr, ACTION_EXECUTION_SYNC, false, false, charging_operations, 1);
DEFINE_AND_REGISTER_ACTION_TYPE(kStopWatchStatusLedActionType, "status_led", describe_status_led, nullptr, ACTION_EXECUTION_SYNC, false, false, led_operations, 1);
DEFINE_AND_REGISTER_ACTION_TYPE(kStopWatchVibrateActionType, "vibrate", describe_vibrate, nullptr, ACTION_EXECUTION_SYNC, false, false, vibration_operations, 1);
DEFINE_AND_REGISTER_ACTION_TYPE(kStopWatchShutdownActionType, "shutdown", describe_shutdown, nullptr, ACTION_EXECUTION_PAUSABLE, false, false, shutdown_operations, 1);

}
#endif