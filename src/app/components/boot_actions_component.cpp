// Boot Actions component — migrated from web_portal_boot_actions.cpp

#include "component_registry.h"
#include "action_parse.h"
#include "boot_actions.h"
#include "log_manager.h"
#include "web_portal_cors.h"
#include "web_portal_json.h"

#include <ArduinoJson.h>

static void boot_actions_get_config(AsyncWebServerRequest *request) {
    const BootActionsConfig* cfg = boot_actions_get();

    auto doc = make_psram_json_doc(1024);
    JsonArray arr = doc->createNestedArray("actions");

    for (uint8_t i = 0; cfg && i < cfg->action_count; i++) {
        JsonObject obj = arr.createNestedObject();
        action_to_json(cfg->actions[i], obj);
    }

    web_portal_send_json_chunked(request, doc);
}

static void boot_actions_save_config(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
    component_handle_save_body(request, data, len, index, total, boot_actions_save_raw);
}

static ComponentDef boot_actions_component = {
    .id = "boot-actions",
    .category = "actions",
    .display_name = "Boot Actions",
    .nav_order = 20,
    .get_config = boot_actions_get_config,
    .save_config = nullptr,
    .save_config_body = boot_actions_save_config,
    .delete_config = nullptr,
    .custom_actions = nullptr,
    .num_custom_actions = 0,
    .fragment_id = "boot-actions"
};

REGISTER_COMPONENT(boot_actions);
