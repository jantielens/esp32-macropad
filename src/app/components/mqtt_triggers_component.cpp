// MQTT-Triggered Actions component — exposes the trigger list (topic + value
// filter + action chain) to the web portal.

#include "board_config.h"
#include "mqtt_triggers.h"

#if MQTT_TRIGGERS_ENABLED

#include "action_parse.h"
#include "component_registry.h"
#include "log_manager.h"
#include "psram_json_allocator.h"
#include "web_portal_json.h"

#include <ArduinoJson.h>
#include <string.h>

#define TAG "MqttTrigCmp"

static void mqtt_triggers_get_config(AsyncWebServerRequest *request) {
    auto doc = make_psram_json_doc(MQTT_TRIGGERS_JSON_CAP);
    (*doc)["max"] = MAX_MQTT_TRIGGERS;
    JsonArray triggers = doc->createNestedArray("triggers");

    for (uint8_t i = 0; i < MAX_MQTT_TRIGGERS; i++) {
        const MqttTriggerConfig* cfg = mqtt_triggers_get(i);
        if (!cfg || !cfg->topic[0]) continue;  // omit empty slots

        JsonObject t = triggers.createNestedObject();
        t["topic"] = cfg->topic;
        t["value"] = cfg->value;
        JsonArray actions = t.createNestedArray("actions");
        for (uint8_t a = 0; a < cfg->action_count; a++) {
            action_to_json(cfg->actions[a], actions.createNestedObject());
        }
    }

    web_portal_send_json_chunked(request, doc);
}

static void mqtt_triggers_save_config(AsyncWebServerRequest *request, uint8_t *data,
                                      size_t len, size_t index, size_t total) {
    // Body arrives in a single chunk for this small payload.
    if (index + len < total) return;

    if (total > MQTT_TRIGGERS_JSON_CAP) {
        web_portal_send_json_error(request, 413, "Payload too large");
        return;
    }

    // Validate before persisting: reject wildcard topics and over-count.
    BasicJsonDocument<PsramJsonAllocator> doc(MQTT_TRIGGERS_JSON_CAP);
    DeserializationError err = deserializeJson(doc, data, total);
    if (err) {
        web_portal_send_json_error(request, 400, "Invalid JSON");
        return;
    }

    JsonArray triggers = doc["triggers"].as<JsonArray>();
    if (triggers.isNull()) {
        web_portal_send_json_error(request, 400, "Missing 'triggers' array");
        return;
    }
    if (triggers.size() > MAX_MQTT_TRIGGERS) {
        web_portal_send_json_error(request, 400, "Too many triggers");
        return;
    }
    for (JsonObject t : triggers) {
        const char* topic = t["topic"] | "";
        if (strchr(topic, '#') || strchr(topic, '+')) {
            web_portal_send_json_error(request, 400,
                "Wildcard topics are not supported. Use exact topic names.");
            return;
        }
    }

    if (mqtt_triggers_save_raw(data, total)) {
        request->send(200, "application/json", "{\"success\":true}");
    } else {
        web_portal_send_json_error(request, 500, "Save failed");
    }
}

static ComponentDef mqtt_triggers_component = {
    .id = "mqtt-triggers",
    .category = "actions",
    .display_name = "MQTT Triggers",
    .nav_order = 16,  // between hw-buttons (15) and boot (20)
    .get_config = mqtt_triggers_get_config,
    .save_config = nullptr,
    .save_config_body = mqtt_triggers_save_config,
    .delete_config = nullptr,
    .custom_actions = nullptr,
    .num_custom_actions = 0,
    .fragment_id = "mqtt-triggers"
};

REGISTER_COMPONENT(mqtt_triggers);

#endif  // MQTT_TRIGGERS_ENABLED
