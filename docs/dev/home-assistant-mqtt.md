---
title: Home Assistant and MQTT Integration
description: Independent direct Home Assistant live bindings and MQTT discovery architecture.
---

This project includes an optional MQTT integration designed for Home Assistant (HA) using **MQTT Discovery**.

Key design choices:
- **Single JSON state topic** (batched payload) for efficiency
- **HA MQTT Discovery** for auto-creating entities
- **Retained** discovery + state so HA can restart without losing entities/values
- System/health entities are marked as **Diagnostic** in HA (`entity_category: diagnostic`)

## Direct HA live bindings

`HAS_HA_LIVE` gates direct live entity access independently of `HAS_MQTT` and
defaults to `(HAS_DISPLAY && HAS_PSRAM)`.
The existing `ha_url` and write-only `ha_token` settings serve live state,
REST service actions, and Recorder history. A successful configuration save
safely applies the HA client's URL/token settings without reboot. There is no
separate live credential or fixed compile-time entity list. The Binding Reference & Builders
dialog provides [assisted HA binding authoring](../user/integrations/home-assistant.md#build-an-ha-binding)
through an independent, on-demand lookup worker.

Bindings use `[ha:entity_id;path;format]`. Empty or `.` paths select `state`;
explicit `state`, `attributes.brightness`, and other dotted paths select values
from the entity state document. Formatting and pipe fallbacks follow MQTT
binding semantics. `unknown`, `unavailable`, missing entities, and disconnected
data resolve as unavailable rather than ordinary state strings. Removal also
invalidates the value; a quiet, connected entity's value does not expire.

```text
[ha:sensor.living_room_temperature;;%.1f C|---]
[ha:light.living_room;attributes.brightness;%.0f|---]
```

The live client uses HTTP/plain WebSocket only. An HTTPS URL is not downgraded
and cannot establish a live connection; REST actions and history retain their
existing behavior. Use a trusted local network and a suitably restricted HA
account: bearer credentials and entity data cross the network in plaintext.

### Bounds and ownership

The client requires PSRAM and supports at most 64 entities. Each retained
serialized entity snapshot occupies at most 2048 bytes, including its NUL
terminator. The 15 KiB message limit applies to the full reassembled message,
not separately to each WebSocket fragment. Attribute-heavy messages can reach
this bound before the entity cap. JSON parsing has a shared, bounded 64 KiB
allocation budget across parser documents. These are bounded current-state
snapshots, not an event queue or Recorder history.

Runtime resources and cache storage are allocated lazily when credentials are
configured and an entity is referenced. The worker has an 8192-byte PSRAM stack;
the main cache uses strictly PSRAM. WebSockets' transient `malloc` allocations
may still use internal RAM: this is not a zero-internal-RAM transport.

The background worker owns sockets, authentication, JSON parsing, and
reconnects. Snapshot copies are protected by a mutex; binding resolution never
runs in the network task or performs network requests from rendering code.
The shared pad-source scanner holds the existing display lock while setting
and restoring named-binding context, including when MQTT invokes it from the
web task.
Configuration changes are applied at safe worker checkpoints. OTA activity
pauses competing live work.

The client issues a separate `subscribe_entities` command per entity. Reference
collection scans all saved pads, including named bindings and nested expression
bindings, rather than only the displayed pad. Preview references expire after
60 seconds unless referenced again; this reference lifetime does not expire
quiet values of entities that remain subscribed.

A successful host test does not establish device memory headroom or HA server
compatibility.

### Connection diagnostics

`GET /api/config` exposes `capabilities.ha_live`. On supported builds,
authenticated `GET /api/ha/live` returns:

```json
{
  "enabled": true,
  "configured": true,
  "state": "connected",
  "entity_count": 2,
  "connected": true,
  "last_error": "",
  "messages": 12,
  "reconnects": 0
}
```

States are `disabled`, `unconfigured`, `idle`, `connecting`, `authenticating`,
`subscribing`, `connected`, `retrying`, `paused`, and `error`. Check `last_error`
when authentication, URL validation, resource bounds, or subscription fails;
`connected` alone does not prove an individual entity is readable.

Normal logs retain connection outcomes and failures. Select `HALive` with
`LOG_DIAGNOSTICS` at DEBUG level for allocation details, expected transition
messages, and five-minute memory summaries. See the
[logging guidelines](logging-guidelines.md#selecting-diagnostics).

The Home Assistant portal fragment hides live diagnostics unless the capability
is true. It polls at five-second intervals without overlapping requests, bounds
each request to eight seconds, skips polling in hidden tabs, and cancels polling
when navigating away. The local portal server supplies deterministic fixtures;
it never contacts an HA instance.

Direct state does not publish device entities into HA and does not replace
MQTT discovery, telemetry, commands, or `[mqtt:...]` bindings. MQTT remains usable
without HA credentials. Existing MQTT screensaver-wake and camera-trigger source
plumbing is not proactively extended to HA live sources; live binding support
does not imply parity with every MQTT-specific feature.
See the [user setup guide](../user/integrations/home-assistant.md)
for configuration and examples.

## Prerequisites

- A reachable MQTT broker (Mosquitto, HA add-on, etc.)
- Home Assistant with the MQTT integration enabled
- Firmware built with `HAS_MQTT` enabled

## Enable/Disable MQTT in Firmware

MQTT support is controlled at compile time:
- Default is defined in `src/app/board_config.h`
- You can override per-board via `src/boards/<board>/board_overrides.h`

When `HAS_MQTT` is disabled:
- The UI hides MQTT settings (capability flag)
- MQTT code is compiled out

## Configure MQTT (Web Portal)

Go to the **Network** page and fill in:
- `MQTT Host` (required to enable MQTT)
- `MQTT Port` (optional, default 1883)
- `MQTT Username` / `MQTT Password` (optional)
- Set the publish cadence via **MQTT Publish Interval (seconds)** on the Network page (Always-On mode only; Duty-Cycle publishes once per wake)

Behavior:
- If `MQTT Host` is empty: device will not connect.
- If `MQTT Host` is set:
  - Device connects and publishes availability + discovery.
  - Device publishes **one retained** state payload right after connect.
  - If `MQTT Publish Interval > 0` (Always-On): it also republishes state periodically.

## Topics

The device derives a **sanitized name** from the configured device name and uses it to build topics.

- Base topic: `devices/<sanitized>`
- Availability (LWT): `devices/<sanitized>/availability` (retained `online` / `offline`)
- State (JSON): `devices/<sanitized>/health/state` (retained JSON)
- Button events: `devices/<sanitized>/event` (non-retained JSON per press)

Home Assistant discovery topics:
- `homeassistant/sensor/<sanitized>/<object_id>/config` (retained)
- `homeassistant/binary_sensor/<sanitized>/<object_id>/config` (retained)
- `homeassistant/event/<sanitized>/button_press/config` (retained)

## State Payload (JSON)

The state topic publishes one JSON document that contains multiple fields (examples):
- `uptime_seconds`
- `reset_reason`
- `cpu_usage`
- `cpu_temperature`
- `heap_free`, `heap_min`, `heap_largest`, `heap_fragmentation`
- `heap_internal_free`, `heap_internal_min`, `heap_internal_largest`
- `psram_free`, `psram_min`, `psram_largest`, `psram_fragmentation`
- `flash_used`, `flash_total`
- `fs_mounted`, `fs_used_bytes`, `fs_total_bytes`
- `display_fps`, `display_lv_timer_us`, `display_present_us` (when `HAS_DISPLAY`)
- `wifi_rssi`

Note:
- The web API `/api/health` includes additional `mqtt_*` self-report fields for debugging.
- The MQTT state payload intentionally omits those `mqtt_*` fields; consumers should use the MQTT availability/LWT topic as the source of truth.

Home Assistant entities use `value_template` to extract a single field, e.g.

- `{{ value_json.cpu_usage }}`

## Adding Custom Sensors (Step-by-Step)

This project is intentionally lightweight: add a JSON key + add a discovery entry.

### 1) Add your JSON fields to the MQTT payload

Edit `src/app/device_telemetry.cpp` in `device_telemetry_fill_mqtt()`.

Example (ambient sensor):

```cpp
// Example: external temperature/humidity
// doc["temperature"] = 23.4;
// doc["humidity"] = 55.2;
```

Notes:
- `cpu_temperature` is reserved for SoC/internal temperature.
- `temperature` is intended for external/ambient temperature.

### 2) Register Home Assistant entities via discovery

Edit `src/app/ha_discovery.cpp` in `ha_discovery_publish_health()` (kept at the top of the file).

Example (normal Sensors category):

```cpp
// publish_sensor_config(mqtt, "temperature", "Temperature", "{{ value_json.temperature }}", "°C", "temperature", "measurement", nullptr);
// publish_sensor_config(mqtt, "humidity", "Humidity", "{{ value_json.humidity }}", "%", "humidity", "measurement", nullptr);
```

Tip:
- Pass `"diagnostic"` as the last argument to put an entity in HA’s Diagnostic category.
- Pass `nullptr` (or omit the argument if you add your own wrapper) to keep it as a normal Sensor.

### 3) Build and flash

Use a board with `HAS_MQTT` enabled, such as the C6 sensor board below.
The default `esp32c3-withsensors` build disables MQTT and MCP to preserve
OTA flash space and uses BTHome BLE telemetry instead.

```bash
./build.sh firebeetle2-esp32c6-aht10
./upload.sh firebeetle2-esp32c6-aht10
./monitor.sh
```

### 4) If entities don’t update in Home Assistant

Discovery and state are **retained**, so HA may keep old configs if you change them.

Typical reset options:
- Remove the device/entities in HA and reboot the device.
- Or delete retained discovery topics under `homeassistant/sensor/<sanitized>/.../config` and reboot.

## Event-Driven Sensors (Presence)

Sometimes you want **most telemetry** to keep publishing on an interval, but a specific sensor (like **presence**) to update **immediately when it changes**.

There are two viable approaches.

### Option A (Quick + Simple): Force-publish the existing JSON state immediately

This keeps the “single JSON state topic” design intact:
- Leave your existing interval publishing enabled (`MQTT Publish Interval (seconds) > 0`).
- When presence changes, publish an immediate update by republishing the same retained JSON state topic.

Tradeoff:
- Presence will still be present in the JSON payload, so it will also get re-sent during interval publishes (even if unchanged).

**Recommended when:** you don’t care about presence being included in periodic publishes.

**Example (called from your sensor code on edge):**

```cpp
#include <ArduinoJson.h>
#include "device_telemetry.h"

// mqtt is your MqttManager instance
void publish_presence_edge_force_health_state(MqttManager &mqtt) {
    if (!mqtt.connected()) return;

    JsonDocument doc;
    device_telemetry_fill_mqtt(doc);
    mqtt.publishJson(mqtt.healthStateTopic(), doc, true);
}
```

Where to store the presence value:
- Keep the latest presence state in a global/module-level variable (or getter) that `device_telemetry_fill_mqtt()` reads when it builds the payload.

### Option B (Recommended): Separate presence topic (event-driven) + keep interval telemetry

This keeps interval telemetry clean and makes presence fully event-driven:
- Keep the existing interval JSON telemetry on: `devices/<sanitized>/health/state`
- Add a dedicated presence topic:
  - `devices/<sanitized>/presence/state` with payload `ON` / `OFF` (retained)
- Register presence in HA as a **binary_sensor** that points at that topic

Benefits:
- Presence updates only when it changes (no periodic republish).
- HA gets the right entity type (binary sensor) and semantics.

**Suggested additions (minimal API surface):**

1) Add a helper publish function (example) somewhere in your app code:

```cpp
// mqtt is your MqttManager instance
// presence is your boolean state
void publish_presence_state(MqttManager &mqtt, bool presence) {
    if (!mqtt.connected()) return;

    char topic[160];
    snprintf(topic, sizeof(topic), "%s/presence/state", mqtt.baseTopic());

    mqtt.publishImmediate(topic, presence ? "ON" : "OFF", true);
}
```

2) Add HA discovery for the presence binary_sensor.

Today, [src/app/ha_discovery.cpp](src/app/ha_discovery.cpp) publishes both `sensor` and `binary_sensor` discovery entries for the default health payload.
For presence, the recommended pattern is publishing a **separate presence state topic**, then registering a presence `binary_sensor` that points at that topic:
- topic: `homeassistant/binary_sensor/<sanitized>/<object_id>/config`
- `stat_t`: `~/presence/state`
- `pl_on`: `ON`
- `pl_off`: `OFF`

Example config payload shape (pseudo-code):

```cpp
// publish_binary_sensor_config(mqtt,
//     "presence",
//     "Presence",
//     "presence",   // device_class
//     nullptr        // entity_category
// );
```

Operational notes:
- Use retained publishes for presence so HA can recover the last known state after restart.
- Add basic debouncing in your sensor logic so you don’t spam MQTT during noisy transitions.
## Button Press Events

When `HAS_DISPLAY` and `HAS_MQTT` are both enabled, pad button taps and long-presses are automatically published as **Home Assistant Event entities**. A single discovery message covers all buttons — no per-button config bloat.

### Discovery (retained, once per boot)

Topic: `homeassistant/event/<sanitized_name>/button_press/config`

```json
{
  "~": "devices/<sanitized_name>",
  "name": "Button Press",
  "object_id": "<sanitized_name>_button_press",
  "uniq_id": "<sanitized_name>_button_press",
  "stat_t": "~/event",
  "event_types": ["press", "hold"],
  "avty_t": "~/availability",
  "pl_avail": "online",
  "pl_not_avail": "offline",
  "dev": { "ids": ["<sanitized_name>"], "name": "...", "mdl": "...", "sw": "..." }
}
```

### Event payload (non-retained, per button press)

Topic: `devices/<sanitized_name>/event`

```json
{
  "event_type": "press",
  "page": 0,
  "col": 1,
  "row": 0,
  "label": "Lights"
}
```

| Field        | Description                                      |
|------------- |--------------------------------------------------|
| `event_type` | `press` (tap) or `hold` (long-press)             |
| `page`       | Zero-based page index                            |
| `col`        | Zero-based column                                |
| `row`        | Zero-based row                                   |
| `label`      | Most prominent label text (omitted if no label)  |

### HA entity

The entity appears as `event.<sanitized_name>_button_press` under your device's **Events** section.

### Example automation — react to a specific button

```yaml
alias: "Toggle Lights on Pad Button"
triggers:
  - trigger: state
    entity_id: event.<sanitized_name>_button_press
conditions:
  - condition: template
    value_template: >
      {{ trigger.to_state.attributes.label == 'Lights'
         and trigger.to_state.attributes.event_type == 'press' }}
actions:
  - action: light.toggle
    target:
      entity_id: light.living_room
```

### Example automation — log all button presses

```yaml
alias: "Log All Pad Button Presses"
triggers:
  - trigger: state
    entity_id: event.<sanitized_name>_button_press
actions:
  - action: system_log.write
    data:
      message: >
        Button {{ trigger.to_state.attributes.event_type }} on
        page={{ trigger.to_state.attributes.page }}
        col={{ trigger.to_state.attributes.col }}
        row={{ trigger.to_state.attributes.row }}
        label={{ trigger.to_state.attributes.label | default('?') }}
      level: warning
```
## Troubleshooting

- Nothing appears in HA:
  - Verify MQTT broker address/credentials.
  - Check serial logs for `[MQTT] Connected` and `Publishing HA discovery`.
- Entities exist but values are `unknown`:
  - Ensure the state topic is being published (`devices/<sanitized>/health/state`).
  - Verify the JSON key matches the `value_template`.
- You changed names and got duplicate entities:
  - Clear retained discovery topics and reboot.
