#include "action_registry.h"
#include "log_manager.h"
#include <limits.h>
#if defined(ARDUINO) && HAS_DISPLAY
#include "screen_saver_manager.h"
#include "visual_alert.h"
#endif

#if HAS_DISPLAY || HAS_BUTTON
namespace {
constexpr const char* kVisualAlertActionTag = "Action";
void parse_visual_alert(const JsonObject& action, ButtonAction& act) { strlcpy(act.payload.visual_alert.va_op, action["op"] | "", sizeof(act.payload.visual_alert.va_op)); strlcpy(act.payload.visual_alert.va_color, action["color"] | "", sizeof(act.payload.visual_alert.va_color)); strlcpy(act.payload.visual_alert.va_pattern, action["pattern"] | "", sizeof(act.payload.visual_alert.va_pattern)); act.payload.visual_alert.va_period_ms = action["period_ms"] | 0; act.payload.visual_alert.va_intensity = action["intensity"] | 0; act.payload.visual_alert.va_duration_ms = action["duration_ms"] | 0; }
void serialize_visual_alert(const ButtonAction& act, JsonObject action) { if (act.payload.visual_alert.va_op[0]) action["op"] = act.payload.visual_alert.va_op; if (act.payload.visual_alert.va_color[0]) action["color"] = act.payload.visual_alert.va_color; if (act.payload.visual_alert.va_pattern[0]) action["pattern"] = act.payload.visual_alert.va_pattern; if (act.payload.visual_alert.va_period_ms) action["period_ms"] = act.payload.visual_alert.va_period_ms; if (act.payload.visual_alert.va_intensity) action["intensity"] = act.payload.visual_alert.va_intensity; if (act.payload.visual_alert.va_duration_ms) action["duration_ms"] = act.payload.visual_alert.va_duration_ms; }
ActionResult dispatch_visual_alert(const ButtonAction& act, const char* label, uint32_t) {
#if defined(ARDUINO) && HAS_DISPLAY
    const auto& alert = act.payload.visual_alert;
    if (!strcmp(alert.va_op, "stop")) { visual_alert_stop(); LOGI(kVisualAlertActionTag, "%s visual_alert: stop", label); }
    else { screen_saver_manager_notify_activity(true); VisualAlertParams params = {}; if (!parse_hex_color(alert.va_color[0] ? alert.va_color : "#FF0000", &params.color)) params.color = 0xFF0000; params.pattern = visual_alert_pattern_from_str(alert.va_pattern); params.period_ms = alert.va_period_ms ? alert.va_period_ms : VA_DEFAULT_PERIOD_MS; params.intensity = alert.va_intensity ? alert.va_intensity : VA_DEFAULT_INTENSITY; params.duration_ms = alert.va_duration_ms; visual_alert_show(&params); LOGI(kVisualAlertActionTag, "%s visual_alert: start pat=%u per=%u int=%u dur=%u", label, params.pattern, params.period_ms, params.intensity, params.duration_ms); }
#else
    (void)act;
    LOGW(kVisualAlertActionTag, "%s visual_alert: no display", label);
#endif
    return ACTION_COMPLETE;
}
const char* validate_alert_color(const char* value) {
    uint32_t color;
    return !value[0] || parse_hex_color(value, &color) ? nullptr : "visual alert color must be a hex color";
}
const char* const kAlertPatterns[] = {"breathe", "blink", "solid"};
const char* const kAlertPatternLabels[] = {"Breathe", "Blink", "Solid"};
const ActionArgumentDef kVisualAlertArguments[] = {
    {"color", "Color", ACTION_ARGUMENT_STRING, 0, 0, CONFIG_BINDABLE_SHORT_LEN - 1,
     true, "[var:color]", "color", nullptr, 0, true, validate_alert_color},
    {"pattern", "Pattern", ACTION_ARGUMENT_ENUM, 0, 0, 7,
     true, "[var:pattern]", "select", kAlertPatterns, 3, false, nullptr, kAlertPatternLabels},
    {"period_ms", "Period (ms; 0 = default)", ACTION_ARGUMENT_INTEGER, 0, 10000,
     0, true, "[var:period_ms]", "number"},
    {"intensity", "Intensity (%; 0 = default)", ACTION_ARGUMENT_INTEGER, 0, 100,
     0, true, "[var:intensity]", "number"},
    {"duration_ms", "Duration (ms; 0 = until stopped)", ACTION_ARGUMENT_INTEGER, 0, INT32_MAX,
     0, true, "[var:duration_ms]", "number"},
};
ActionResult dispatch_visual_alert_start(const ActionResolvedArgument* arguments, const char* label,
                                         uint32_t token) {
#if defined(ARDUINO) && HAS_DISPLAY
    ButtonAction act = {};
    auto& alert = act.payload.visual_alert;
    strlcpy(alert.va_op, "start", sizeof(alert.va_op));
    strlcpy(alert.va_color, arguments[0].text, sizeof(alert.va_color));
    strlcpy(alert.va_pattern, arguments[1].text, sizeof(alert.va_pattern));
    alert.va_period_ms = static_cast<uint16_t>(arguments[2].integer);
    alert.va_intensity = static_cast<uint16_t>(arguments[3].integer);
    alert.va_duration_ms = static_cast<uint32_t>(arguments[4].integer);
    return dispatch_visual_alert(act, label, token);
#else
    (void)arguments;
    (void)token;
    LOGW(kVisualAlertActionTag, "%s visual_alert: no display", label);
    return ACTION_FAILED;
#endif
}
ActionResult dispatch_visual_alert_stop(const ActionResolvedArgument*, const char* label, uint32_t token) {
#if defined(ARDUINO) && HAS_DISPLAY
    ButtonAction act = {};
    strlcpy(act.payload.visual_alert.va_op, "stop", sizeof(act.payload.visual_alert.va_op));
    return dispatch_visual_alert(act, label, token);
#else
    (void)token;
    LOGW(kVisualAlertActionTag, "%s visual_alert: no display", label);
    return ACTION_FAILED;
#endif
}
const ActionOperationDef kVisualAlertOperations[] = {
    {"visual_alert.start", "Start visual alert", kVisualAlertArguments, 5, dispatch_visual_alert_start},
    {"visual_alert.stop", "Stop visual alert", nullptr, 0, dispatch_visual_alert_stop},
};
bool visual_alert_available() { return HAS_DISPLAY; }
const char* validate_visual_alert(const JsonObjectConst action) { if (action.containsKey("op")) { if (!action["op"].is<const char*>()) return "visual_alert op must be a string"; const char* op = action["op"].as<const char*>(); if (strcmp(op, "start") && strcmp(op, "stop")) return "visual_alert op must be start or stop"; } return nullptr; }
bool visit_visual_alert_fields(ButtonAction& act, ActionFieldVisitor visitor, void* context) {
    return action_visit_field(visitor, context, "va_color", act.payload.visual_alert.va_color, ACTION_FIELD_BINDING | ACTION_FIELD_EVENT);
}
void describe_visual_alert(JsonObject& action) { action["group"] = "Display"; action["label"] = "Visual alert"; JsonArray commands = action.createNestedArray("commands"); JsonObject start = commands.createNestedObject(); start["id"] = "start"; start["label"] = "Start alert"; JsonObject stop = commands.createNestedObject(); stop["id"] = "stop"; stop["label"] = "Stop alert"; }
DEFINE_AND_REGISTER_ACTION_TYPE(kVisualAlertActionType, ACTION_TYPE_VISUAL_ALERT, parse_visual_alert, serialize_visual_alert, dispatch_visual_alert, visit_visual_alert_fields, describe_visual_alert, visual_alert_available, validate_visual_alert, ACTION_EXECUTION_SYNC, true, true, false, nullptr, kVisualAlertOperations, 2);
} // namespace
#endif // HAS_DISPLAY || HAS_BUTTON