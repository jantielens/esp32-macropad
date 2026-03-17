#include "web_portal_swipe.h"

#if HAS_DISPLAY

#include "board_config.h"
#include "log_manager.h"
#include "swipe_config.h"
#include "web_portal_auth.h"
#include "web_portal_cors.h"

#include <ArduinoJson.h>

#define TAG "SwipeAPI"

// Serialize a ButtonAction into a JSON object
static void action_to_json(JsonObject obj, const ButtonAction& act) {
    if (!act.type[0]) return;  // empty action → empty object
    obj["type"] = act.type;
    if (act.screen_id[0])    obj["target"]   = act.screen_id;
    if (act.mqtt_topic[0])   obj["topic"]    = act.mqtt_topic;
    if (act.mqtt_payload[0]) obj["payload"]  = act.mqtt_payload;
    if (act.key_sequence[0]) obj["sequence"] = act.key_sequence;
}

void handleGetSwipeActions(AsyncWebServerRequest *request) {
    const SwipeConfig* cfg = swipe_config_get();

    StaticJsonDocument<1024> doc;
    JsonObject left  = doc.createNestedObject("swipe_left");
    JsonObject right = doc.createNestedObject("swipe_right");
    JsonObject up    = doc.createNestedObject("swipe_up");
    JsonObject down  = doc.createNestedObject("swipe_down");

    action_to_json(left,  cfg->swipe_left);
    action_to_json(right, cfg->swipe_right);
    action_to_json(up,    cfg->swipe_up);
    action_to_json(down,  cfg->swipe_down);

    String output;
    serializeJson(doc, output);
    request->send(200, "application/json", output);
}

void handlePostSwipeActions(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
    if (!portal_auth_gate(request)) return;

    // Only handle final chunk (small payload, arrives in one piece)
    if (index + len < total) return;

    if (total > 4096) {
        request->send(413, "application/json", "{\"error\":\"payload too large\"}");
        return;
    }

    // Save raw JSON directly (same pattern as pad config)
    bool ok = swipe_config_save_raw(data, total);
    if (ok) {
        request->send(200, "application/json", "{\"ok\":true}");
    } else {
        request->send(500, "application/json", "{\"error\":\"save failed\"}");
    }
}

#endif // HAS_DISPLAY
