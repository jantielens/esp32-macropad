# Home Assistant Integration Guide

The ESP32 Macropad integrates with Home Assistant over **two independent paths**. Use either, both, or neither — they do not depend on each other.

| Path | Direction | Needs | Enables |
|------|-----------|-------|---------|
| **Service Actions** (REST API) | device → HA | HA URL + long-lived token | The *Home Assistant Service* button action — call any HA service. See [Service Actions](#service-actions-rest-api). |
| **MQTT** | device ↔ HA | MQTT broker (configured on the web portal's MQTT page) | Auto-discovery, live HA state, `[mqtt:…]` bindings, and remote control. |

Service Actions talk to Home Assistant's HTTP API and require no MQTT broker. Everything in the MQTT path below requires only a connected broker — no token.

## What MQTT Unlocks

Once an MQTT broker is connected, these capabilities become available. Each is independent — you can use any subset.

| Capability | Direction | Description |
|------------|-----------|-------------|
| **Auto-Discovery & State** | device → HA | The device advertises its entities (sensors, diagnostics, buttons, siren, volume…) under the `homeassistant/` prefix with no manual YAML, and keeps publishing their live values. |
| **Statestream** | HA → device | HA's `mqtt_statestream` integration publishes every entity's state to MQTT so the device can read it. |
| **`[mqtt:…]` Bindings** | any topic → device | Button labels, colors, and widgets subscribe to any MQTT topic (Statestream or any other publisher) to show live data. Works without auto-discovery or Statestream. |
| **Remote Control** | HA → device | Control entities let HA drive the device: screen-select navigation, screensaver wake, notification messages, and (on audio boards) siren, volume, and beep buttons. |

> **Prerequisites (MQTT path)**: MQTT broker configured and connected, Home Assistant MQTT integration enabled.

---

## Audio

The audio integration exposes the device speaker as a set of Home Assistant entities. This requires a board with audio hardware (compile-time flag `HAS_AUDIO`).

### Entities

| Entity | Type | Description |
|--------|------|-------------|
| **Siren** | `siren` | Looping tone with turn on/off, tone selection, volume override, and optional duration |
| **Volume** | `number` | Device volume (0–100%), persisted to NVS across reboots |
| **Custom Tone** | `text` | Beep pattern DSL string used when the siren tone is set to "custom" |
| **Beep** | `button` | One-shot single beep at device volume |
| **Beep Double** | `button` | One-shot double beep |
| **Beep Triple** | `button` | One-shot triple beep |

### Siren Tones

The siren entity has a dropdown with five available tones:

| Tone | Pattern | Description |
|------|---------|-------------|
| `default` | 1000 Hz, 200 ms | Single tone, repeated |
| `alert` | 1000 Hz × 3, 150 ms each | Rapid triple beep, repeated |
| `doorbell` | 800 Hz → 1200 Hz | Ding-dong, repeated |
| `warning` | 500 Hz → 800 Hz | Two-tone rising, repeated |
| `custom` | *(from Custom Tone entity)* | User-defined pattern |

All tones loop continuously until turned off or until the optional duration expires.

### Beep Pattern DSL

The Custom Tone entity accepts a beep pattern DSL string. The syntax is space-separated segments:

- **`freq:dur`** — Play a tone at `freq` Hz for `dur` milliseconds (e.g., `1000:200`)
- **`dur`** — Silent gap for `dur` milliseconds (e.g., `100`)

**Examples:**

| Pattern | Description |
|---------|-------------|
| `1000:200` | Single 1 kHz beep, 200 ms |
| `1000:200 100 2000:300` | 1 kHz beep, 100 ms gap, 2 kHz beep |
| `800:150 1200:300` | Two-tone doorbell-style |
| `440:500 100 440:500 100 440:500` | Three A4 notes |

When used as a siren tone, a trailing gap controls the pause between loop iterations (e.g., `1000:200 800` gives a 200 ms beep then 800 ms silence per loop).

---

### Basic Examples

#### Play a Preset Tone

```yaml
action: siren.turn_on
target:
  entity_id: siren.esp32_macropad_keuken_siren
data:
  tone: "doorbell"
```

#### Play a Preset Tone for 5 Seconds

```yaml
action: siren.turn_on
target:
  entity_id: siren.esp32_macropad_keuken_siren
data:
  tone: "alert"
  duration: "5"
```

#### Stop the Siren

```yaml
action: siren.turn_off
target:
  entity_id: siren.esp32_macropad_keuken_siren
```

#### Trigger a One-Shot Beep

```yaml
action: button.press
target:
  entity_id: button.esp32_macropad_keuken_beep
```

#### Set Volume

```yaml
action: number.set_value
target:
  entity_id: number.esp32_macropad_keuken_volume
data:
  value: "75"
```

#### Play a Siren at a Specific Volume

The `siren.turn_on` action accepts a volume override (0.0–1.0) that applies only for that playback without changing the device volume:

```yaml
action: siren.turn_on
target:
  entity_id: siren.esp32_macropad_keuken_siren
data:
  tone: "warning"
  volume_level: 0.5
```

---

### Custom Tones

To play a custom beep pattern, first set the pattern via the **Custom Tone** text entity, then activate the siren with `tone: "custom"`.

#### Set and Play a Custom Tone

```yaml
# Step 1: Set the custom tone pattern
action: text.set_value
target:
  entity_id: text.esp32_macropad_keuken_custom_tone
data:
  value: "2000:100 100 1500:100 100 1000:100"

# Step 2: Play it via the siren
action: siren.turn_on
target:
  entity_id: siren.esp32_macropad_keuken_siren
data:
  tone: "custom"
  duration: "3"
```

The custom tone pattern is retained until changed — you can trigger `tone: "custom"` repeatedly without re-setting the pattern.

---

### Automation Examples

#### Doorbell Notification

Play a doorbell sound for 3 seconds when a door sensor triggers:

```yaml
alias: Doorbell chime
triggers:
  - trigger: state
    entity_id: binary_sensor.front_door
    to: "on"
actions:
  - action: siren.turn_on
    target:
      entity_id: siren.esp32_macropad_keuken_siren
    data:
      tone: "doorbell"
      duration: "3"
```

#### Custom Alert with Dynamic Pattern

Set a custom pattern and play it when a temperature threshold is exceeded:

```yaml
alias: Temperature alert
triggers:
  - trigger: numeric_state
    entity_id: sensor.living_room_temperature
    above: 30
actions:
  - action: text.set_value
    target:
      entity_id: text.esp32_macropad_keuken_custom_tone
    data:
      value: "2000:100 100"
  - action: siren.turn_on
    target:
      entity_id: siren.esp32_macropad_keuken_siren
    data:
      tone: "custom"
      duration: "3"
```

#### Quiet Hours Volume Control

Lower the volume at night and restore it in the morning:

```yaml
alias: Quiet hours volume
triggers:
  - trigger: time
    at: "22:00:00"
actions:
  - action: number.set_value
    target:
      entity_id: number.esp32_macropad_keuken_volume
    data:
      value: "20"
```

```yaml
alias: Morning volume restore
triggers:
  - trigger: time
    at: "07:00:00"
actions:
  - action: number.set_value
    target:
      entity_id: number.esp32_macropad_keuken_volume
    data:
      value: "70"
```

#### Confirmation Beep on Action

Add a beep to confirm an automation ran (e.g., after locking the front door):

```yaml
alias: Lock confirmation
triggers:
  - trigger: state
    entity_id: lock.front_door
    to: "locked"
actions:
  - action: button.press
    target:
      entity_id: button.esp32_macropad_keuken_beep_double
```

### Behavior Notes

- **Re-trigger**: Sending `siren.turn_on` while the siren is already playing replaces the current tone immediately.
- **Beep interrupts siren**: Pressing a beep button while the siren is playing stops the siren and plays the one-shot beep instead.
- **Volume override vs device volume**: The `volume_level` parameter on `siren.turn_on` is a temporary override (0.0–1.0). The `number.set_value` on the Volume entity changes the persistent device volume for all future playback. Volume changes from HA are immediately reflected in the web portal and vice versa.
- **Duration**: When specified, the siren automatically stops after the given number of seconds and publishes an OFF state. Without a duration, the siren loops indefinitely until explicitly turned off.
- **Touch feedback audio cues**: The device also supports configurable beep patterns for button tap and long-press events, configured in the web portal's Home page Audio section (not via HA entities). These use the same beep pattern DSL and respect the HA-controllable device volume. See the [Web Portal Guide](web-portal-guide.md#audio) and [Pad Editor Guide](pad-editor-guide.md#audio-feedback) for details.

---

## Notifications

The notification integration lets you display floating message bubbles on the device screen from Home Assistant. This requires a board with a display (compile-time flag `HAS_DISPLAY`).

### Entity

| Entity | Type | Description |
|--------|------|-------------|
| **Notify** | `text` | Display a notification on the device screen. Accepts plain text or JSON. |

### Usage

The Notify text entity accepts two formats:

**Plain text** — displays the message with default styling (white text, dark background, bottom position, 3-second duration):

```yaml
action: text.set_value
target:
  entity_id: text.esp32_macropad_keuken_notify
data:
  value: "Dryer finished!"
```

**JSON** — full control over appearance:

```yaml
action: text.set_value
target:
  entity_id: text.esp32_macropad_keuken_notify
data:
  value: '{"text": "Power high!", "duration_ms": 5000, "text_color": "#ffa0a0", "bg_color": "#2e1a1a", "location": "top"}'
```

**Dismiss** — send an empty string to dismiss the active notification:

```yaml
action: text.set_value
target:
  entity_id: text.esp32_macropad_keuken_notify
data:
  value: ""
```

### JSON Fields

All fields are optional except `text`.

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `text` | string | *(required)* | The notification message |
| `duration_ms` | number | `3000` | Display duration in milliseconds. `0` = persistent (tap to dismiss) |
| `text_color` | string | `#ffffff` | Hex color for the text |
| `bg_color` | string | `#333333` | Hex color for the background |
| `border_color` | string | *(none)* | Hex color for a border. Omit for no border |
| `opacity` | number | `85` | Background opacity, 0–100 |
| `font_size` | number | `0` (auto) | Font pixel size: 12, 14, 18, 24, 32, 36, or 48. `0` uses the scale-tier default |
| `location` | string | `bottom` | `top`, `center`, or `bottom` |

### MQTT Topics

| Topic | Direction | Description |
|-------|-----------|-------------|
| `~/notify/set` | Subscribe | Receives plain text or JSON |
| `~/notify/state` | Publish | Last notification text (retained) |

### Automation Examples

#### Washer Done Alert

Show a persistent notification when the washing machine finishes:

```yaml
alias: Washer done notification
triggers:
  - trigger: state
    entity_id: sensor.washing_machine_status
    to: "idle"
actions:
  - action: text.set_value
    target:
      entity_id: text.esp32_macropad_keuken_notify
    data:
      value: '{"text": "Washer done! 🧺", "duration_ms": 0, "bg_color": "#1a3a1a", "location": "center"}'
```

#### Temperature Warning

Flash a red warning at the top of the screen when temperature exceeds a threshold:

```yaml
alias: Temperature warning
triggers:
  - trigger: numeric_state
    entity_id: sensor.living_room_temperature
    above: 30
actions:
  - action: text.set_value
    target:
      entity_id: text.esp32_macropad_keuken_notify
    data:
      value: '{"text": "🔥 Temperature: {{ states(\"sensor.living_room_temperature\") }}°C", "duration_ms": 10000, "text_color": "#ff4444", "bg_color": "#3a1a1a", "border_color": "#5a2a2a", "location": "top"}'
```

#### Dismiss After Action

Clear a persistent notification after the user acknowledges it in HA:

```yaml
alias: Clear macropad notification
triggers:
  - trigger: event
    event_type: call_service
    event_data:
      domain: input_boolean
      service: turn_on
actions:
  - action: text.set_value
    target:
      entity_id: text.esp32_macropad_keuken_notify
    data:
      value: ""
```

### Behavior Notes

- **Re-trigger**: Sending a new notification while one is active replaces it immediately — the old bubble is destroyed and the new one fades in.
- **Tap to dismiss**: Users can always tap the bubble to dismiss it, regardless of the configured duration.
- **Persistent mode**: Set `duration_ms` to `0` for notifications that stay on screen until tapped or replaced. The bubble still responds to tap-to-dismiss.
- **Screensaver**: Notifications display on `lv_layer_top()` — they appear above all screens including the screensaver overlay.
- **State topic**: The device publishes the last notification text (retained) to `~/notify/state`. An empty state means no active notification.

---

## Service Actions (REST API)

In addition to publishing MQTT, the device can call Home Assistant **services** directly over the REST API. This lets a button (or swipe, boot, or timer-expire action) toggle a light, run a scene, open a cover, or invoke any other HA service — without an MQTT round-trip.

Unlike the MQTT integration above, this path talks to Home Assistant's HTTP API in the outbound direction and does not require an MQTT broker.

### Setup

On the web portal's **Home Assistant** page, configure:

| Field | Description |
|-------|-------------|
| **Home Assistant URL** | Base URL including scheme and port, e.g. `http://192.168.1.50:8123`. HTTPS is supported (the certificate is not verified). Leave empty to disable service actions. |
| **Long-Lived Access Token** | Created in Home Assistant under your user profile → **Security** → **Long-lived access tokens**. Stored on the device and never returned by the API. |

Both values persist to NVS (`ha_url` / `ha_token`).

### Action Fields

When you add a **Home Assistant Service** button action, you configure:

| Field | Description |
|-------|-------------|
| **Entity ID** | The target entity, e.g. `light.living_room`. The service **domain** is derived from the text before the first `.`. |
| **Service** | The service within that domain, e.g. `toggle`, `turn_on`, `turn_off`. |
| **Service Data (JSON)** | Optional JSON object merged into the request body alongside `entity_id`, e.g. `{"brightness_pct": 60}`. |

The device issues:

```http
POST <ha_url>/api/services/<domain>/<service>
Authorization: Bearer <ha_token>
Content-Type: application/json

{"entity_id": "<entity_id>", ...<service data>}
```

### Examples

| Goal | Entity ID | Service | Service Data |
|------|-----------|---------|--------------|
| Toggle a light | `light.living_room` | `toggle` | *(empty)* |
| Turn a light on at 75% | `light.kitchen` | `turn_on` | `{"brightness_pct": 75}` |
| Run a scene | `scene.movie_night` | `turn_on` | *(empty)* |
| Open a cover | `cover.garage` | `open_cover` | *(empty)* |

### Behavior Notes

- **Non-blocking (display)**: The HTTP request is queued from the UI task and executed on the main loop, so calling a service never stalls the display.
- **No bindings**: The entity ID, service, and service-data fields are stored literally — binding templates (`[mqtt:...]`, `[health:...]`, etc.) are **not** resolved in these fields.
- **Display required (for now)**: Service calls are dispatched from the display action loop, so this action runs on boards with a display. Headless button-only boards can be configured for the action but do not yet drain the queued call.
- **Configuration**: See the [Pad Editor Guide](pad-editor-guide.md#home-assistant-service-action) for the in-editor walkthrough.
