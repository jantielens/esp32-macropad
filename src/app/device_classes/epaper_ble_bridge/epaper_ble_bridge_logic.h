#ifndef EPAPER_BLE_BRIDGE_LOGIC_H
#define EPAPER_BLE_BRIDGE_LOGIC_H

#include "epaper_ble_bridge_config.h"
#include "epaper_ble_codec.h"

enum class EpaperBleBridgeSiteAction : uint8_t {
    ReplaceAssignment,
    ClearAssignment,
    KeepAssignment,
    RetryLater,
};

const char *epaper_ble_bridge_validate_config(const EpaperBleBridgeConfig &config);
bool epaper_ble_bridge_site_state_fresh(uint32_t now_ms,
                                        uint32_t last_site_success_ms);
bool epaper_ble_bridge_rotation_fits(size_t frame_count,
                                     uint32_t advertisement_interval_ms,
                                     uint32_t scan_deadline_ms);
EpaperBleBridgeSiteAction epaper_ble_bridge_site_action(int http_status);
uint32_t epaper_ble_bridge_retry_delay_ms(uint8_t failure_count);
EpaperBleAssignmentPacket epaper_ble_bridge_advertised_packet(
    const EpaperBleAssignmentPacket &assignment, uint32_t device_key,
    uint32_t now_ms, uint32_t last_site_success_ms);
uint8_t epaper_ble_bridge_next_frame(uint8_t current, uint8_t frame_count);
bool epaper_ble_bridge_ack_status_terminal(int http_status);

#endif
