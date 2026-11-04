#include "tab5_hardware.h"
#include "sensors/sensor_manager.h"

#if HAS_SENSOR_BATTERY_ADC
#error "Select one battery provider: Tab5 INA226 or battery ADC"
#endif

#if HAS_MQTT
#include "ha_discovery.h"
#endif

namespace {
void tab5_battery_sample(JsonObject& doc) {
    float voltage = 0, current = 0;
    const bool valid = tab5_battery_read(voltage, current);
    sensor_manager_set_number(doc, "battery_voltage", voltage, valid);
    sensor_manager_set_number(doc, "battery_current", current, valid);
}

#if HAS_MQTT
void tab5_battery_publish_ha(MqttManager& mqtt) {
    ha_discovery_publish_sensor_config(mqtt, "battery_voltage", "Battery Voltage",
        "{{ value_json.battery_voltage }}", "V", "voltage", "measurement", "diagnostic");
    ha_discovery_publish_sensor_config(mqtt, "battery_current", "Battery Current",
        "{{ value_json.battery_current }}", "A", "current", "measurement", "diagnostic");
}
#endif
}

void board_register_sensors(SensorRegistry& registry) {
    static const SensorField fields[] = {
        {"battery_voltage", "Battery voltage in volts.", "V", "7.8"},
        {"battery_current", "Battery current in amperes; positive when charging.", "A", "-0.25"},
    };
    SensorCallbacks callbacks = {};
    callbacks.name = "Tab5 INA226";
    callbacks.sample = tab5_battery_sample;
    callbacks.sample_interval_ms = 5000;
    callbacks.fields = fields;
    callbacks.field_count = sizeof(fields) / sizeof(fields[0]);
#if HAS_MQTT
    callbacks.publish_ha = tab5_battery_publish_ha;
#endif
    registry.add(callbacks);
}