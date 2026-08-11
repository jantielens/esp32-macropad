#include "action_registry.h"
#include "log_manager.h"
#include "pad_cycle.h"
#if defined(ARDUINO) && HAS_DISPLAY
#include "display_manager.h"
#endif

#if HAS_DISPLAY || HAS_BUTTON
namespace {
constexpr const char* kCyclePadActionTag = "Action";
void parse_cycle_pad(const JsonObject& action, ButtonAction& act) {
    if ((action.containsKey("direction") && !action["direction"].is<const char*>()) ||
        (action.containsKey("wrap") && !action["wrap"].is<bool>()) ||
        (action.containsKey("excluded_pads") && !action["excluded_pads"].is<const char*>())) { memset(&act, 0, sizeof(act)); return; }
    const char* direction = action["direction"] | "next";
    if (strcmp(direction, "next") != 0 && strcmp(direction, "previous") != 0) { memset(&act, 0, sizeof(act)); return; }
    act.payload.cycle_pad.direction = strcmp(direction, "previous") == 0 ? -1 : 1;
    act.payload.cycle_pad.wrap = action["wrap"] | true;
    act.payload.cycle_pad.excluded_mask = pad_cycle_parse_exclusions(action["excluded_pads"] | "");
}
void serialize_cycle_pad(const ButtonAction& act, JsonObject action) {
    action["direction"] = act.payload.cycle_pad.direction < 0 ? "previous" : "next";
    action["wrap"] = act.payload.cycle_pad.wrap;
    char exclusions[MAX_PADS * 3 + 1]; pad_cycle_format_exclusions(act.payload.cycle_pad.excluded_mask, exclusions, sizeof(exclusions));
    if (exclusions[0]) action["excluded_pads"] = exclusions;
}
ActionResult dispatch_cycle_pad(const ButtonAction& act, const char* label, uint32_t) {
#if defined(ARDUINO) && HAS_DISPLAY
    const auto& cycle = act.payload.cycle_pad;
    if (!display_manager_cycle_pad(cycle.direction, cycle.wrap, cycle.excluded_mask)) LOGD(kCyclePadActionTag, "%s cycle_pad: no eligible destination", label);
#else
    (void)act;
    LOGW(kCyclePadActionTag, "%s cycle_pad: no display", label);
#endif
    return ACTION_COMPLETE;
}
bool cycle_pad_available() { return HAS_DISPLAY; }
const char* validate_cycle_pad(const JsonObjectConst action) {
    if (action.containsKey("direction")) { if (!action["direction"].is<const char*>()) return "cycle_pad direction must be a string"; const char* direction = action["direction"].as<const char*>(); if (strcmp(direction, "next") && strcmp(direction, "previous")) return "cycle_pad direction must be 'next' or 'previous'"; }
    if (action.containsKey("wrap") && !action["wrap"].is<bool>()) return "cycle_pad wrap must be boolean";
    return action.containsKey("excluded_pads") && !action["excluded_pads"].is<const char*>() ? "cycle_pad excluded_pads must be a string" : nullptr;
}
void describe_cycle_pad(JsonObject& action) { action["group"] = "Navigation"; action["label"] = "Navigate pad sequence"; JsonArray fields = action.createNestedArray("fields"); JsonObject direction = fields.createNestedObject(); direction["name"] = "direction"; direction["description"] = "next or previous (default next)"; JsonObject wrap = fields.createNestedObject(); wrap["name"] = "wrap"; wrap["description"] = "boolean, default true"; }
DEFINE_AND_REGISTER_ACTION_TYPE(kCyclePadActionType, ACTION_TYPE_CYCLE_PAD, parse_cycle_pad, serialize_cycle_pad, dispatch_cycle_pad, nullptr, describe_cycle_pad, cycle_pad_available, validate_cycle_pad);
} // namespace
#endif // HAS_DISPLAY || HAS_BUTTON