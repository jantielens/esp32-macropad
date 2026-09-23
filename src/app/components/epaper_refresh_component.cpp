// E-paper presentation controls -- available only to drivers that implement
// the persisted presentation settings contract.
#include "component_registry.h"

#include "action_dispatch.h"
#include "board_config.h"
#include "display_manager.h"
#include "version.h"

namespace {

constexpr const char* kEpaperRefreshPortalScript =
    "/portal-epaper-refresh.js?v=" FIRMWARE_VERSION;

void epaper_refresh_full_refresh_post(AsyncWebServerRequest* request) {
    ButtonAction action = {};
    strlcpy(action.type, ACTION_TYPE_DISPLAY_REFRESH, sizeof(action.type));
    strlcpy(action.payload.display_refresh.mode, "full", sizeof(action.payload.display_refresh.mode));
    const ActionResult result = action_dispatch(action, "Portal", 0);
    if (result == ACTION_COMPLETE) {
        request->send(202, "application/json", "{\"success\":true,\"message\":\"Full refresh queued\"}");
    } else {
        request->send(503, "application/json", "{\"success\":false,\"message\":\"Full refresh unavailable\"}");
    }
}

const ComponentAction epaper_refresh_actions[] = {
    {"full-refresh", HTTP_POST, epaper_refresh_full_refresh_post, nullptr},
};

ComponentDef epaper_refresh_component = {
    .id = "epaper-refresh",
    .category = "display",
    .display_name = "E-paper",
    .nav_order = 12,
    .get_config = nullptr,
    .save_config = nullptr,
    .save_config_body = nullptr,
    .delete_config = nullptr,
    .custom_actions = epaper_refresh_actions,
    .num_custom_actions = sizeof(epaper_refresh_actions) / sizeof(epaper_refresh_actions[0]),
    .fragment_id = "epaper-refresh",
    .portal_script = kEpaperRefreshPortalScript,
    .portal_style = nullptr,
};

} // namespace

REGISTER_COMPONENT(epaper_refresh);