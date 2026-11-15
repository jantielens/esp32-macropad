#include "stopwatch_hardware.h"
#include "sensors/battery_adc_sensor.h"
#include "sensors/sensor_manager.h"
#include "ble_telemetry.h"

#if HAS_SENSOR_BATTERY_ADC
#error "Select one battery provider: StopWatch PMIC or battery ADC"
#endif
#if HAS_BATTERY_INFO
#include "sensors/battery_adc_sensor.cpp"

namespace {
void stopwatch_battery_append(BatterySample& sample) {
    uint16_t millivolts = 0;
    bool charging = false, externalPower = false;
    const bool read = m5stack_stopwatch_battery_read(millivolts, charging, externalPower);
    if (read) sample.set(BatteryField::ExternalPowerPresent, externalPower ? 1 : 0);
    const bool valid = read &&
        millivolts >= 2500 && millivolts <= 4350;
    if (valid) {
        const uint8_t percentage = battery_adc_lipo_percentage(millivolts / 1000.0f);
        sample.set(BatteryField::Voltage, millivolts / 1000.0f);
        sample.set(BatteryField::Percentage, percentage);
        if (charging) sample.set_status(BatteryStatus::Charging);
#if HAS_BLE
        ble_telemetry_set_u8(0x01, percentage);
        ble_telemetry_set_u16(0x0c, millivolts);
#endif
    }
}
}
#endif

void board_register_sensors(SensorRegistry& registry) {
#if HAS_BATTERY_INFO
    static const BatteryProvider provider = {
        "M5PM1: measured voltage and voltage-estimated single-cell LiPo percentage. Input voltage above 4 V detects external supply. Charger input establishes Charging only; non-charging status, current and runtime remain unavailable.",
        battery_field_mask(BatteryField::Voltage) | battery_field_mask(BatteryField::Percentage) |
            battery_field_mask(BatteryField::Status) | battery_field_mask(BatteryField::ExternalPowerPresent),
        stopwatch_battery_append,
    };
    SensorCallbacks callbacks = {};
    callbacks.name = "StopWatch PMIC";
    callbacks.battery = &provider;
    registry.add(callbacks);
#endif
}