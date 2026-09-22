#include "action_registry.h"
#include "log_manager.h"

#if defined(ARDUINO) && HAS_DISPLAY
#include "display_manager.h"
#endif

#if HAS_DISPLAY || HAS_BUTTON
namespace {

constexpr const char* kDisplayRefreshActionTag = "Action";

void parse_display_refresh(const JsonObject& action, ButtonAction& act) {
    strlcpy(act.payload.display_refresh.mode, action["mode"] | "",
            sizeof(act.payload.display_refresh.mode));
}

void serialize_display_refresh(const ButtonAction& act, JsonObject action) {
    if (act.payload.display_refresh.mode[0]) action["mode"] = act.payload.display_refresh.mode;
}

ActionResult dispatch_display_refresh(const ButtonAction& act, const char* label, uint32_t) {
#if defined(ARDUINO) && HAS_EPAPER_PRESENTATION
    if (display_manager_request_full_refresh()) {
        LOGI(kDisplayRefreshActionTag, "%s display_refresh: queued full waveform", label);
    } else {
        LOGW(kDisplayRefreshActionTag, "%s display_refresh: unavailable", label);
    }
#else
    (void)act;
    LOGW(kDisplayRefreshActionTag, "%s display_refresh: unsupported", label);
#endif
    return ACTION_COMPLETE;
}

bool display_refresh_available() {
    return HAS_DISPLAY && HAS_EPAPER_PRESENTATION;
}

const char* validate_display_refresh(const JsonObjectConst action) {
    if (!action.containsKey("mode") || !action["mode"].is<const char*>()) return "display_refresh mode must be full";
    return strcmp(action["mode"].as<const char*>(), "full") == 0 ? nullptr : "display_refresh mode must be full";
}

void describe_display_refresh(JsonObject& action) {
    action["group"] = "Display";
    action["label"] = "E-paper full refresh";
    JsonArray commands = action.createNestedArray("commands");
    JsonObject full = commands.createNestedObject(); full["id"] = "full"; full["label"] = "E-paper full refresh";
    JsonArray fields = action.createNestedArray("fields");
    JsonObject mode = fields.createNestedObject(); mode["name"] = "mode"; mode["description"] = "full";
    JsonArray editor_fields = action.createNestedArray("editor_fields");
    JsonObject editor_mode = editor_fields.createNestedObject(); editor_mode["name"] = "mode"; editor_mode["label"] = "Mode"; editor_mode["type"] = "select"; editor_mode["command_options"] = true;
}

DEFINE_AND_REGISTER_ACTION_TYPE(kDisplayRefreshActionType, ACTION_TYPE_DISPLAY_REFRESH,
    parse_display_refresh, serialize_display_refresh, dispatch_display_refresh, nullptr,
    describe_display_refresh, display_refresh_available, validate_display_refresh, nullptr);

} // namespace
#endif // HAS_DISPLAY || HAS_BUTTON