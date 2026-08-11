#include "action_registry.h"
#include "log_manager.h"
#if defined(ARDUINO) && HAS_DISPLAY
#include "display_manager.h"
#endif

#if HAS_DISPLAY || HAS_BUTTON
namespace {
constexpr const char* kScreenActionTag = "Action";
void parse_screen(const JsonObject& action, ButtonAction& act) { strlcpy(act.payload.screen.screen_id, action["target"] | "", sizeof(act.payload.screen.screen_id)); }
void serialize_screen(const ButtonAction& act, JsonObject action) { if (act.payload.screen.screen_id[0]) action["target"] = act.payload.screen.screen_id; }
ActionResult dispatch_screen(const ButtonAction& act, const char* label, uint32_t) {
#if defined(ARDUINO) && HAS_DISPLAY
    if (act.payload.screen.screen_id[0]) { bool ok = false; display_manager_show_screen(act.payload.screen.screen_id, &ok); if (!ok) LOGW(kScreenActionTag, "%s nav failed: '%s'", label, act.payload.screen.screen_id); }
#else
    (void)act;
    LOGW(kScreenActionTag, "%s screen: no display", label);
#endif
    return ACTION_COMPLETE;
}
char* screen_value_field(ButtonAction& act, size_t* size) { *size = sizeof(act.payload.screen.screen_id); return act.payload.screen.screen_id; }
bool screen_available() { return HAS_DISPLAY; }
const char* validate_screen(const JsonObjectConst action) { return action.containsKey("target") && !action["target"].is<const char*>() ? "screen target must be a string" : nullptr; }
void describe_screen(JsonObject& action) { action["group"] = "Navigation"; action["label"] = "Navigate to screen"; JsonArray fields = action.createNestedArray("fields"); JsonObject target = fields.createNestedObject(); target["name"] = "target"; target["description"] = "screen id to navigate to"; }
DEFINE_AND_REGISTER_ACTION_TYPE(kScreenActionType, ACTION_TYPE_SCREEN, parse_screen, serialize_screen, dispatch_screen, screen_value_field, describe_screen, screen_available, validate_screen);
} // namespace
#endif // HAS_DISPLAY || HAS_BUTTON