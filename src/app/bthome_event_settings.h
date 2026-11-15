#pragma once

#include <stdint.h>

class Preferences;

namespace bthome {
struct EventSettings {
    uint16_t burst_ms = 350;
    uint16_t adv_interval_ms = 100;
};

struct BluetoothSettings {
    int8_t tx_power_dbm;
    EventSettings events;
};

inline bool valid_event_settings(const EventSettings& settings) {
    return settings.burst_ms >= 100 && settings.burst_ms <= 1000 &&
        settings.adv_interval_ms >= 20 && settings.adv_interval_ms <= 300 &&
        settings.burst_ms >= 3u * settings.adv_interval_ms;
}

BluetoothSettings load_bluetooth_settings(Preferences& preferences, int8_t default_tx_power_dbm);
bool save_bluetooth_settings(Preferences& preferences, const BluetoothSettings& settings);
}
