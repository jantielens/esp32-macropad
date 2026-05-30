<!-- markdownlint-disable-file MD041 -->
# Coffee Scale

The Coffee Scale device class turns the ESP32 Macropad firmware into a connected espresso and pour-over scale. A load-cell ADC streams weight at high rate; the firmware smooths the signal, derives flow rate, drives a stage-based brew engine with per-template targets, records the full weight series of each brew, and presents everything in a touch UI, the web portal, and Home Assistant.

## At a glance

- **Reference boards**: `jc4880p433-nau7802` and `jc4880p433-hx711` (4.8" 480×800 MIPI-DSI, GT911 touch, ESP32-P4, 16 MB flash, 32 MB PSRAM)
- **Sensors**: a single load cell read through either a NAU7802 (I2C 24-bit ADC) or an HX711 (SPI-style amplifier)
- **Smoothing**: EMA + dead-band with three presets (Stable / Balanced / Responsive) and a windowed flow-rate estimate
- **Brew engine**: count-up state machine (idle / active / done) with per-stage templates and a brew-template DSL
- **Storage**: brew logs and brew templates persist on the `Storage` facade; live series buffers live in PSRAM
- **Connectivity**: web portal (scale calibration, brew browser, template editor), MQTT, BLE HID

## Supported boards

| Board | ADC backend | Interface | Flag |
|---|---|---|---|
| `jc4880p433-nau7802` | NAU7802 | I2C (24-bit) | `HAS_SENSOR_NAU7802` |
| `jc4880p433-hx711` | HX711 | bit-banged data/clock | `HAS_SENSOR_HX711` |

Both boards set `IS_COFFEE_SCALE true` in their `board_overrides.h`, which selects the Coffee Scale device class (brand prefix `ESP32-MP Coffee Scale`, SSID `ESP32-MP-SCALE-XXXXXX`). All coffee-scale code is gated by `IS_COFFEE_SCALE` / `HAS_SCALE`, so other boards in the same firmware build never link the scale or brew modules.

## Build

```bash
./build.sh jc4880p433-nau7802     # NAU7802 variant
./build.sh jc4880p433-hx711       # HX711 variant
./upload.sh jc4880p433-nau7802
```

## Hardware setup

Both variants reuse the same physical 2-pin header on the JC4880P433, so the same load cell wiring works for either ADC:

| Signal | GPIO | NAU7802 | HX711 |
|---|---|---|---|
| Data | 52 | `SENSOR_I2C_SDA` | `HX711_DOUT_PIN` |
| Clock | 51 | `SENSOR_I2C_SCL` | `HX711_SCK_PIN` |

Wire the load cell to the chosen ADC breakout, then connect the ADC's data and clock lines to GPIO 52 and GPIO 51. Power the ADC from the board's 3V3 rail. The NAU7802 is the recommended backend (true 24-bit I2C ADC with better noise performance); the HX711 variant exists for builds that already have an HX711 amplifier on hand.

## Calibration flow

The scale stores a calibration factor and a tare offset in NVS (keys `scale_cal`, `scale_ofs`) plus a smoothing preset (`scale_smooth`). Calibration is sensor-agnostic — the same flow applies to both backends.

1. **Tare** — with the platform empty, zero the reading. Triggered by the `scale:tare` action, the **Tare** button in the portal, or `POST /api/scale/tare`. On first boot, if no calibration data has ever been persisted, the firmware auto-requests a tare so the first reading is not garbage.
2. **Set the reference weight** — tell the firmware the mass of your calibration weight (default 500 g). Adjust it with `scale:cal_weight <delta>` or set it absolutely with `scale:cal_weight_set <grams>`.
3. **Calibrate** — place the reference weight on the platform and run `scale:calibrate` (or `POST /api/scale/calibrate`). The firmware derives a new calibration factor from the known weight and persists it.
4. **Smoothing preset** — choose Stable, Balanced (default), or Responsive to trade settling time against latency.

Calibration changes are applied live so subsequent readings use the new factor immediately, then persisted to NVS on the main task.

## Brew engine

A brew is a count-up timer driven by a **brew template**. A template defines an ordered list of **stages**, each with a weight target, a time target, an instruction line, and optional next-stage label. The engine has three phases:

- **Idle** — no brew running. The next-brew label (`[brew:next_label]`) shows the start prompt of the last-used template.
- **Active** — a brew is in progress. Weight and flow update live, the current stage advances automatically when its target is met or manually via the `brew:next` / `brew:advance` actions, and every sample is appended to an in-PSRAM series for later export.
- **Done** — the brew finished or was stopped. A summary is available until the next `brew:start` or `brew:reset`.

When a brew completes it is written to the brew log on the `Storage` facade (capped at a fixed number of brews, oldest evicted first). The full weight series, stage markers, and captured field values are embedded in each report so it stays self-contained for export.

Brew templates are loaded from storage at boot and can be reloaded at runtime. Reloading first stops any active brew and drops the engine's cached template pointers before the dynamic templates are freed, so cached pointers can never dangle.

## Action types

Two action types are registered via `REGISTER_ACTION_TYPE`, so buttons, swipe gestures, boot actions, and timer-expire actions can all drive the scale and brew engine.

### `scale`

| Command | Effect |
|---|---|
| `tare` (or empty) | Zero the reading against the current platform load |
| `calibrate` | Derive and persist a new calibration factor from the reference weight |
| `cal_weight <delta>` | Adjust the calibration reference weight by `delta` grams |
| `cal_weight_set <grams>` | Set the calibration reference weight to an absolute value |

### `brew`

| Command | Effect |
|---|---|
| `set_template <name>` | Select the active brew template |
| `start` | Start a brew with the active template |
| `next` | Advance to the next stage (manual) |
| `advance` | Advance the brew (alias used by auto/skip flows) |
| `stop` | Stop the current brew (moves to done, or cancels before first pour) |
| `reset` | Clear the current/done brew back to idle |
| `tare` | Tare the scale without leaving the brew flow |

## Binding schemes

Two binding schemes expose live scale and brew data to pad widgets. Both support the standard `[scheme:key]` / `[scheme:key;format]` syntax with an optional `|fallback`.

### `[scale:*]`

| Key | Value |
|---|---|
| `weight` | Current smoothed weight (g) |
| `flow_rate` | Windowed flow rate (g/s) |
| `calibration_factor` | Active calibration factor |
| `offset` | Active tare offset |
| `available` | `1` if the sensor is present, else `0` |
| `cal_weight` | Calibration reference weight (g) |
| `status` | `idle` / `taring` / `calibrating` |

### `[brew:*]`

| Key | Value |
|---|---|
| `weight`, `flow_rate` | Live weight / flow |
| `timer` | Elapsed brew time |
| `stage`, `active`, `template` | Current stage name, active flag, template name |
| `dose`, `water`, `ratio` | Dose weight, water weight, brew ratio |
| `instruction`, `next_label` | Current instruction text and next-action label |
| `stage_weight_target` / `_current` / `_remaining` / `_pct` | Per-stage weight progress |
| `stage_time_target` / `_current` / `_remaining` / `_pct` | Per-stage time progress |
| `stages_json` | JSON array of stages for a list/table widget |
| `summary_json` | JSON summary (dose / water / ratio / time + captures) for the done screen |

## REST endpoints

| Method | Endpoint | Purpose |
|---|---|---|
| `GET` | `/api/scale` | Current scale status (weight, flow, calibration, smoothing) |
| `POST` | `/api/scale/tare` | Tare the scale |
| `POST` | `/api/scale/calibrate` | Run calibration against the reference weight |
| `GET` | `/api/brews` | List saved brews |
| `POST` | `/api/brews/import` | Import one or more brews (JSON object or array) |
| `DELETE` | `/api/brews` | Delete all saved brews |
| `GET` | `/api/brew-templates` | List brew templates |
| `GET` | `/api/brew-templates/get` | Fetch a single template |
| `POST` | `/api/brew-templates` | Create or update a template |
| `DELETE` | `/api/brew-templates` | Delete a template |

## Documentation

- [../README.md](../README.md) — All device classes
- [../../dev/web-portal.md](../../dev/web-portal.md) — Web portal architecture and REST conventions
- [../../pad-editor-guide.md](../../pad-editor-guide.md) — Pad editor, binding templates, and widgets
