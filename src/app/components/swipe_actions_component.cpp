// Swipe Actions component — migrated from web_portal_swipe.cpp

#include "component_registry.h"
#include "action_parse.h"
#include "board_config.h"
#include "log_manager.h"
#include "swipe_config.h"
#include "web_portal_cors.h"

#include <ArduinoJson.h>

static void swipe_actions_get_config(AsyncWebServerRequest *request) {
    const SwipeConfig* cfg = swipe_config_get();

    StaticJsonDocument<1024> doc;
    JsonObject left  = doc.createNestedObject("swipe_left");
    JsonObject right = doc.createNestedObject("swipe_right");
    JsonObject up    = doc.createNestedObject("swipe_up");
    JsonObject down  = doc.createNestedObject("swipe_down");

    action_to_json(cfg->swipe_left,  left);
    action_to_json(cfg->swipe_right, right);
    action_to_json(cfg->swipe_up,    up);
    action_to_json(cfg->swipe_down,  down);

    String output;
    serializeJson(doc, output);
    request->send(200, "application/json", output);
}

static void swipe_actions_save_config(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
    component_handle_save_body(request, data, len, index, total, swipe_config_save_raw);
}

static ComponentDef swipe_actions_component = {
    .id = "swipe-actions",
    .category = "actions",
    .display_name = "Swipe Actions",
    .nav_order = 10,
    .get_config = swipe_actions_get_config,
    .save_config = nullptr,
    .save_config_body = swipe_actions_save_config,
    .delete_config = nullptr,
    .custom_actions = nullptr,
    .num_custom_actions = 0,
    .fragment_id = "swipe-actions"
};

REGISTER_COMPONENT(swipe_actions);
