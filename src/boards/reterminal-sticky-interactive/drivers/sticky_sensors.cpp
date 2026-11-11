#include "sticky_hardware.h"
#include "sensors/sensor_manager.h"
#include "i2c_bus.h"
#include "ha_discovery.h"
#include "log_manager.h"
#include <SensirionI2cSht4x.h>
#include <Wire.h>
#include <math.h>

namespace {
constexpr uint8_t kShtAddress = 0x44;
constexpr uint8_t kGaugeAddress = 0x55;
constexpr uint8_t kVoltage = 0x08;
constexpr uint8_t kStateOfCharge = 0x2c;
constexpr uint32_t kLogIntervalMs = 60000;
SensirionI2cSht4x sht;

struct FailureLog {
    bool active = false;
    uint32_t last = 0;
    void update(bool failed, const char *message) {
        const uint32_t now = millis();
        if (failed && (!active || uint32_t(now - last) >= kLogIntervalMs)) {
            LOGW("Sticky", "%s", message);
            last = now;
        } else if (!failed && active) {
            LOGI("Sticky", "%s recovered", message);
        }
        active = failed;
    }
};

FailureLog shtFailure, voltageFailure, chargeFailure;

void sht_init() {
    sht.begin(Wire, kShtAddress);
}

void sht_sample(JsonObject &doc) {
    float temperature = 0, humidity = 0;
    bool read = false;
    if (sticky_hardware_ready() && i2c_bus_lock()) {
        read = sht.measureHighPrecision(temperature, humidity) == 0;
        i2c_bus_unlock();
    }
    const bool temperatureValid = read && isfinite(temperature) && temperature >= -45 && temperature <= 125;
    const bool humidityValid = read && isfinite(humidity);
    // Sensirion returns uncropped RH; crop successful measurements per its datasheet.
    if (humidityValid) humidity = fminf(100.0f, fmaxf(0.0f, humidity));
    shtFailure.update(!temperatureValid || !humidityValid, "SHT40 read invalid or failed (check Wire0 and 0x44)");
    sensor_manager_set_number(doc, "temperature", temperature, temperatureValid);
    sensor_manager_set_number(doc, "humidity", humidity, humidityValid);
}

bool gauge_word(uint8_t command, uint16_t &value) {
    Wire.beginTransmission(kGaugeAddress);
    const bool wrote = Wire.write(command) == 1;
    const uint8_t result = Wire.endTransmission(wrote ? false : true);
    if (!wrote || result != 0) return false;
    if (Wire.requestFrom(kGaugeAddress, uint8_t(2)) != 2) return false;
    const int low = Wire.read();
    const int high = Wire.read();
    if (low < 0 || high < 0) return false;
    value = uint16_t(low) | (uint16_t(high) << 8);
    return true;
}

void gauge_sample(JsonObject &doc) {
    uint16_t voltage = 0, charge = 0;
    bool voltageRead = false, chargeRead = false;
    const bool ready = sticky_hardware_ready();
    if (ready && i2c_bus_lock()) {
        voltageRead = gauge_word(kVoltage, voltage);
        chargeRead = gauge_word(kStateOfCharge, charge);
        i2c_bus_unlock();
    }
    const bool voltageValid = voltageRead && voltage >= 2000 && voltage <= 5000;
    const bool chargeValid = chargeRead && charge <= 100;
    voltageFailure.update(!voltageValid, "BQ27220 voltage read invalid or failed (check Wire0 and 0x55)");
    chargeFailure.update(!chargeValid, "BQ27220 charge read invalid or failed (check Wire0 and 0x55)");
    sensor_manager_set_number(doc, "battery_voltage", voltage / 1000.0f, voltageValid);
    sensor_manager_set_number(doc, "battery_percentage", charge, chargeValid);
    sensor_manager_set_bool(doc, "external_power_present", ready && digitalRead(9) == HIGH, ready);
}

#if HAS_MQTT
void sht_discovery(MqttManager &mqtt) {
    ha_discovery_publish_sensor_config(mqtt, "temperature", "Temperature", "{{ value_json.temperature }}", "°C", "temperature", "measurement", nullptr);
    ha_discovery_publish_sensor_config(mqtt, "humidity", "Humidity", "{{ value_json.humidity }}", "%", "humidity", "measurement", nullptr);
}

void gauge_discovery(MqttManager &mqtt) {
    ha_discovery_publish_sensor_config(mqtt, "battery_voltage", "Battery Voltage", "{{ value_json.battery_voltage }}", "V", "voltage", "measurement", "diagnostic");
    ha_discovery_publish_sensor_config(mqtt, "battery_percentage", "Battery", "{{ value_json.battery_percentage }}", "%", "battery", "measurement", nullptr);
    ha_discovery_publish_binary_sensor_config(mqtt, "external_power_present", "External Power",
        "{{ 'None' if value_json.external_power_present is none else ('ON' if value_json.external_power_present else 'OFF') }}", "power", "diagnostic");
}
#endif
}

void board_register_sensors(SensorRegistry &registry) {
    static const SensorField environmental[] = {
        {"temperature", "Temperature in degrees Celsius.", "\xC2\xB0" "C", "21.37"},
        {"humidity", "Relative humidity in percent.", "%", "54.2"},
    };
    SensorCallbacks env = {};
    env.name = "SHT40";
    env.init = sht_init;
    env.sample = sht_sample;
    env.sample_interval_ms = 5000;
    env.fields = environmental;
    env.field_count = sizeof(environmental) / sizeof(environmental[0]);
#if HAS_MQTT
    env.publish_ha = sht_discovery;
#endif
    registry.add(env);

    static const SensorField battery[] = {
        {"battery_voltage", "Battery voltage in volts.", "V", "3.85"},
        {"battery_percentage", "Battery gauge state of charge in percent.", "%", "73"},
        {"external_power_present", "External power present: 1 or 0.", nullptr, "1"},
    };
    SensorCallbacks gauge = {};
    gauge.name = "BQ27220 and external power";
    gauge.sample = gauge_sample;
    gauge.sample_interval_ms = 2000;
    gauge.fields = battery;
    gauge.field_count = sizeof(battery) / sizeof(battery[0]);
#if HAS_MQTT
    gauge.publish_ha = gauge_discovery;
#endif
    registry.add(gauge);
}
