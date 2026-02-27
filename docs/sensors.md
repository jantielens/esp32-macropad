# Sensors (Developer Guide)

## Overview of Sensor Pattern
This project uses a **registry-based sensor pattern** that keeps sensor implementations isolated in `src/app/sensors/`. Only a single registration point in `src/app/sensors.cpp` uses compile-time flags to include sensor adapters, which keeps the main app clean and avoids scattered `#if HAS_SENSOR_*` logic.

Key points:
- Each sensor adapter is a small wrapper around a library (no custom low-level driver).
- Adapters append their values into a JSON object (for `/api/health` and MQTT).
- Home Assistant discovery is still supported via the adapter.
- Only enabled sensors are compiled (guarded in `src/app/sensors.cpp`).

Optional testing helper:
- A dummy sensor can be enabled to emit a synthetic `dummy_value` for UI/MQTT/BLE testing.


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
When a sensor is missing or has invalid readings, adapters may emit sentinel values (some sensors use min-range values instead of `null`).
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
- **Sensor missing**: `/api/health` may show `null` or sentinel values under `sensors`.
- **Wrong I2C address**: Try `0x76` and `0x77` (depending on SDO wiring).
- **No values in portal**: Ensure sensor is enabled in board overrides and device rebuilt.
- **Library missing**: add to `arduino-libraries.txt` and run `./setup.sh`.
