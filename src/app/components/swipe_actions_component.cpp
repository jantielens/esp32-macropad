// Swipe Actions component — migrated from web_portal_swipe.cpp

#include "component_registry.h"
#include "board_config.h"
#include "log_manager.h"
#include "swipe_config.h"
#include "web_portal_cors.h"
#include "web_portal_json.h"

#include <ArduinoJson.h>

static void swipe_actions_get_config(AsyncWebServerRequest *request) {
    auto doc = make_psram_json_doc(4096);
    if (!swipe_config_to_json(doc->to<JsonObject>()) || doc->overflowed()) {
        request->send(409, "application/json", "{\"error\":\"Swipe actions unavailable or incompatible; recreate the document\"}");
        return;
    }

    web_portal_send_json_chunked(request, doc);
}

static void swipe_actions_save_config(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
    component_handle_save_body(request, data, len, index, total, swipe_config_save_raw);
}

static ComponentDef swipe_actions_component = {
    .id = "swipe-actions",
    .category = "actions",
    .display_name = "Swipes",
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
