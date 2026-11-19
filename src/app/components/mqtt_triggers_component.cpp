// MQTT-Triggered Actions component — exposes the trigger list (topic + value
// filter + action chain) to the web portal.

#include "board_config.h"
#include "mqtt_triggers.h"

#if MQTT_TRIGGERS_ENABLED

#include "component_registry.h"
#include "psram_json_allocator.h"
#include "web_portal_json.h"

#include <ArduinoJson.h>
static void mqtt_triggers_get_config(AsyncWebServerRequest *request) {
    auto doc = make_psram_json_doc(MQTT_TRIGGERS_JSON_CAP);
    (*doc)["action_format_version"] = 1;
    (*doc)["max"] = MAX_MQTT_TRIGGERS;
    JsonArray triggers = doc->createNestedArray("triggers");
    bool config_reset = false;
    const bool available = mqtt_triggers_to_json(triggers, &config_reset);
    (*doc)["config_reset"] = config_reset;
    if (!available || doc->overflowed()) {
        web_portal_send_json_error(request, 409, "MQTT triggers unavailable or incompatible; recreate the document");
        return;
    }

    web_portal_send_json_chunked(request, doc);
}

static void mqtt_triggers_save_config(AsyncWebServerRequest *request, uint8_t *data,
                                      size_t len, size_t index, size_t total) {
    component_handle_save_body(request, data, len, index, total,
                               mqtt_triggers_save_raw, MQTT_TRIGGERS_JSON_CAP);
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
