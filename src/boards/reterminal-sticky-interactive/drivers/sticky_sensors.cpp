#include "../sticky_hardware.h"
#include "sensors/sensor_manager.h"
#include "i2c_bus.h"
#include "ha_discovery.h"
#include "log_manager.h"
#include <SensirionI2cSht4x.h>
#include <Wire.h>
#include <math.h>

namespace {
constexpr uint8_t kShtAddress = 0x44;
#if HAS_BATTERY_INFO
constexpr uint8_t kGaugeAddress = 0x55;
constexpr uint8_t kVoltage = 0x08;
constexpr uint8_t kStateOfCharge = 0x2c;
constexpr uint8_t kStatus = 0x0a;
constexpr uint8_t kCurrent = 0x0c;
constexpr uint8_t kTimeToEmpty = 0x16;
constexpr uint8_t kTimeToFull = 0x18;
#endif
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

FailureLog shtFailure;
#if HAS_BATTERY_INFO
FailureLog voltageFailure, chargeFailure, currentFailure, statusFailure, runtimeFailure;
#endif

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

#if HAS_BATTERY_INFO
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

void gauge_sample(BatterySample &sample) {
    uint16_t voltage = 0, charge = 0, current = 0, status = 0, timeEmpty = 0xffff, timeFull = 0xffff;
    bool voltageRead = false, chargeRead = false, currentRead = false, statusRead = false;
    bool emptyRead = false, fullRead = false;
    const bool ready = sticky_hardware_ready();
    if (ready && i2c_bus_lock()) {
        voltageRead = gauge_word(kVoltage, voltage);
        chargeRead = gauge_word(kStateOfCharge, charge);
        currentRead = gauge_word(kCurrent, current);
        statusRead = gauge_word(kStatus, status);
        emptyRead = gauge_word(kTimeToEmpty, timeEmpty);
        fullRead = gauge_word(kTimeToFull, timeFull);
        i2c_bus_unlock();
    }
    const bool voltageValid = voltageRead && voltage >= 2000 && voltage <= 5000;
    const bool chargeValid = chargeRead && charge <= 100;
    voltageFailure.update(!voltageValid, "BQ27220 voltage read invalid or failed (check Wire0 and 0x55)");
    chargeFailure.update(!chargeValid, "BQ27220 charge read invalid or failed (check Wire0 and 0x55)");
    currentFailure.update(!currentRead, "BQ27220 current read failed");
    statusFailure.update(!statusRead, "BQ27220 status read failed");
    runtimeFailure.update(!emptyRead || !fullRead, "BQ27220 runtime read failed");
    if (voltageValid) sample.set(BatteryField::Voltage, voltage / 1000.0f);
    if (chargeValid) sample.set(BatteryField::Percentage, charge);
    // Decode two's complement without unsigned-to-signed overflow.
    const int32_t signedCurrent = current & 0x8000 ? int32_t(current) - 65536 : current;
    if (currentRead) sample.set(BatteryField::Current, signedCurrent / 1000.0f);
    if (ready) sample.set(BatteryField::ExternalPowerPresent, digitalRead(9) == HIGH ? 1 : 0);
    if (statusRead && (status & 0x0008)) {
        if (status & 0x0001) sample.set_status(BatteryStatus::Discharging);
        else if (status & 0x0200) sample.set_status(BatteryStatus::Full);
        else if (currentRead && signedCurrent > 0) sample.set_status(BatteryStatus::Charging);
        else if (currentRead && signedCurrent == 0) sample.set_status(BatteryStatus::Idle);
    }
    if (emptyRead && timeEmpty != 0xffff) sample.set(BatteryField::TimeRemaining, timeEmpty);
    if (fullRead && timeFull != 0xffff) sample.set(BatteryField::TimeToFull, timeFull);
}
#endif

#if HAS_MQTT
void sht_discovery(MqttManager &mqtt) {
    ha_discovery_publish_sensor_config(mqtt, "temperature", "Temperature", "{{ value_json.temperature }}", "°C", "temperature", "measurement", nullptr);
    ha_discovery_publish_sensor_config(mqtt, "humidity", "Humidity", "{{ value_json.humidity }}", "%", "humidity", "measurement", nullptr);
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

#if HAS_BATTERY_INFO
    static const BatteryProvider battery = {
        "BQ27220: Current() is sampled current updated every second, not peak or averaged current. Power uses the same voltage/current sample. Runtime uses native TimeToEmpty/TimeToFull estimates. Shipping current calibration and capacity configuration are unverified; readings require hardware validation. No gauge settings are written.",
        battery_field_mask(BatteryField::Voltage) | battery_field_mask(BatteryField::Percentage) |
            battery_field_mask(BatteryField::Current) | battery_field_mask(BatteryField::Status) |
            battery_field_mask(BatteryField::ExternalPowerPresent) |
            battery_field_mask(BatteryField::TimeRemaining) | battery_field_mask(BatteryField::TimeToFull),
        gauge_sample,
    };
    SensorCallbacks gauge = {};
    gauge.name = "BQ27220 and external power";
    gauge.sample_interval_ms = 2000;
    gauge.battery = &battery;
    registry.add(gauge);
#endif
}
