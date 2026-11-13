#include "tab5_hardware.h"
#include "sensors/sensor_manager.h"

#if HAS_SENSOR_BATTERY_ADC
#error "Select one battery provider: Tab5 INA226 or battery ADC"
#endif

#if HAS_BATTERY_INFO
namespace {
void tab5_battery_sample(BatterySample& sample) {
    float voltage = 0, current = 0;
    const bool valid = tab5_battery_read(voltage, current);
    if (!valid) return;
    sample.set(BatteryField::Voltage, voltage);
    sample.set(BatteryField::Current, current);
}
}
#endif

void board_register_sensors(SensorRegistry& registry) {
#if HAS_BATTERY_INFO
    static const BatteryProvider provider = {
        "INA226: pack voltage and signed current from monitor conversions; power uses the same sample. No gauge state, percentage or runtime estimate is available.",
        battery_field_mask(BatteryField::Voltage) | battery_field_mask(BatteryField::Current),
        tab5_battery_sample,
    };
    SensorCallbacks callbacks = {};
    callbacks.name = "Tab5 INA226";
    callbacks.sample_interval_ms = 5000;
    callbacks.battery = &provider;
    registry.add(callbacks);
#endif
}