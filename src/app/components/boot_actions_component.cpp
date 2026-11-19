// Boot Actions component — migrated from web_portal_boot_actions.cpp

#include "component_registry.h"
#include "action_parse.h"
#include "boot_actions.h"
#include "log_manager.h"
#include "web_portal_cors.h"
#include "web_portal_json.h"

#include <ArduinoJson.h>

static void boot_actions_get_config(AsyncWebServerRequest *request) {
    auto doc = make_psram_json_doc(4096);
    (*doc)["action_format_version"] = 1;
    JsonArray arr = doc->createNestedArray("actions");
    bool config_reset = false;
    const bool available = boot_actions_to_json(arr, &config_reset);
    (*doc)["config_reset"] = config_reset;
    if (!available || doc->overflowed()) {
        request->send(409, "application/json", "{\"error\":\"Boot actions unavailable or incompatible; recreate the document\"}");
        return;
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
