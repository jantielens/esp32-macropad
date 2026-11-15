#include "bthome_event_settings.h"
#include "board_config.h"

#if HAS_BLE || HAS_BLE_HID
#include "ble_backend.h"
#include "log_manager.h"
#include <Preferences.h>

namespace bthome {
namespace {
constexpr const char* kSettingsKey = "bt_settings";
}

BluetoothSettings load_bluetooth_settings(Preferences& preferences, int8_t default_tx_power_dbm) {
    const BluetoothSettings defaults{default_tx_power_dbm, {}};
    if (!preferences.isKey(kSettingsKey)) return defaults;
    uint8_t bytes[5] = {};
    if (preferences.getBytesLength(kSettingsKey) == sizeof(bytes) &&
        preferences.getBytes(kSettingsKey, bytes, sizeof(bytes)) == sizeof(bytes)) {
        BluetoothSettings settings = defaults;
        settings.events.burst_ms = uint16_t(bytes[0]) | uint16_t(bytes[1]) << 8;
        settings.events.adv_interval_ms = uint16_t(bytes[2]) | uint16_t(bytes[3]) << 8;
        const int power = int(bytes[4]) - 12;
        if (valid_event_settings(settings.events) && bluetooth_power_valid(power)) {
            settings.tx_power_dbm = static_cast<int8_t>(power);
            return settings;
        }
    }
    LOGW("Config", "Invalid stored Bluetooth settings; using %d dBm and 350/100 ms",
        int(default_tx_power_dbm));
    return defaults;
}

bool save_bluetooth_settings(Preferences& preferences, const BluetoothSettings& settings) {
    if (!valid_event_settings(settings.events) || !bluetooth_power_valid(settings.tx_power_dbm)) {
        LOGE("Config", "Invalid Bluetooth settings");
        return false;
    }
    // One NVS value prevents a failed write from splitting power and timing.
    const uint8_t bytes[5] = {
        uint8_t(settings.events.burst_ms), uint8_t(settings.events.burst_ms >> 8),
        uint8_t(settings.events.adv_interval_ms), uint8_t(settings.events.adv_interval_ms >> 8),
        uint8_t(settings.tx_power_dbm + 12)};
    if (preferences.putBytes(kSettingsKey, bytes, sizeof(bytes)) != sizeof(bytes)) {
        LOGE("Config", "Failed to persist Bluetooth settings");
        return false;
    }
    return true;
}
}
#endif
