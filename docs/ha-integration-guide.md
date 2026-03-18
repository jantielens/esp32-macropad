# Home Assistant Integration Guide

The ESP32 Macropad integrates with Home Assistant via MQTT auto-discovery. Once connected to your MQTT broker (configured in the web portal's Network page), the device automatically registers its entities in Home Assistant — no manual YAML configuration needed.

> **Prerequisites**: MQTT broker configured and connected, Home Assistant MQTT integration enabled.

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
- **Volume override vs device volume**: The `volume_level` parameter on `siren.turn_on` is a temporary override (0.0–1.0). The `number.set_value` on the Volume entity changes the persistent device volume for all future playback.
- **Duration**: When specified, the siren automatically stops after the given number of seconds and publishes an OFF state. Without a duration, the siren loops indefinitely until explicitly turned off.
