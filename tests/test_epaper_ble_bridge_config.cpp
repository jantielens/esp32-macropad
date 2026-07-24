#include "device_classes/epaper_ble_bridge/epaper_ble_bridge_config.h"

#include <ArduinoJson.h>
#include <Preferences.h>
#include <assert.h>
#include <string.h>
#include <string>

void epaper_ble_bridge_config_api_get(JsonObject &root);
const char *epaper_ble_bridge_config_api_validate(JsonObject &body);
void epaper_ble_bridge_config_api_set(JsonObject &body);

namespace {

JsonDocument frame_request(const char *device_id, const char *api_key) {
    JsonDocument document;
    JsonArray frames = document["epaper_ble_bridge_frames"].to<JsonArray>();
    JsonObject frame = frames.add<JsonObject>();
    frame["site_url"] = "https://photos.example";
    frame["device_id"] = device_id;
    frame["api_key"] = api_key;
    return document;
}

}  // namespace

int main() {
    epaper_ble_bridge_config_defaults();
    assert(g_epaper_ble_bridge_config.frame_count == 0);

    JsonDocument create = frame_request("frame-a", "private-key");
    JsonObject create_body = create.as<JsonObject>();
    assert(epaper_ble_bridge_config_api_validate(create_body) == nullptr);
    epaper_ble_bridge_config_api_set(create_body);
    assert(g_epaper_ble_bridge_config.frame_count == 1);
    assert(strcmp(g_epaper_ble_bridge_config.frames[0].api_key,
                  "private-key") == 0);

    JsonDocument response;
    JsonObject response_root = response.to<JsonObject>();
    epaper_ble_bridge_config_api_get(response_root);
    std::string json;
    serializeJson(response, json);
    assert(json.find("api_key_set") != std::string::npos);
    assert(json.find("private-key") == std::string::npos);
    assert(json.find("\"api_key\":") == std::string::npos);

    JsonDocument retain = frame_request("frame-a", "");
    JsonObject retain_body = retain.as<JsonObject>();
    assert(epaper_ble_bridge_config_api_validate(retain_body) == nullptr);
    epaper_ble_bridge_config_api_set(retain_body);
    assert(strcmp(g_epaper_ble_bridge_config.frames[0].api_key,
                  "private-key") == 0);

    JsonDocument collision;
    JsonArray collision_frames =
        collision["epaper_ble_bridge_frames"].to<JsonArray>();
    for (size_t index = 0; index < 2; ++index) {
        JsonObject frame = collision_frames.add<JsonObject>();
        frame["site_url"] = "https://photos.example";
        frame["device_id"] = "frame-a";
        frame["api_key"] = "replacement";
    }
    JsonObject collision_body = collision.as<JsonObject>();
    assert(epaper_ble_bridge_config_api_validate(collision_body) != nullptr);
    epaper_ble_bridge_config_api_set(collision_body);
    assert(g_epaper_ble_bridge_config.frame_count == 1);
    assert(strcmp(g_epaper_ble_bridge_config.frames[0].api_key,
                  "private-key") == 0);

    Preferences preferences;
    epaper_ble_bridge_config_save(preferences);
    epaper_ble_bridge_config_defaults();
    epaper_ble_bridge_config_load(preferences);
    assert(g_epaper_ble_bridge_config.frame_count == 1);
    assert(strcmp(g_epaper_ble_bridge_config.frames[0].device_id,
                  "frame-a") == 0);
    assert(strcmp(g_epaper_ble_bridge_config.frames[0].api_key,
                  "private-key") == 0);
    return 0;
}