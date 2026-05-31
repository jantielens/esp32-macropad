<!-- markdownlint-disable-file MD041 -->
# Darkroom Timer

The Darkroom Timer device class turns the ESP32 Macropad firmware into a touch-screen enlarger timer for analog darkroom printing. It runs three independent timing engines (exposure, f-stop test strip, and light meter), meters paper exposure with a TSL2591 light sensor, drives the enlarger and safelight through Wi-Fi relays, and keeps a persistent log of every print made.

## At a glance

- **Reference board**: `jc4880p433-darkroom` (4.8" 480×800 MIPI-DSI, GT911 touch, ESP32-P4, 16 MB flash, 32 MB PSRAM)
- **Sensor**: TSL2591 high-dynamic-range light sensor for paper-grade and enlargement-factor metering
- **Relays**: Shelly Wi-Fi relays (HTTP) for the enlarger and safelight — no mains wiring on the controller
- **Timer engines**: expose (with dry-down compensation), f-stop test strip, and light meter, all active simultaneously
- **Print log**: 500-entry FIFO of finished prints with exposure details, starred status, and notes
- **Storage**: print bodies persist on the `Storage` facade (`/prints/`); counters live in NVS
- **Connectivity**: web portal (darkroom landing page + print-log viewer), MQTT, BLE HID

## Supported boards

| Board | Panel | Sensor | Flag |
|---|---|---|---|
| `jc4880p433-darkroom` | GUITION JC4880P433 ESP32-P4 480×800 MIPI-DSI | TSL2591 (I2C) | `HAS_SENSOR_TSL2591` |

The board sets `IS_DARKROOM_TIMER true` in its `board_overrides.h`, which selects the Darkroom Timer device class (brand prefix `ESP32-MP Darkroom Timer`, SSID `ESP32-MP-DARKROOM-XXXXXX`). All darkroom code is gated by `IS_DARKROOM_TIMER`, so other boards in the same firmware build never link the timer engines, sensor, or print log. The board override also promotes the darkroom landing page to the portal's primary page via `PORTAL_PRIMARY_FRAGMENT "darkroom"`.

## Build

```bash
./build.sh jc4880p433-darkroom
./upload.sh jc4880p433-darkroom
```

## Hardware setup

### TSL2591 light sensor

The TSL2591 meters paper exposure for grade selection and enlargement-factor compensation. It is read on demand (no continuous polling) on a dedicated I2C bus:

| Setting | Value | Flag |
|---|---|---|
| I2C bus | 1 (`Wire1`) | `TSL2591_I2C_BUS` |
| SDA | GPIO 52 | `TSL2591_I2C_SDA` |
| SCL | GPIO 51 | `TSL2591_I2C_SCL` |
| Clock | 400 kHz | `TSL2591_I2C_FREQUENCY` |
| Address | `0x29` (fixed) | — |

Wire the TSL2591 breakout's SDA/SCL to GPIO 52/51 and power it from the board's 3V3 rail. Mount the sensor under the enlarger lens so it sees the projected image when metering.

### Shelly relays

The enlarger and safelight are switched by [Shelly](https://www.shelly.com/) Wi-Fi relays over HTTP, so no mains wiring touches the controller board. Put each relay on the same network as the macropad and configure its IP/slot from the darkroom portal page (or `PUT /api/relay`). The `expose` and `strip` engines drive the configured relay automatically while running; the `shelly` action lets buttons toggle relays directly.

## Timer engines

Three engines run independently and expose live state through binding schemes and the touch UI.

### Expose timer (`expose`)

A count-up/count-down enlarger exposure timer with **dry-down compensation** (a percentage added to compensate for prints drying down darker). Supports a focus mode that turns the enlarger lamp on continuously for composing, plus pause/resume. On completion it drives the configured relay and pends a print-log entry with the full exposure details.

### F-stop test strip (`strip`)

An f-stop test-strip sequencer: pick a base exposure and a per-segment stop increment, and the engine walks through timed segments (with an optional pause/tick between segments) so you can dial in the right exposure from a single sheet. On completion it pends a print-log strip entry.

### Light meter (`meter`)

Reads the TSL2591 to derive a paper **grade** suggestion from a highlight/shadow (bright/dark) measurement, and computes an **enlargement (magnification) factor** from two reference reads so exposure can be corrected when you resize a print.

## Action types

Five action types are registered via `REGISTER_ACTION_TYPE`, so buttons, swipe gestures, boot actions, and timer-expire actions can all drive the darkroom. Action **wire strings** are `expose`, `strip`, `meter`, `print`, and `shelly` (legacy field names preserved for field-deployed pad configs). Each takes a flat `{<type>_command, <type>_value}` JSON shape.

### `expose`

| Command | Effect |
|---|---|
| `start` / `stop` / `toggle` | Run / halt / toggle the exposure |
| `pause` / `resume` | Pause and resume a running exposure |
| `reset` | Reset to the configured time |
| `focus` / `focus_off` / `focus_toggle` | Enlarger lamp on/off for composing |
| `set_time <seconds>` | Set the exposure time |
| `adjust_seconds <delta>` | Nudge the time by seconds |
| `adjust_stops <delta>` | Nudge the time by stops |
| `set_dry_down <pct>` / `adjust_dry_down <delta>` | Set/adjust dry-down compensation |

### `strip`

| Command | Effect |
|---|---|
| `start` / `cancel` | Run / cancel the test-strip sequence |
| `set_base <s>` / `adjust_base <delta>` | Set/adjust the base exposure |
| `step_up` / `step_down` | Change the per-segment stop increment |
| `set_segments <n>` / `adjust_segments <delta>` | Set/adjust the number of segments |
| `set_countdown <0\|1>` / `adjust_countdown` | Toggle countdown display |
| `set_pause <s>` / `adjust_pause <delta>` | Inter-segment pause |
| `set_tick <0\|1>` | Audible tick during segments |

### `meter`

| Command | Effect |
|---|---|
| `read_lref` / `read_bright` / `read_dark` | Take a reference / highlight / shadow reading |
| `set_lref <v>` / `adjust_lref <delta>` | Set/adjust the reference level |
| `set_zone5 <v>` / `adjust_zone5 <delta>` | Set/adjust the Zone-V target |
| `mag_measure_a` / `mag_measure_b` / `mag_clear` | Two-point enlargement-factor measurement |

### `print`

| Command | Effect |
|---|---|
| `toggle_star` | Toggle the starred flag on the most recent print |
| `set_star <0\|1>` | Set the starred flag explicitly |

Prints themselves are logged automatically when an exposure or strip completes; the `print` action manages the starred state of the last entry.

### `shelly`

Drives a configured Shelly relay (on/off/toggle) directly, independent of the timer engines.

## Binding schemes

Four binding schemes expose live engine and print data to pad widgets. All support the standard `[scheme:key]` / `[scheme:key;format]` syntax with an optional `|fallback`.

### `[expose:*]`

`time`, `elapsed`, `remaining`, `effective_time`, `dry_down`, `state`, `running`, `paused`, `stopped`, `focus`, `relay`

### `[strip:*]`

`state`, `base_time`, `total_time`, `elapsed`, `remaining`, `segment`, `segments`, `seg_inc`, `step`, `range`, `progress`, `countdown`, `tick`, `relay`, `table`

### `[meter:*]`

`lref`, `l_bright`, `l_dark`, `sbr`, `grade`, `grade_label`, `time`, `mag_lux_a`, `mag_lux_b`, `mag_time`, `mag_factor`

### `[print:*]`

| Key | Value |
|---|---|
| `id` | Current print ID (`---` while printing, actual ID after save) |
| `last_id` | Most recently saved print ID, or `---` |
| `count` | Number of prints currently logged |

## Print logging

Finished prints are written to the `Storage` facade under `/prints/` (one JSON body per print, named `YYMMDD-NNN.json`). The log is a **500-entry FIFO** (`DARKROOM_PRINT_LOG_MAX`); the oldest print is evicted when a new one would exceed the cap. A delete removes the body file and updates the counters atomically so no orphan files are left behind.

Counters and the daily sequence live in NVS:

| Key | Purpose |
|---|---|
| `print_date` | Current day key (`YYMMDD`) for sequence numbering |
| `print_seq` | Per-day print sequence counter |
| `print_seq_fb` | Sequence fallback (monotonic) |
| `print_count` | Total prints currently stored |

Each print body captures the exposure or strip parameters, the metered grade, a starred flag, and a free-text notes field.

## Relay control

Relays are driven through a generic relay abstraction with a Shelly HTTP backend. Relay slots (IP/channel) are configured from the darkroom portal page and persisted; the expose and strip engines reference the configured slot when they fire.

## REST endpoints

| Method | Endpoint | Purpose |
|---|---|---|
| `GET` | `/api/relay` | Get relay slot configuration |
| `PUT` | `/api/relay` | Update relay slot configuration |
| `GET` | `/api/prints` | List all prints (fields + notes + starred, newest first) |
| `GET` | `/api/prints?id=ID` | Full print detail (segments, notes, star) |
| `PUT` | `/api/prints?id=ID` | Update mutable fields (notes, starred) |
| `DELETE` | `/api/prints?id=ID` | Delete a single print |
| `DELETE` | `/api/prints?confirm=true` | Delete all prints |
| `GET` | `/api/prints/export` | Export all prints as a JSON array (download) |

## Documentation

- [../README.md](../README.md) — All device classes
- [../../dev/web-portal.md](../../dev/web-portal.md) — Web portal architecture and REST conventions
- [../../pad-editor-guide.md](../../pad-editor-guide.md) — Pad editor, binding templates, and widgets
