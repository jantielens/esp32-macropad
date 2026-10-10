#include "action_registry.h"
#include "stopwatch_hardware.h"
#include <string.h>

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
    action["group"] = "StopWatch";
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

DEFINE_AND_REGISTER_ACTION_TYPE(kStopWatchChargingActionType, "charging_mode",
    parse_charging_mode, serialize_charging_mode, dispatch_charging_mode, nullptr,
    describe_charging_mode, nullptr, validate_charging_mode, nullptr, ACTION_EXECUTION_SYNC, false);

}
#endif