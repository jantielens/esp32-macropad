#include "board_config.h"

#if IS_EPAPER_BLE_BRIDGE

#include "epaper_ble_bridge_config.h"

#include "epaper_ble_bridge_logic.h"

#include <ArduinoJson.h>
#include <Preferences.h>
#include <string.h>

EpaperBleBridgeConfig g_epaper_ble_bridge_config = {};

namespace {

constexpr char kConfigNvsKey[] = "eb_frames";
static_assert(sizeof(kConfigNvsKey) - 1 <= 14,
              "NVS key exceeds 14 characters");

const EpaperBleBridgeFrameConfig *find_existing(const char *device_id) {
    for (size_t i = 0; i < g_epaper_ble_bridge_config.frame_count; ++i) {
        if (strcmp(g_epaper_ble_bridge_config.frames[i].device_id, device_id) == 0) {
            return &g_epaper_ble_bridge_config.frames[i];
        }
    }
    return nullptr;
}

const char *parse_candidate(JsonObject &body, EpaperBleBridgeConfig *candidate) {
    *candidate = g_epaper_ble_bridge_config;
    JsonVariant frames_value = body["epaper_ble_bridge_frames"];
    if (frames_value.isNull()) return nullptr;
    if (!frames_value.is<JsonArray>()) {
        return "Bridge frame records must be an array";
    }
    JsonArray frames = frames_value.as<JsonArray>();
    if (frames.size() > EPAPER_BLE_BRIDGE_MAX_FRAMES) {
        return "Bridge supports at most two frame records";
    }
    memset(candidate, 0, sizeof(*candidate));
    candidate->frame_count = (uint8_t)frames.size();
    size_t index = 0;
    for (JsonVariant value : frames) {
        if (!value.is<JsonObject>()) return "Bridge frame record must be an object";
        JsonObject frame = value.as<JsonObject>();
        if (!frame["site_url"].is<const char *>() ||
            !frame["device_id"].is<const char *>()) {
            return "Bridge site URL and device ID must be strings";
        }
        const char *site_url = frame["site_url"] | "";
        const char *device_id = frame["device_id"] | "";
        if (strlen(site_url) >= EPAPER_BLE_BRIDGE_SITE_URL_MAX_LEN) {
            return "Bridge site URL is too long";
        }
        if (strlen(device_id) >= EPAPER_BLE_BRIDGE_DEVICE_ID_MAX_LEN) {
            return "Bridge device ID is too long";
        }
        EpaperBleBridgeFrameConfig &target = candidate->frames[index++];
        strlcpy(target.site_url, site_url, sizeof(target.site_url));
        strlcpy(target.device_id, device_id, sizeof(target.device_id));
        const char *api_key = frame["api_key"] | "";
        if (api_key[0]) {
            if (strlen(api_key) >= EPAPER_BLE_BRIDGE_API_KEY_MAX_LEN) {
                return "Bridge device API key is too long";
            }
            strlcpy(target.api_key, api_key, sizeof(target.api_key));
        } else if (const EpaperBleBridgeFrameConfig *existing =
                       find_existing(device_id)) {
            strlcpy(target.api_key, existing->api_key, sizeof(target.api_key));
        }
    }
    return epaper_ble_bridge_validate_config(*candidate);
}

}  // namespace

void epaper_ble_bridge_config_defaults() {
    memset(&g_epaper_ble_bridge_config, 0, sizeof(g_epaper_ble_bridge_config));
}

void epaper_ble_bridge_config_load(Preferences &preferences) {
    epaper_ble_bridge_config_defaults();
    if (preferences.getBytesLength(kConfigNvsKey) ==
        sizeof(g_epaper_ble_bridge_config)) {
        preferences.getBytes(kConfigNvsKey, &g_epaper_ble_bridge_config,
                             sizeof(g_epaper_ble_bridge_config));
    }
    if (epaper_ble_bridge_validate_config(g_epaper_ble_bridge_config)) {
        epaper_ble_bridge_config_defaults();
    }
}

void epaper_ble_bridge_config_save(Preferences &preferences) {
    preferences.putBytes(kConfigNvsKey, &g_epaper_ble_bridge_config,
                         sizeof(g_epaper_ble_bridge_config));
}

void epaper_ble_bridge_config_api_get(JsonObject &root) {
    JsonArray frames = root["epaper_ble_bridge_frames"].to<JsonArray>();
    for (size_t i = 0; i < g_epaper_ble_bridge_config.frame_count; ++i) {
        const EpaperBleBridgeFrameConfig &frame =
            g_epaper_ble_bridge_config.frames[i];
        JsonObject output = frames.add<JsonObject>();
        output["site_url"] = frame.site_url;
        output["device_id"] = frame.device_id;
        output["api_key_set"] = frame.api_key[0] != '\0';
    }
}

const char *epaper_ble_bridge_config_api_validate(JsonObject &body) {
    EpaperBleBridgeConfig candidate = {};
    return parse_candidate(body, &candidate);
}

void epaper_ble_bridge_config_api_set(JsonObject &body) {
    EpaperBleBridgeConfig candidate = {};
    if (!parse_candidate(body, &candidate)) {
        g_epaper_ble_bridge_config = candidate;
    }
}

#endif  // IS_EPAPER_BLE_BRIDGE