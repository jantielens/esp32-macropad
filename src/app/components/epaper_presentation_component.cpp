// E-paper presentation controls -- available only to drivers that implement
// the persisted presentation settings contract.
#include "component_registry.h"

#include "action_dispatch.h"
#include "board_config.h"
#include "display_manager.h"
#include "version.h"

namespace {

constexpr const char* kEpaperPresentationPortalScript =
    "/portal-epaper-presentation.js?v=" FIRMWARE_VERSION;

void epaper_presentation_full_refresh_post(AsyncWebServerRequest* request) {
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

const ComponentAction epaper_presentation_actions[] = {
    {"full-refresh", HTTP_POST, epaper_presentation_full_refresh_post, nullptr},
};

ComponentDef epaper_presentation_component = {
    .id = "epaper-presentation",
    .category = "display",
    .display_name = "E-paper",
    .nav_order = 12,
    .get_config = nullptr,
    .save_config = nullptr,
    .save_config_body = nullptr,
    .delete_config = nullptr,
    .custom_actions = epaper_presentation_actions,
    .num_custom_actions = sizeof(epaper_presentation_actions) / sizeof(epaper_presentation_actions[0]),
    .fragment_id = "epaper-presentation",
    .portal_script = kEpaperPresentationPortalScript,
    .portal_style = nullptr,
};

} // namespace

REGISTER_COMPONENT(epaper_presentation);