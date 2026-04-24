// Boot Actions component — migrated from web_portal_boot_actions.cpp

#include "component_registry.h"
#include "action_parse.h"
#include "boot_actions.h"
#include "log_manager.h"
#include "web_portal_auth.h"
#include "web_portal_cors.h"

#include <ArduinoJson.h>

static void boot_actions_get_config(AsyncWebServerRequest *request) {
    const BootActionsConfig* cfg = boot_actions_get();

    StaticJsonDocument<1024> doc;
    JsonArray arr = doc.createNestedArray("actions");

    for (uint8_t i = 0; i < cfg->action_count; i++) {
        JsonObject obj = arr.createNestedObject();
        action_to_json(cfg->actions[i], obj);
    }

    String output;
    serializeJson(doc, output);
    request->send(200, "application/json", output);
}

static void boot_actions_save_config(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
    if (!portal_auth_gate(request)) return;

    // Only handle final chunk (small payload, arrives in one piece)
    if (index + len < total) return;

    if (total > 4096) {
        request->send(413, "application/json", "{\"error\":\"payload too large\"}");
        return;
    }

    bool ok = boot_actions_save_raw(data, total);
    if (ok) {
        request->send(200, "application/json", "{\"success\":true}");
    } else {
        request->send(500, "application/json", "{\"success\":false,\"error\":\"save failed\"}");
    }
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
