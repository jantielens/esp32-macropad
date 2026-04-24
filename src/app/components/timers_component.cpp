// Timers component — migrated from web_portal_timers.cpp

#include "component_registry.h"
#include "action_parse.h"
#include "board_config.h"
#include "log_manager.h"
#include "timer_config.h"
#include "web_portal_auth.h"
#include "web_portal_cors.h"

#include <ArduinoJson.h>

static void timers_get_config(AsyncWebServerRequest *request) {
    const TimerConfig* cfg = timer_config_get();

    StaticJsonDocument<3072> doc;

    for (uint8_t i = 0; i < TIMER_COUNT; i++) {
        char key[4];
        snprintf(key, sizeof(key), "%u", i + 1);
        JsonObject tobj = doc.createNestedObject(key);

        const TimerSettings& ts = cfg->timers[i];
        tobj["mode"] = (ts.mode == TIMER_MODE_DOWN) ? "down" : "up";
        if (ts.countdown > 0) tobj["countdown"] = ts.countdown;

        if (ts.expire_action_count > 0) {
            JsonArray arr = tobj.createNestedArray("expire_actions");
            for (uint8_t a = 0; a < ts.expire_action_count; a++) {
                JsonObject aobj = arr.createNestedObject();
                action_to_json(ts.expire_actions[a], aobj);
            }
        }
    }

    String output;
    serializeJson(doc, output);
    request->send(200, "application/json", output);
}

static void timers_save_config(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
    if (!portal_auth_gate(request)) return;

    // Only handle final chunk (small payload, arrives in one piece)
    if (index + len < total) return;

    if (total > 4096) {
        request->send(413, "application/json", "{\"error\":\"payload too large\"}");
        return;
    }

    bool ok = timer_config_save_raw(data, total);
    if (ok) {
        request->send(200, "application/json", "{\"ok\":true}");
    } else {
        request->send(500, "application/json", "{\"error\":\"save failed\"}");
    }
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
