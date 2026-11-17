#include "battery_info.h"

#if HAS_BATTERY_INFO

namespace {
const BatteryFieldInfo fields[] = {
    {"voltage", "battery_voltage", "Battery pack voltage in volts.", "V", "3.85"},
    {"percentage", "battery_percentage", "Battery state of charge in percent; may be voltage-estimated.", "%", "73"},
    {"current", "battery_current", "Battery current in amperes; positive charging, negative discharging. See provider measurement notes.", "A", "-0.25"},
    {"power", "battery_power", "Signed battery power in watts, calculated from voltage and current in the same sample; not USB/device input power.", "W", "-0.96"},
    {"discharge_power", "battery_discharge_power", "Positive battery discharge power in watts; zero when not discharging. Not total device consumption on external power.", "W", "0.96"},
    {"status", "battery_status", "Charging, Discharging, Idle or Full, only when the provider can establish the state.", "", "Discharging"},
    {"external_power_present", "external_power_present", "External supply detected: 1 or 0. Does not establish battery charging.", "", "0"},
    {"time_remaining", "battery_time_remaining", "Provider-estimated minutes to empty at the current load; unavailable unless discharging.", "min", "120"},
    {"time_to_full", "battery_time_to_full", "Provider-estimated minutes to full; unavailable unless charging.", "min", "45"},
};
static_assert(sizeof(fields) / sizeof(fields[0]) == BATTERY_FIELD_COUNT);

bool valid(BatteryField field, float value) {
    if (!isfinite(value)) return false;
    switch (field) {
        case BatteryField::Voltage: return value > 0;
        case BatteryField::Percentage: return value >= 0 && value <= 100;
        case BatteryField::Status: return battery_status_text(value) != nullptr;
        case BatteryField::ExternalPowerPresent: return value == 0 || value == 1;
        case BatteryField::TimeRemaining:
        case BatteryField::TimeToFull: return value >= 0;
        default: return true;
    }
}
}

BatterySample::BatterySample() {
    for (float &value : values) value = NAN;
}

void BatterySample::set(BatteryField field, float value) {
    invalidated &= ~battery_field_mask(field);
    values[static_cast<size_t>(field)] = valid(field, value) ? value : NAN;
}

void BatterySample::invalidate(BatteryField field) {
    values[static_cast<size_t>(field)] = NAN;
    invalidated |= battery_field_mask(field);
}

const BatteryFieldInfo &battery_field_info(BatteryField field) {
    return fields[static_cast<size_t>(field)];
}

const char *battery_status_text(float value) {
    if (value == static_cast<float>(BatteryStatus::Charging)) return "Charging";
    if (value == static_cast<float>(BatteryStatus::Discharging)) return "Discharging";
    if (value == static_cast<float>(BatteryStatus::Idle)) return "Idle";
    if (value == static_cast<float>(BatteryStatus::Full)) return "Full";
    return nullptr;
}

uint32_t battery_supported_fields(uint32_t provider_fields) {
    const uint32_t inputs = battery_field_mask(BatteryField::Voltage) | battery_field_mask(BatteryField::Current);
    if ((provider_fields & inputs) == inputs)
        provider_fields |= battery_field_mask(BatteryField::Power) | battery_field_mask(BatteryField::DischargePower);
    return provider_fields;
}

void BatteryCache::configure(const BatteryProvider &provider, uint32_t interval_ms) {
    cached_.supported = battery_supported_fields(provider.fields);
    cached_.description = provider.description;
    expiry_ms_ = interval_ms > UINT32_MAX / 3 ? UINT32_MAX : interval_ms * 3;
}

void BatteryCache::tick(uint32_t now, bool paused) {
    const uint32_t elapsed = now - last_tick_;
    if (clock_started_ && !paused && !paused_) {
        for (size_t index = 0; index < BATTERY_FIELD_COUNT; ++index) {
            const uint64_t age = uint64_t(ages_[index]) + elapsed;
            ages_[index] = age > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(age);
        }
    }
    clock_started_ = true;
    last_tick_ = now;
    paused_ = paused;
}

void BatteryCache::update(BatterySample sample) {
    for (size_t index = 0; index < BATTERY_FIELD_COUNT; ++index) {
        const BatteryField field = static_cast<BatteryField>(index);
        if (sample.invalidated & battery_field_mask(field))
            cached_.readings.set(field, NAN);
    }
    const float voltage = sample.get(BatteryField::Voltage);
    const float current = sample.get(BatteryField::Current);
    sample.set(BatteryField::Power, voltage * current);
    const float power = sample.get(BatteryField::Power);
    sample.set(BatteryField::DischargePower, isfinite(power) ? fmaxf(0, -power) : NAN);
    const uint32_t powerInputs = battery_field_mask(BatteryField::Voltage) | battery_field_mask(BatteryField::Current);
    if (!isfinite(power) && (isfinite(voltage) || isfinite(current) || (sample.invalidated & powerInputs))) {
        cached_.readings.set(BatteryField::Power, NAN);
        cached_.readings.set(BatteryField::DischargePower, NAN);
    }

    const float status = sample.get(BatteryField::Status);
    const bool discharging = status == static_cast<float>(BatteryStatus::Discharging);
    const bool charging = status == static_cast<float>(BatteryStatus::Charging);
    if (!discharging) sample.set(BatteryField::TimeRemaining, NAN);
    if (!charging) sample.set(BatteryField::TimeToFull, NAN);
    if (isfinite(status) && status != cached_.readings.get(BatteryField::Status)) {
        cached_.readings.set(BatteryField::Current, NAN);
        cached_.readings.set(BatteryField::Power, NAN);
        cached_.readings.set(BatteryField::DischargePower, NAN);
    }
    for (size_t index = 0; index < BATTERY_FIELD_COUNT; ++index) {
        const BatteryField field = static_cast<BatteryField>(index);
        const float value = sample.get(field);
        if (!(cached_.supported & battery_field_mask(field))) continue;
        if (valid(field, value)) {
            cached_.readings.set(field, value);
            ages_[index] = 0;
        } else if (field == BatteryField::Status || field == BatteryField::TimeRemaining ||
                   field == BatteryField::TimeToFull) {
            cached_.readings.set(field, NAN);
        }
    }
}

BatterySnapshot BatteryCache::snapshot() const {
    BatterySnapshot snapshot = cached_;
    for (size_t index = 0; index < BATTERY_FIELD_COUNT; ++index) {
        if (ages_[index] >= expiry_ms_) snapshot.readings.values[index] = NAN;
    }
    return snapshot;
}

#endif
