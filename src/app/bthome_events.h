#pragma once

#include "board_config.h"
#include <stdint.h>

namespace bthome {
constexpr uint8_t button_count = 10;
constexpr uint8_t press = 0x01;
constexpr uint8_t double_press = 0x02;
constexpr uint8_t long_press = 0x04;

inline bool valid_button_event(uint8_t button, uint8_t event) {
    return button >= 1 && button <= button_count &&
        (event == press || event == double_press || event == long_press);
}
}

#if HAS_BLE || HAS_BLE_HID
// Configure on the main loop; Sticky initializes BLE here, other boards on use.
void bthome_events_configure(const char* device_name);
// Thread-safe admission. Success means queued, not received by Home Assistant.
bool bthome_events_send(uint8_t button, uint8_t event);
// Owns advertising and HID pause/resume on the Arduino main loop.
void bthome_events_loop();
#endif
