#include "m5stack_core2_hardware.h"
#include "sensors/battery_adc_sensor.cpp"
#include "sensors/sensor_manager.h"
#include "ble_telemetry.h"

#if HAS_MQTT
#include "ha_discovery.h"
#endif

namespace {
void core2_battery_append(JsonObject& doc) {
    uint16_t millivolts = 0;
    bool charging = false;
    const bool readOk = m5stack_core2_battery_read(millivolts, charging);
    const bool valid = readOk && millivolts >= 2500 && millivolts <= 4350;
    const float voltage = millivolts / 1000.0f;
    sensor_manager_set_number(doc, "battery_voltage", voltage, valid);
    sensor_manager_set_bool(doc, "battery_charging", charging, valid);
    if (valid) {
        const uint8_t percentage = battery_adc_lipo_percentage(voltage);
        doc["battery_percentage"] = percentage;
#if HAS_BLE
        ble_telemetry_set_u8(0x01, percentage);
        ble_telemetry_set_u16(0x0c, millivolts);
#endif
    } else {
        doc["battery_percentage"] = nullptr;
    }
}

#if HAS_MQTT
void core2_battery_publish_ha(MqttManager& mqtt) {
    ha_discovery_publish_sensor_config(mqtt, "battery_voltage", "Battery Voltage",
        "{{ value_json.battery_voltage }}", "V", "voltage", "measurement", "diagnostic");
    ha_discovery_publish_sensor_config(mqtt, "battery_percentage", "Battery",
        "{{ value_json.battery_percentage }}", "%", "battery", "measurement", nullptr);
    ha_discovery_publish_binary_sensor_config(mqtt, "battery_charging", "Battery Charging",
        "{{ 'ON' if value_json.battery_charging else 'OFF' }}", "battery_charging", nullptr);
}
#endif
}

void board_register_sensors(SensorRegistry& registry) {
    static const SensorField fields[] = {
        {"battery_voltage", "Battery voltage in volts."},
        {"battery_charging", "Battery charging: 1 or 0."},
        {"battery_percentage", "Estimated battery charge in percent."},
    };
    SensorCallbacks callbacks = {};
    callbacks.name = "M5Stack Core2 PMIC";
    callbacks.sample = core2_battery_append;
    callbacks.fields = fields;
    callbacks.field_count = sizeof(fields) / sizeof(fields[0]);
#if HAS_MQTT
    callbacks.publish_ha = core2_battery_publish_ha;
#endif
    registry.add(callbacks);
}