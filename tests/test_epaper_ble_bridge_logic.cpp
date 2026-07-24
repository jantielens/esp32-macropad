#include "device_classes/epaper_ble_bridge/epaper_ble_bridge_logic.h"

#include <assert.h>
#include <string.h>

int main() {
    assert(epaper_ble_bridge_rotation_fits(0, 100, 400));
    assert(epaper_ble_bridge_rotation_fits(2, 100, 400));
    assert(!epaper_ble_bridge_rotation_fits(5, 100, 400));
    assert(!epaper_ble_bridge_site_state_fresh(1000, 0));
    assert(epaper_ble_bridge_site_state_fresh(900001, 1));
    assert(!epaper_ble_bridge_site_state_fresh(900002, 1));
    assert(epaper_ble_bridge_site_action(200) ==
           EpaperBleBridgeSiteAction::ReplaceAssignment);
    assert(epaper_ble_bridge_site_action(204) ==
           EpaperBleBridgeSiteAction::ClearAssignment);
    assert(epaper_ble_bridge_site_action(304) ==
           EpaperBleBridgeSiteAction::KeepAssignment);
    assert(epaper_ble_bridge_site_action(-1) ==
           EpaperBleBridgeSiteAction::RetryLater);
    assert(epaper_ble_bridge_retry_delay_ms(0) == 1000);
    assert(epaper_ble_bridge_retry_delay_ms(6) == 60000);
       assert(epaper_ble_bridge_ack_status_terminal(200));
       assert(epaper_ble_bridge_ack_status_terminal(204));
       assert(epaper_ble_bridge_ack_status_terminal(409));
       assert(!epaper_ble_bridge_ack_status_terminal(500));
       assert(epaper_ble_bridge_next_frame(0, 2) == 1);
       assert(epaper_ble_bridge_next_frame(1, 2) == 0);
       assert(epaper_ble_bridge_next_frame(0, 0) == 0);

       EpaperBleAssignmentPacket assignment = {};
       assignment.valid = true;
       assignment.revision = 7;
       EpaperBleAssignmentPacket fresh = epaper_ble_bridge_advertised_packet(
              assignment, 42, 900001, 1);
       assert(fresh.valid && fresh.device_key == 42 && fresh.revision == 7);
       EpaperBleAssignmentPacket stale = epaper_ble_bridge_advertised_packet(
              assignment, 42, 900002, 1);
       assert(!stale.valid && stale.device_key == 42 && stale.revision == 7);

    EpaperBleBridgeConfig config = {};
    config.frame_count = 2;
    strlcpy(config.frames[0].site_url, "https://photos.example", sizeof(config.frames[0].site_url));
    strlcpy(config.frames[0].device_id, "frame-a", sizeof(config.frames[0].device_id));
    strlcpy(config.frames[0].api_key, "secret-a", sizeof(config.frames[0].api_key));
    strlcpy(config.frames[1].site_url, "https://photos.example", sizeof(config.frames[1].site_url));
    strlcpy(config.frames[1].device_id, "frame-b", sizeof(config.frames[1].device_id));
    strlcpy(config.frames[1].api_key, "secret-b", sizeof(config.frames[1].api_key));
    assert(epaper_ble_bridge_validate_config(config) == nullptr);
    strlcpy(config.frames[1].device_id, "frame-a", sizeof(config.frames[1].device_id));
    assert(epaper_ble_bridge_validate_config(config) != nullptr);
    return 0;
}