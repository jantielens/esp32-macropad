// Timers component — migrated from web_portal_timers.cpp

#include "component_registry.h"
#include "action_parse.h"
#include "board_config.h"
#include "log_manager.h"
#include "timer_config.h"
#include "web_portal_cors.h"
#include "web_portal_json.h"

#include <ArduinoJson.h>

static void timers_get_config(AsyncWebServerRequest *request) {
    auto doc = make_psram_json_doc(4096);
    timer_config_to_json(doc->to<JsonObject>());

    web_portal_send_json_chunked(request, doc);
}

static void timers_save_config(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
    component_handle_save_body(request, data, len, index, total, timer_config_save_raw);
}

static ComponentDef timers_component = {
    .id = "timers",
    .category = "actions",
    .display_name = "Timers",
    .nav_order = 30,
    .get_config = timers_get_config,
    .save_config = nullptr,
    .save_config_body = timers_save_config,
    .delete_config = nullptr,
    .custom_actions = nullptr,
    .num_custom_actions = 0,
    .fragment_id = "timers"
};

REGISTER_COMPONENT(timers);
