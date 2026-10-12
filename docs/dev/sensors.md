---
title: Sensors Developer Guide
description: Register local sensors with sampling intervals, shared cached readings, and binding metadata.
---

## Overview of Sensor Pattern
This project uses a **registry-based sensor pattern** that keeps sensor implementations isolated in `src/app/sensors/`. Only a single registration point in `src/app/sensors.cpp` uses compile-time flags to include sensor adapters, which keeps the main app clean and avoids scattered `#if HAS_SENSOR_*` logic.

Key points:
- Each sensor adapter is a small wrapper around a library or the board hardware
  facade; board-specific register transactions stay in that facade.
- Adapters declare their fields and sampling interval, and produce one JSON sample.
- The manager schedules sampling and shares cached readings with the API, MQTT,
  and `[sensor:key;format]` bindings; consumers never trigger hardware reads.
- Home Assistant discovery is still supported via the adapter.
- Only enabled sensors are compiled (guarded in `src/app/sensors.cpp`).

Optional testing helper:
- A dummy sensor can be enabled to emit a synthetic `dummy_value` for UI/MQTT/BLE testing.

Battery-powered boards can enable the reusable ADC battery adapter. It averages
calibrated ADC millivolt readings, applies the board's divider and calibration
factor, and publishes voltage plus an approximate resting single-cell LiPo
percentage through the API, MQTT/Home Assistant, and BTHome BLE telemetry.

## Sampling Contract

The sensor owns hardware settings, its sampling callback, field descriptions,
and its requested interval. The manager owns timers, cached values, and locking.
No new task or user-facing sampling setting is required.

- Polled sensors default to `sample_interval_ms = 2000`. Set a different
  interval in the adapter registration for slower hardware.
- The first normal loop samples each sensor immediately. Later loops sample
  only sensors whose intervals have elapsed.
- MQTT/BLE duty-cycle wakes call `sensor_manager_sample_once()` before
  publishing. It samples every sensor once, regardless of interval; repeated
  consumer reads and one-shot calls do not sample again in that wake.
- An event sensor sets `event_driven = true` and supplies a `loop` callback
  returning `true` when its sampled fields changed. Existing event publishing
  remains in that callback; the manager refreshes its cache on the same loop.
- Sampling pauses during OTA. Pending event snapshots are refreshed afterward.
- Return `null` for failed readings. The manager accepts finite numbers and
  booleans, retaining each field's last good value indefinitely after failure.
  Before the first successful sample, consumers see an unavailable value.
- Cached values live only in RAM and are reset by reboot or deep sleep.
- Declare every field in a static `SensorField` array, including units in the
  description. Only declared fields are cached. The array and strings must
  remain valid for the lifetime of the firmware.
- Duplicate flat keys use the later registered sensor, even if it has never
  produced a valid sample. Prefer unique names for new measurements.

API and MQTT retain their existing field names and native JSON booleans.
Sensors that publish only dedicated event topics can set
`include_in_mqtt = false`; their values remain available in the API and bindings.


## Step-by-Step: Add Your Own Sensor (BME280 Example)
This is the full pattern using the BME280 adapter as reference.

### 1) Add compile-time flags (defaults)
Add defaults in `src/app/board_config.h`:
- `HAS_SENSOR_BME280`
- I2C pins and address

Then enable in a board override:
- `src/boards/<board>/board_overrides.h`

Example:
```cpp
#define HAS_SENSOR_BME280 true
#define SENSOR_I2C_SDA 8
#define SENSOR_I2C_SCL 9
#define BME280_I2C_ADDR 0x76
```

### 2) Add adapter files
Create:
- `src/app/sensors/bme280_sensor.h`
- `src/app/sensors/bme280_sensor.cpp`

The adapter should:
- `begin()` → library init
- `appendJson(JsonObject &doc)` → write values into the JSON object
- `publishHaDiscovery()` → register HA discovery (optional)

Tip: use the sensor framework helpers to keep adapters small:
- `sensor_manager_set_number()` / `sensor_manager_set_bool()` for JSON values
- `ha_discovery_publish_sensor_config()` for numeric HA discovery
- `ha_discovery_publish_binary_sensor_config_with_topic_suffix()` for binary HA discovery

### 3) Register the sensor

Register the sampling contract in the adapter. Its field catalog also generates
binding keys and portal/MCP documentation; do not add a second sensor list to
the binding resolver.

```cpp
static void bme280_sample(JsonObject &doc) {
  g_bme280_adapter.appendJson(doc);
}

void register_bme280_sensor(SensorRegistry &registry) {
  static const SensorField fields[] = {
    {"temperature", "Temperature in degrees Celsius."},
    {"humidity", "Relative humidity in percent."},
    {"pressure", "Atmospheric pressure in hPa."},
  };
  SensorCallbacks callbacks = {};
  callbacks.name = "BME280";
  callbacks.init = bme280_init;
  callbacks.sample = bme280_sample;
  callbacks.sample_interval_ms = 10000;
  callbacks.fields = fields;
  callbacks.field_count = sizeof(fields) / sizeof(fields[0]);
  registry.add(callbacks);
}
```

Omit the interval assignment to use two seconds. Add `publish_ha` under
`#if HAS_MQTT` when the adapter provides discovery. Sampling callbacks run on
the main task, outside the cache lock; use the owning hardware facade's bus
lock when hardware is shared with another task. API and binding callbacks must
not call the adapter directly.

In `src/app/sensors.cpp`, include and register it behind a compile-time flag:
```cpp
#if HAS_SENSOR_BME280
#include "sensors/bme280_sensor.cpp"
#endif

void sensor_manager_register_all(SensorRegistry &registry) {
#if HAS_SENSOR_BME280
    register_bme280_sensor(registry);
#endif
}
```

### Example: LD2410 OUT pin (no UART)
For a simple presence-only LD2410 setup, use its **OUT** pin as a digital input:
- Add `HAS_SENSOR_LD2410_OUT` and `LD2410_OUT_PIN` in your board override.
- Implement an interrupt-based adapter that updates a cached `presence` boolean.
- Expose it in JSON as `sensors.presence` (true/false/null).
- Publish a **dedicated MQTT event topic** (e.g. `devices/<sanitized>/presence/state`) on changes only.
- Publish HA discovery as a `binary_sensor` with device_class `presence` and `stat_t` pointing to that event topic.

### Example: AHT10 (I2C temperature and humidity)

Enable it in your board override:

```cpp
#define HAS_SENSOR_AHT10 true
#define SENSOR_I2C_SDA 19
#define SENSOR_I2C_SCL 20
```

The adapter supports AHT10 and AHT20 devices at address `0x38`. It publishes
`temperature` and `humidity`, registers Home Assistant sensors, and advertises
BTHome temperature and humidity objects when `HAS_BLE` is enabled.

### Example: Battery ADC

Enable the generic adapter with the hardware-specific voltage-divider values:

```cpp
#define HAS_SENSOR_BATTERY_ADC true
#define BATTERY_ADC_PIN 0
#define BATTERY_ADC_DIVIDER 2.0f
#define BATTERY_ADC_CALIBRATION 1.0f
#define BATTERY_ADC_SAMPLE_COUNT 8
```

The adapter publishes `battery_voltage` in volts and `battery_percentage` as an
approximate state of charge for a resting single-cell LiPo. Adjust
`BATTERY_ADC_CALIBRATION` after comparing the reported voltage with a trusted
multimeter reading.

### StopWatch PMIC Battery

The StopWatch board selects
[src/boards/m5stack-stopwatch/stopwatch_battery_sensor.cpp](../../src/boards/m5stack-stopwatch/stopwatch_battery_sensor.cpp)
through `BOARD_SENSOR_SOURCE`. The sketch-root sensor aggregator includes it
and calls `board_register_sensors()`. This opt-in provider uses the existing
sensor registry; generic sensor code does not test board identity. The provider
rejects simultaneous `HAS_SENSOR_BATTERY_ADC` selection to avoid duplicate
battery fields, while reusing its single-cell voltage curve helper.

It reads M5PM1 through the board hardware facade on the already-initialized, locked
internal I2C bus. It does not enable an ESP32 battery ADC or reinitialize Wire.
Battery millivolts come from registers 0x22/0x23; input voltage and the
active-low charger-status GPIO determine charging state.

It publishes `battery_voltage` (volts), `battery_percentage` (the shared
approximate single-cell LiPo voltage curve), and `battery_charging` (boolean)
in `/api/health`'s `sensors` object and the normal MQTT state/discovery path.
Invalid readings produce `null` until a successful sample; subsequent failures
retain the last good values. Percentage is not a fuel-gauge measurement and can vary under load or
while charging. Verify voltage and charging behavior on hardware before
relying on them. Charging-current configuration and power-button defaults are
not changed by telemetry.

### Example: Dummy sensor (synthetic values)
Enable it in your board override:
```cpp
#define HAS_SENSOR_DUMMY true
```
The dummy adapter publishes `dummy_value` (0.00–99.99) and registers a diagnostic HA sensor.

### 4) Ensure library dependency
Add the library to `arduino-libraries.txt`:
```
Adafruit BME280 Library
Adafruit Unified Sensor
```

### 5) Build + test
```
./build.sh <board-name>
./upload.sh <board-name>
./monitor.sh
```


## Sensor JSON Schema (API + MQTT)
### `/api/health`
Sensor values are nested under `sensors`:
```json
{
  "sensors": {
    "temperature": 21.7,
    "humidity": 39.6,
    "pressure": 995.4
  }
}
```
Before a field has a successful sample, it is `null`. Later failures retain
the last good value; BME280 no longer emits missing-sensor sentinels.
```json
{
  "sensors": {
    "temperature": null,
    "humidity": null,
    "pressure": null
  }
}
```
If no sensors are enabled, `sensors` may be an empty object.

### Local Sensor Bindings

Display builds with registered sensor fields provide `[sensor:key;format]`.
For example, `[sensor:temperature;%.1f|N/A]` shows a temperature with one
decimal place, or `N/A` before a successful reading. Booleans resolve as `0`
or `1`, making them usable directly in expressions and numeric widgets.
Formatting accepts one float conversion such as `%.0f`, `%.2f`, or `%g`;
omit it for the default numeric representation. Invalid formats use the default.

The binding reads the shared cache without MQTT, hardware I/O, or sampling
side effects. Available keys and examples come from each adapter's field
catalog. Current-value bindings do not change existing history feature gates.

### MQTT state payload
Sensor fields are **flat** in the MQTT JSON state:
```json
{
  "temperature": 21.7,
  "humidity": 39.6,
  "pressure": 995.4
}
```


## Instant Publishing (Event Sensors)
Some sensors need immediate updates (e.g., motion/presence) rather than waiting for the next MQTT interval.

Recommended pattern:
- Keep periodic telemetry as-is (the existing MQTT interval).
- Publish events to a dedicated topic when a state change occurs.
- Defer ISR work to normal context (e.g., `sensor_manager_loop()`), then publish there.

Note: The template already calls `sensor_manager_loop()` in `app.ino`.

Example:
```cpp
sensor_manager_publish_binary_state("presence/state", presence, true);
```

In Home Assistant, register a **binary_sensor** that points to this event topic (`stat_t`) and uses `pl_on`/`pl_off`.


## Troubleshooting
- **Sensor missing**: `/api/health` shows `null` before the first successful
  sample, or the last good value after a later failure.
- **Wrong I2C address**: Try `0x76` and `0x77` (depending on SDO wiring).
- **No values in portal**: Ensure sensor is enabled in board overrides and device rebuilt.
- **Library missing**: add to `arduino-libraries.txt` and run `./setup.sh`.
