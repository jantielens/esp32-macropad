#include "epaper_ble_bridge_logic.h"

#include "epaper_ble_codec.h"

#include <string.h>

namespace {

bool has_http_scheme(const char *url) {
    return url && (strncmp(url, "http://", 7) == 0 ||
                   strncmp(url, "https://", 8) == 0);
}

}  // namespace

const char *epaper_ble_bridge_validate_config(const EpaperBleBridgeConfig &config) {
    if (config.frame_count > EPAPER_BLE_BRIDGE_MAX_FRAMES) {
        return "Bridge supports at most two frame records";
    }
    if (!epaper_ble_bridge_rotation_fits(
            config.frame_count, EPAPER_BLE_BRIDGE_ADV_INTERVAL_MS,
            EPAPER_BLE_FRAME_SCAN_DEADLINE_MS)) {
        return "Configured frames exceed the BLE scan deadline";
    }
    for (size_t i = 0; i < config.frame_count; ++i) {
        const EpaperBleBridgeFrameConfig &frame = config.frames[i];
        if (!frame.site_url[0] || !has_http_scheme(frame.site_url)) {
            return "Bridge site URL must start with http:// or https://";
        }
        if (!frame.device_id[0]) return "Bridge device ID is required";
        if (!frame.api_key[0]) return "Bridge device API key is required";
        const uint32_t device_key = epaper_ble_device_key(frame.device_id);
        for (size_t other = 0; other < i; ++other) {
            if (epaper_ble_device_key(config.frames[other].device_id) == device_key) {
                return "Bridge frame records have a device-key collision";
            }
        }
    }
    return nullptr;
}

bool epaper_ble_bridge_site_state_fresh(uint32_t now_ms,
                                        uint32_t last_site_success_ms) {
    return last_site_success_ms != 0 &&
           (uint32_t)(now_ms - last_site_success_ms) <=
               EPAPER_BLE_BRIDGE_FRESHNESS_LIMIT_MS;
}

bool epaper_ble_bridge_rotation_fits(size_t frame_count,
                                     uint32_t advertisement_interval_ms,
                                     uint32_t scan_deadline_ms) {
    if (frame_count == 0) return true;
    return advertisement_interval_ms != 0 &&
           frame_count <= scan_deadline_ms / advertisement_interval_ms;
}

EpaperBleBridgeSiteAction epaper_ble_bridge_site_action(int http_status) {
    if (http_status == 200) return EpaperBleBridgeSiteAction::ReplaceAssignment;
    if (http_status == 204) return EpaperBleBridgeSiteAction::ClearAssignment;
    if (http_status == 304) return EpaperBleBridgeSiteAction::KeepAssignment;
    return EpaperBleBridgeSiteAction::RetryLater;
}

uint32_t epaper_ble_bridge_retry_delay_ms(uint8_t failure_count) {
    constexpr uint32_t kBaseDelayMs = 1000;
    constexpr uint32_t kMaximumDelayMs = 60000;
    const uint8_t shift = failure_count > 6 ? 6 : failure_count;
    const uint32_t delay_ms = kBaseDelayMs << shift;
    return delay_ms > kMaximumDelayMs ? kMaximumDelayMs : delay_ms;
}

EpaperBleAssignmentPacket epaper_ble_bridge_advertised_packet(
    const EpaperBleAssignmentPacket &assignment, uint32_t device_key,
    uint32_t now_ms, uint32_t last_site_success_ms) {
    EpaperBleAssignmentPacket packet = assignment;
    packet.device_key = device_key;
    if (!epaper_ble_bridge_site_state_fresh(now_ms, last_site_success_ms)) {
        packet.valid = false;
    }
    return packet;
}

uint8_t epaper_ble_bridge_next_frame(uint8_t current, uint8_t frame_count) {
    if (!frame_count || current + 1 >= frame_count) return 0;
    return current + 1;
}

bool epaper_ble_bridge_ack_status_terminal(int http_status) {
    return (http_status >= 200 && http_status < 300) || http_status == 409;
}
