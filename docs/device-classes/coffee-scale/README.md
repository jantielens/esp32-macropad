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

- **Idle** — no brew running. The next-brew label (`[brew:next_label]`) shows the start prompt of the last-used template, and `[brew:instruction]` shows the template's idle guidance.
- **Active** — a brew is in progress. Weight and flow update live. `auto_weight` and `auto_time` stages advance automatically; manual stages advance through the `brew:next` / `brew:advance` actions. Every sample is appended to an in-PSRAM series for later export.
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
| `weight` | Current scale weight in g |
| `flow_rate` | Current flow rate in g/s |
| `timer` | Elapsed brew time; accepts timer format such as `mm:ss` |
| `stage` | Current stage name, or `Idle` / `Done` |
| `active` | `1` while a brew is active, otherwise `0` |
| `template` | Active or last-selected template machine name, or `Idle` |
| `display_name` | Active or last-selected template display name |
| `dose` | Captured coffee dose in g |
| `water` | Water weight in g while brewing or after completion |
| `ratio` | Water-to-dose ratio, or `---` when no dose is captured |
| `instruction` | Current phase or stage instruction text; resolves inner bindings |
| `next_label` | Label for the single advance button |
| `advance_state` | `action` for Idle, Done, and manual stages; `automatic` for auto-advancing stages. Use it to style the advance button. |
| `stage_status` | Compact live summary such as `28 g to pour - 31 s` or `Waiting - 12 s` |
| `stage_weight_target` | Current stage's cumulative water target in g, or `0` |
| `stage_weight_current` | Current scale weight in g for the active stage |
| `stage_weight_remaining` | Remaining grams to the current stage target, never below `0` |
| `stage_weight_pct` | Current weight as a percentage of the current stage target |
| `stage_time_target` | Current stage's intended duration in seconds, or `0` |
| `stage_time_current` | Elapsed seconds toward the current stage time target, or `0` when no time target is configured |
| `stage_time_remaining` | Remaining seconds to the current stage time target, clamped at `0` |
| `stage_time_pct` | Elapsed time as a percentage of the current stage time target; may exceed `100` for a manual stage |
| `stage_flow_target` | Current stage's target flow rate in g/s, or `0` |
| `stage_flow_current` | Current flow rate in g/s for the active stage |
| `stage_flow_pct` | Current flow rate as a percentage of the current stage target |
| `stages_json` | Table-widget JSON for the template stage list and current progress coloring |
| `summary_json` | Table-widget JSON for dose, water, ratio, time, and named captures |
| `template_count` | Number of registered built-in and custom templates |
| `tpl_N_name` | Machine name of registered template `N`, starting at `0` |
| `tpl_N_display_name` | Display name of registered template `N` |
| `tpl_N_description` | Description of registered template `N` |
| `tpl_N_stages` | Stage count of registered template `N` |

## Built-in templates

The built-in templates share concise `Start` labels in Idle and Done. The advance
button's color can be styled from `[brew:advance_state]`: normal for `action` and muted
for `automatic`.

* **Free Pour**: tare the assembled brewer, auto-start on the first pour, then finish
	the brew manually.
* **V60 Pour-Over**: tare an empty dosing cup, capture the coffee dose, prepare the
	V60 and cup, then auto-start on the first pour.
* **Advanced V60**: a 16 g / 250 g guided recipe with a 50 g bloom, a 150 g timed
	second pour, and a 250 g final pour. Its stage targets are 45, 60, and 75 seconds,
	giving a three-minute guide. The final pour remains manual until **Finish** is tapped.
	Timed stages advance on time; their cumulative water and flow targets guide the pour
	but do not block the next stage.

## Template time targets

Each stage may define `target_time_s`. It is an intended duration used by
`stage_time_*` bindings for progress displays. It does not itself decide how a stage
advances:

| Stage type | Behavior with `target_time_s` |
|------------|-------------------------------|
| `manual` | Advisory only. The user advances the stage, even after the target is exceeded. |
| `auto_weight` | Advisory only. The stage advances when its weight threshold is reached. |
| `auto_time` | Required. The stage advances automatically when its time target is reached. |

## Template authoring guidance

Built-in and custom templates should use short, consistent labels. Keep the selected
recipe name visible elsewhere on the pad and use `Start` in both Idle and Done. Avoid
"again" labels.

| Purpose | Recommended vocabulary |
|---------|------------------------|
| Start or restart | `Start` |
| Zero the scale | `Tare` |
| Measure coffee | `Weigh coffee` |
| Store the dose | `Save dose` |
| Set up the brewer | `Prepare brewer` |
| First-pour stage | `Ready to pour` with `Waiting` as its label |
| Initial saturation stage | `Bloom` with `Blooming` as its label |
| End a brew | `Finish` |

Use the instruction to state the physical action, then the button action when one is
required. Automatic-stage labels are status text, not invitations to tap. For example,
use `Put the prepared V60 and cup on the scale, then tap Ready.` before a first-pour
stage, and `Start pouring to begin.` while waiting for automatic detection.

Timed pour stages are time-led. Their water and flow targets guide the barista, but do
not delay an `auto_time` transition. Reach the target at the recommended flow, then let
the stage timer complete; correct an under- or over-pour in the following stage.

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
