#include "action_registry.h"
#include "log_manager.h"

#if defined(ARDUINO) && HAS_DISPLAY
#include "display_manager.h"
#endif

#if HAS_DISPLAY || HAS_BUTTON
namespace {

constexpr const char* kDisplayRefreshActionTag = "Action";

ActionResult dispatch_full_refresh(const ActionResolvedArgument*, const char* label, uint32_t) {
#if defined(ARDUINO) && HAS_LVGL_EPAPER
    const DisplayRefreshRequestResult result = display_manager_request_full_refresh();
    if (result == DISPLAY_REFRESH_QUEUED) {
        LOGI(kDisplayRefreshActionTag, "%s display_refresh: queued full waveform", label);
        return ACTION_COMPLETE;
    }
    if (result == DISPLAY_REFRESH_UNAVAILABLE) {
        LOGW(kDisplayRefreshActionTag, "%s display_refresh: unavailable", label);
    } else {
        LOGW(kDisplayRefreshActionTag, "%s display_refresh: failed to queue", label);
    }
    return ACTION_FAILED;
#else
    LOGW(kDisplayRefreshActionTag, "%s display_refresh: unsupported", label);
    return ACTION_FAILED;
#endif
}

const ActionOperationDef kDisplayRefreshOperations[] = {
    {"display.refresh_full", "E-paper full refresh", nullptr, 0, dispatch_full_refresh},
};

bool display_refresh_available() {
    return HAS_DISPLAY && HAS_LVGL_EPAPER;
}

void describe_display_refresh(JsonObject& action) {
    action["group"] = "Display";
    action["label"] = "E-paper full refresh";
}

DEFINE_AND_REGISTER_ACTION_TYPE(kDisplayRefreshActionType, ACTION_TYPE_DISPLAY_REFRESH, describe_display_refresh, display_refresh_available, ACTION_EXECUTION_SYNC, true, false, kDisplayRefreshOperations, 1);

} // namespace
#endif // HAS_DISPLAY || HAS_BUTTON