#include "action_registry.h"
#include "log_manager.h"
#if defined(ARDUINO) && HAS_DISPLAY
#include "message_bubble.h"
#endif

#if HAS_DISPLAY || HAS_BUTTON
namespace {
constexpr const char* kNotifyActionTag = "Action";
void parse_notify(const JsonObject& action, ButtonAction& act) { strlcpy(act.payload.notify.notify_text, action["notify_text"] | "", sizeof(act.payload.notify.notify_text)); strlcpy(act.payload.notify.notify_duration_ms, action["notify_duration_ms"] | "", sizeof(act.payload.notify.notify_duration_ms)); strlcpy(act.payload.notify.notify_text_color, action["notify_text_color"] | "", sizeof(act.payload.notify.notify_text_color)); strlcpy(act.payload.notify.notify_bg_color, action["notify_bg_color"] | "", sizeof(act.payload.notify.notify_bg_color)); strlcpy(act.payload.notify.notify_border_color, action["notify_border_color"] | "", sizeof(act.payload.notify.notify_border_color)); act.payload.notify.notify_opacity = action["notify_opacity"] | 0; act.payload.notify.notify_font_size = action["notify_font_size"] | 0; strlcpy(act.payload.notify.notify_location, action["notify_location"] | "", sizeof(act.payload.notify.notify_location)); }
void serialize_notify(const ButtonAction& act, JsonObject action) { if (act.payload.notify.notify_text[0]) action["notify_text"] = act.payload.notify.notify_text; if (act.payload.notify.notify_duration_ms[0]) action["notify_duration_ms"] = act.payload.notify.notify_duration_ms; if (act.payload.notify.notify_text_color[0]) action["notify_text_color"] = act.payload.notify.notify_text_color; if (act.payload.notify.notify_bg_color[0]) action["notify_bg_color"] = act.payload.notify.notify_bg_color; if (act.payload.notify.notify_border_color[0]) action["notify_border_color"] = act.payload.notify.notify_border_color; if (act.payload.notify.notify_opacity) action["notify_opacity"] = act.payload.notify.notify_opacity; if (act.payload.notify.notify_font_size) action["notify_font_size"] = act.payload.notify.notify_font_size; if (act.payload.notify.notify_location[0]) action["notify_location"] = act.payload.notify.notify_location; }
ActionResult dispatch_notify(const ButtonAction& act, const char* label, uint32_t) {
#if defined(ARDUINO) && HAS_DISPLAY
    const auto& notify = act.payload.notify; MessageBubbleParams params = {}; strlcpy(params.text, notify.notify_text, sizeof(params.text));
    if (!params.text[0]) { message_bubble_dismiss(); LOGI(kNotifyActionTag, "%s notify: dismiss", label); }
    else { params.duration_ms = (uint16_t)atoi(notify.notify_duration_ms[0] ? notify.notify_duration_ms : "3000"); if (!parse_hex_color(notify.notify_text_color[0] ? notify.notify_text_color : "#ffffff", &params.text_color)) params.text_color = 0xFFFFFF; if (!parse_hex_color(notify.notify_bg_color[0] ? notify.notify_bg_color : "#333333", &params.bg_color)) params.bg_color = 0x333333; if (notify.notify_border_color[0]) params.has_border = parse_hex_color(notify.notify_border_color, &params.border_color); params.opacity = notify.notify_opacity; params.font_size = notify.notify_font_size; params.location = notify_location_from_str(notify.notify_location); message_bubble_show(&params); LOGI(kNotifyActionTag, "%s notify: '%s' dur=%u loc=%s", label, params.text, params.duration_ms, notify.notify_location[0] ? notify.notify_location : "bottom"); }
#else
    (void)act;
    LOGW(kNotifyActionTag, "%s notify: no display", label);
#endif
    return ACTION_COMPLETE;
}
bool notify_available() { return HAS_DISPLAY; }
const char* validate_notify(const JsonObjectConst action) { return action.containsKey("notify_text") && !action["notify_text"].is<const char*>() ? "notify_text must be a string" : nullptr; }
bool visit_notify_fields(ButtonAction& act, ActionBindableFieldVisitor visitor, void* context) { return (!act.payload.notify.notify_text[0] || visitor(act.payload.notify.notify_text, sizeof(act.payload.notify.notify_text), false, context)) && (!act.payload.notify.notify_duration_ms[0] || visitor(act.payload.notify.notify_duration_ms, sizeof(act.payload.notify.notify_duration_ms), false, context)) && (!act.payload.notify.notify_text_color[0] || visitor(act.payload.notify.notify_text_color, sizeof(act.payload.notify.notify_text_color), false, context)) && (!act.payload.notify.notify_bg_color[0] || visitor(act.payload.notify.notify_bg_color, sizeof(act.payload.notify.notify_bg_color), false, context)) && (!act.payload.notify.notify_border_color[0] || visitor(act.payload.notify.notify_border_color, sizeof(act.payload.notify.notify_border_color), false, context)); }
void describe_notify(JsonObject& action) { action["group"] = "Display"; action["label"] = "Show notification"; JsonArray fields = action.createNestedArray("fields"); JsonObject text = fields.createNestedObject(); text["name"] = "notify_text"; text["description"] = "message text, bindable"; JsonObject duration = fields.createNestedObject(); duration["name"] = "notify_duration_ms"; duration["description"] = "display duration in milliseconds, bindable"; }
DEFINE_AND_REGISTER_ACTION_TYPE(kNotifyActionType, ACTION_TYPE_NOTIFY, parse_notify, serialize_notify, dispatch_notify, nullptr, describe_notify, notify_available, validate_notify, visit_notify_fields);
} // namespace
#endif // HAS_DISPLAY || HAS_BUTTON