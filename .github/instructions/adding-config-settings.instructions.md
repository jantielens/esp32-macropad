---
description: "Step-by-step checklist for adding new NVS configuration settings to backend, REST API, and web portal frontend"
applyTo: "**/config_manager.*, **/web_portal.cpp, **/web_portal.h, **/web/*.html, **/web/portal.js"
---

# Adding New Configuration Settings

When adding new configuration settings (e.g., MQTT, custom features), follow this complete checklist. For more details on the web portal architecture and REST API, see `docs/dev/web-portal.md`.

## 1. Backend: Configuration Storage

**Update `config_manager.h`:**

- Add `#define` constants for maximum field lengths (e.g., `CONFIG_MQTT_BROKER_MAX_LEN`)
- Add new fields to the `DeviceConfig` struct
- For strings: Use `char field_name[CONFIG_XXX_MAX_LEN]`
- For numbers: Use appropriate types (`uint16_t`, `int`, `float`, etc.)

**Update `config_manager.cpp`:**

- Add `#define` keys for NVS storage (e.g., `KEY_MQTT_BROKER "mqtt_broker"`)
- Update `config_manager_load()` to load new fields from NVS
  - Use `preferences.getString()` for strings
  - Use `preferences.getUShort()`, `preferences.getInt()`, etc. for numbers
  - Provide sensible defaults (second parameter)
- Update `config_manager_save()` to save new fields to NVS
  - Use `preferences.putString()` for strings
  - Use `preferences.putUShort()`, `preferences.putInt()`, etc. for numbers
- Update `config_manager_print()` to log new settings for debugging

## 2. Backend: Web API

**Update `web_portal.cpp`:**

- In `handleGetConfig()`: Add new fields to JSON response
  - Use `doc["field_name"] = config->field_name`
  - For passwords: Return empty string (`doc["password_field"] = ""`)
- In `handlePostConfig()`: Handle new fields from JSON request
  - Use `if (doc.containsKey("field_name"))` for partial updates
  - Use `doc["field_name"] | default_value` syntax for safe extraction
  - Handle passwords specially (only update if non-empty)

## 3. Frontend: HTML Form

**Update appropriate HTML page (e.g., `network.html`, `home.html`):**

- Add form section with descriptive heading
- Add input fields with proper attributes:
  - `id` and `name` must match the backend field name exactly
  - `type` (text, number, password, etc.)
  - `maxlength` should match the backend max length constant
  - `placeholder` with helpful examples
  - `required` attribute if field is mandatory
- Add `<small>` helper text under each field
- Use `.grid-2col` class for side-by-side layout on desktop

## 4. Frontend: JavaScript

**Update `portal.js`:**

- In `buildConfigFromForm()` function:
  - Add new field names to the `fields` array
  - Fields are automatically read from form inputs by the existing code
- In `loadConfig()` function:
  - Add `setValueIfExists('field_name', config.field_name)` calls
  - For passwords: Set placeholder text if saved, leave value empty
  - For numbers: Use `setValueIfExists()` with numeric values
- Optionally add validation in `validateConfig()` if needed

## 5. Usage in Application Code

**Initialize with loaded config:**

```cpp
// In setup() or after config_manager_load()
if (strlen(device_config.mqtt_broker) > 0) {
    some_manager_init(&device_config);
}
```

**Access configuration:**

```cpp
Serial.printf("Broker: %s:%d\n", device_config.mqtt_broker, device_config.mqtt_port);
```

## Common Mistakes to Avoid

- Forgetting to update `portal.js` fields array — settings won't be saved
- Mismatched field names between HTML `id`, JS, and backend — data won't transfer
- Not rebuilding after HTML/JS changes — old code still embedded in firmware
- Missing default values in load function — uninitialized data
- Not using `doc.containsKey()` in POST handler — can't do partial updates
