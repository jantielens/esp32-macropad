#include <ArduinoJson.h>

#include <assert.h>
#include <string>

namespace {

std::string max_text(size_t capacity) {
    return std::string(capacity - 1, 'x');
}

}  // namespace

int main() {
    JsonDocument doc;
    doc["wifi_ssid"] = max_text(32);
    doc["wifi_password"] = "";
    doc["device_name"] = max_text(32);
    doc["device_name_sanitized"] = max_text(32);
    doc["fixed_ip"] = max_text(16);
    doc["subnet_mask"] = max_text(16);
    doc["gateway"] = max_text(16);
    doc["dns1"] = max_text(16);
    doc["dns2"] = max_text(16);
    doc["mqtt_host"] = max_text(64);
    doc["mqtt_port"] = 65535;
    doc["mqtt_username"] = max_text(32);
    doc["mqtt_password"] = "";
    doc["ha_url"] = max_text(48);
    doc["ha_token"] = "";
    doc["operating_mode"] = max_text(20);
    doc["duty_cycle_wake_seconds"] = UINT32_MAX;
    doc["mqtt_publish_interval_seconds"] = UINT32_MAX;
    doc["portal_idle_timeout_seconds"] = UINT32_MAX;
    doc["wifi_backoff_max_seconds"] = UINT32_MAX;
    doc["ble_burst_count"] = 255;
    doc["ble_adv_interval_ms"] = 65535;
    JsonObject caps = doc["caps"].to<JsonObject>();
    caps["ble"] = true;
    caps["mqtt"] = true;
    caps["display"] = false;
    caps["ble_hid"] = false;
    caps["mcp"] = true;
    doc["mqtt_publish_scope"] = max_text(20);
    doc["basic_auth_enabled"] = true;
    doc["basic_auth_username"] = max_text(32);
    doc["basic_auth_password"] = "";
    doc["basic_auth_password_set"] = true;
    doc["mcp_enabled"] = true;
    doc["mcp_control_enabled"] = true;
    doc["mcp_authoring_enabled"] = true;
    doc["mcp_token_set"] = true;
    doc["backlight_brightness"] = 255;
    JsonArray frames = doc["epaper_ble_bridge_frames"].to<JsonArray>();
    for (size_t index = 0; index < 2; ++index) {
        JsonObject frame = frames.add<JsonObject>();
        frame["site_url"] = max_text(128);
        frame["device_id"] = max_text(64);
        frame["api_key_set"] = true;
    }
    assert(measureJson(doc) <= 2304);
    std::string json;
    serializeJson(doc, json);
    assert(json.find("api_key_set") != std::string::npos);
    assert(json.find("\"api_key\":") == std::string::npos);
    return 0;
}