// Shutter Session Save Actions component — lifecycle hooks for shutter session persistence

#include "board_config.h"

#if IS_SHUTTER_TESTER

#include "component_registry.h"
#include "action_parse.h"
#include "../shutter_session_actions.h"
#include "web_portal_json.h"

#include <ArduinoJson.h>

static void shutter_session_actions_get_config(AsyncWebServerRequest *request) {
    const ShutterSessionActionsConfig* cfg = shutter_session_actions_get();

    auto doc = make_psram_json_doc(2048);
    JsonArray start_arr    = doc->createNestedArray("save_start_actions");
    JsonArray complete_arr = doc->createNestedArray("save_complete_actions");

    for (uint8_t i = 0; i < cfg->save_start_count; i++) {
        JsonObject obj = start_arr.createNestedObject();
        action_to_json(cfg->save_start_actions[i], obj);
    }
    for (uint8_t i = 0; i < cfg->save_complete_count; i++) {
        JsonObject obj = complete_arr.createNestedObject();
        action_to_json(cfg->save_complete_actions[i], obj);
    }

    web_portal_send_json_chunked(request, doc);
}

static void shutter_session_actions_save_config(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
    component_handle_save_body(request, data, len, index, total, shutter_session_actions_save_raw);
}

static ComponentDef shutter_session_actions_component = {
    .id = "shutter-session-actions",
    .category = "camera",
    .display_name = "Session Save Actions",
    .nav_order = 13,
    .get_config = shutter_session_actions_get_config,
    .save_config = nullptr,
    .save_config_body = shutter_session_actions_save_config,
    .delete_config = nullptr,
    .custom_actions = nullptr,
    .num_custom_actions = 0,
    .fragment_id = "shutter-session-actions"
};

REGISTER_COMPONENT(shutter_session_actions);

#endif // IS_SHUTTER_TESTER
