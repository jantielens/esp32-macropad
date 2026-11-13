#pragma once

#include "board_config.h"
#if HAS_BATTERY_INFO

#include <stddef.h>
#include <stdint.h>
#include <math.h>

enum class BatteryField : uint8_t {
    Voltage, Percentage, Current, Power, DischargePower, Status,
    ExternalPowerPresent, TimeRemaining, TimeToFull, Count
};

enum class BatteryStatus : uint8_t { Charging, Discharging, Idle, Full };

constexpr size_t BATTERY_FIELD_COUNT = static_cast<size_t>(BatteryField::Count);
constexpr uint32_t battery_field_mask(BatteryField field) {
    return uint32_t(1) << static_cast<uint8_t>(field);
}

struct BatterySample {
    float values[BATTERY_FIELD_COUNT];
    BatterySample();
    void set(BatteryField field, float value);
    void set_status(BatteryStatus status) { set(BatteryField::Status, static_cast<float>(status)); }
    float get(BatteryField field) const { return values[static_cast<size_t>(field)]; }
};

struct BatteryProvider {
    const char *description;
    uint32_t fields;
    void (*sample)(BatterySample &sample);
};

struct BatteryFieldInfo {
    const char *key;
    const char *json_key;
    const char *description;
    const char *unit;
    const char *example;
};

const BatteryFieldInfo &battery_field_info(BatteryField field);
const char *battery_status_text(float value);
uint32_t battery_supported_fields(uint32_t provider_fields);

struct BatterySnapshot {
    BatterySample readings;
    uint32_t supported = 0;
    const char *description = nullptr;
};

// Owned by the sensor manager and accessed under its cache lock.
class BatteryCache {
public:
    void configure(const BatteryProvider &provider, uint32_t interval_ms);
    void tick(uint32_t now, bool paused);
    void update(BatterySample sample);
    BatterySnapshot snapshot() const;
private:
    BatterySnapshot cached_;
    uint32_t ages_[BATTERY_FIELD_COUNT] = {};
    uint32_t expiry_ms_ = 0;
    uint32_t last_tick_ = 0;
    bool clock_started_ = false;
    bool paused_ = false;
};

#endif
