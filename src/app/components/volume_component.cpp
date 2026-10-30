#include "component_registry.h"
#include "audio.h"
#include <ArduinoJson.h>

static void volume_set_body(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
    if (index != 0 || index + len != total) return;
    StaticJsonDocument<128> doc;
    if (deserializeJson(doc, data, len) || !doc["volume"].is<int>()) {
        request->send(400, "application/json", "{\"success\":false,\"message\":\"Invalid volume\"}");
        return;
    }
    const int volume = doc["volume"];
    if (volume < 0 || volume > 100) {
        request->send(400, "application/json", "{\"success\":false,\"message\":\"Volume must be 0-100\"}");
        return;
    }
    audio_set_volume(static_cast<uint8_t>(volume));
    request->send(200, "application/json", "{\"success\":true}");
}

static const ComponentAction volume_actions[] = {
    {"level", HTTP_PUT, nullptr, volume_set_body},
};

static ComponentDef volume_component = {
    .id = "volume",
    .category = "audio",
    .display_name = "Volume",
    .nav_order = 10,
    .get_config = nullptr,
    .save_config = nullptr,
    .save_config_body = nullptr,
    .delete_config = nullptr,
    .custom_actions = volume_actions,
    .num_custom_actions = sizeof(volume_actions) / sizeof(volume_actions[0]),
    .fragment_id = "volume"
};

REGISTER_COMPONENT(volume);
