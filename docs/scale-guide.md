# Scale Guide

This guide covers the HX711 load cell scale integration — bindings, actions, calibration workflow, guided brew mode, and REST API. It's aimed at users building rich touch-screen UIs (pad dashboards) for a scale application on an ESP32 Macropad device.

> **Prerequisite**: The scale requires an HX711 load cell amplifier connected to two GPIO pins and `HAS_SENSOR_HX711` enabled in the board overrides. See the hardware setup section below.

---

## Overview

The scale subsystem provides:

- **Real-time weight** — EMA-smoothed readings at ~80 Hz, displayed via bindings
- **Flow rate** — weight derivative computed over a 1-second window (grams per second)
- **On-device calibration** — tare and calibrate directly from pad buttons
- **Guided brew mode** — auto-tare and auto-start timer on first pour (brew manager)
- **Status feedback** — binding-driven status for taring/calibrating operations
- **REST API** — tare, calibrate, and status endpoints for external tooling

The device also includes **3 independent timers** — useful for tracking brew time, bloom phases, and pour intervals:

- **Count-up and countdown modes** — stopwatch or countdown with overtime display
- **Expire beep alerts** — configurable audio alert when a countdown reaches zero
- **Adjustable on-the-fly** — add or subtract seconds mid-brew without stopping
- **Auto-configured countdowns** — entering a pad automatically sets up timer presets from button configs

All scale data is exposed through the `[scale:]` binding scheme, and the guided brew workflow through `[brew:]`. Both work everywhere bindings are supported: labels, colors, widget data, conditional expressions, and more.

---

## Hardware Setup

### Wiring

Connect an HX711 breakout board to your ESP32:

| HX711 Pin | ESP32 GPIO | Description |
|-----------|------------|-------------|
| DOUT      | Board-specific | Data output (e.g. GPIO 52) |
| SCK       | Board-specific | Clock input (e.g. GPIO 51) |
| VCC       | 3.3V or 5V | Power |
| GND       | GND | Ground |

### Board Configuration

Enable the scale in your board's `src/boards/<board-name>/board_overrides.h`:

```cpp
#define HAS_SENSOR_HX711  true
#define HX711_DOUT_PIN     52
#define HX711_SCK_PIN      51
```

> **Tip**: Avoid GPIOs used by SDIO, SPI flash, or other peripherals. Check your board's datasheet.

### Calibration Data

Calibration factor and tare offset are automatically stored in NVS (non-volatile storage). They survive reboots — you only need to calibrate once after wiring changes or load cell replacement.

---

## Scale Bindings

The `[scale:]` binding scheme provides live scale data for labels, colors, widgets, and expressions. The general syntax is:

```
[scale:key]
[scale:key;format]
```

Where `key` selects what data to read and the optional `;format` applies a printf-style format override.

### Available Keys

| Key | Type | Default Format | Description |
|-----|------|---------------|-------------|
| `weight` | float | `%.1f` | Current EMA-smoothed weight in grams |
| `flow_rate` | float | `%.1f` | Weight change rate in g/s (1-second window) |
| `calibration_factor` | float | `%.4f` | Current calibration divisor |
| `offset` | long | `%ld` | Raw ADC tare offset |
| `available` | string | — | `ON` if HX711 detected, `OFF` otherwise |
| `cal_weight` | float | `%.1f` | Current calibration reference weight (grams) |
| `status` | string | — | `idle`, `taring`, or `calibrating` |

### Binding Examples

**Simple weight display:**
```
[scale:weight] g
```
→ `123.4 g`

**Weight with no decimal places:**
```
[scale:weight;%.0f] g
```
→ `123 g`

**Flow rate with one decimal:**
```
[scale:flow_rate] g/s
```
→ `2.3 g/s`

**Calibration reference weight:**
```
Cal: [scale:cal_weight] g
```
→ `Cal: 251.5 g`

**Status indicator:**
```
[scale:status]
```
→ `idle`, `taring`, or `calibrating`

**Availability check:**
```
Scale: [scale:available]
```
→ `Scale: ON` or `Scale: OFF`

### Using Scale Bindings in Expressions

Scale bindings work inside `[expr:]` for computed values and conditional formatting:

**Conditional color based on flow rate (V60 pour-over zones):**
```
[expr:threshold([scale:flow_rate],0,808080,1.5,00FF00,2.5,FFAA00,3.5,FF0000)]
```
- Gray when idle (< 1.5 g/s)
- Green in the sweet spot (1.5–2.5 g/s)
- Orange at the high end (2.5–3.5 g/s)
- Red when pouring too fast (> 3.5 g/s)

**Conditional status text:**
```
[expr:[scale:status]=="idle"?"Ready":"Working..."]
```

**Absolute value of flow rate:**
```
[expr:abs([scale:flow_rate]);%.1f] g/s
```

### Using Scale Bindings with Widgets

Scale data integrates naturally with all widget types.

**Sparkline — weight over time:**
```json
{
  "type": "sparkline",
  "data_binding": "[scale:weight]",
  "time_window": 120,
  "min_binding": "0",
  "max_binding": "500"
}
```

**Sparkline — flow rate trend:**
```json
{
  "type": "sparkline",
  "data_binding": "[scale:flow_rate]",
  "time_window": 60,
  "line_color_binding": "[expr:threshold([scale:flow_rate],0,808080,1.5,00FF00,2.5,FFAA00,3.5,FF0000)]"
}
```

**Gauge — weight as a dial:**
```json
{
  "type": "gauge",
  "data_binding": "[scale:weight]",
  "min_binding": "0",
  "max_binding": "500"
}
```

**Bar chart — flow rate bar:**
```json
{
  "type": "bar_chart",
  "data_binding": "[scale:flow_rate]",
  "min_binding": "0",
  "max_binding": "5",
  "bar_color_binding": "[expr:threshold([scale:flow_rate],0,808080,1.5,00FF00,2.5,FFAA00,3.5,FF0000)]"
}
```

### Pipe Fallback

Use the pipe fallback syntax to display a custom placeholder before the first reading is available:

```
[scale:weight|--.-] g
```
→ Shows `--.-- g` until the HX711 is ready, then switches to live weight.

---

## Scale Actions

Scale actions are button actions you assign in the pad editor. They enable on-device tare and calibration without needing a computer or the REST API.

### Action Types

| Action Type | Pad Editor Name | Payload | Description |
|-------------|----------------|---------|-------------|
| `scale` | Scale Tare | — | Zeros the scale (deferred, non-blocking) |
| `scale_cal` | Scale Calibrate | — | Calibrates using the current reference weight (deferred, non-blocking) |
| `scale_cal_weight` | Scale Cal Weight ± | delta (g) | Adjusts the reference weight by ± delta grams |
| `scale_cal_set` | Scale Cal Weight Set | value (g) | Sets the reference weight to an absolute value |

All tare and calibrate operations are **deferred** — the button tap returns immediately, and the actual operation runs on the next main loop cycle. This keeps the display responsive. Use `[scale:status]` to show progress.

### On-Device Calibration Workflow

A complete calibration flow using pad buttons:

1. **Set reference weight** — Use `scale_cal_set` to set the known weight of your calibration object (e.g. 251.5 g)
2. **Tare** — Tap a `scale` (tare) button with the scale empty
3. **Place object** — Put the known weight on the scale
4. **Calibrate** — Tap a `scale_cal` button to compute the calibration factor
5. **Verify** — The weight display should match the known weight

### Calibration Pad Example

Here's how to build a calibration pad with all the controls:

| Button | Action | Payload | Labels |
|--------|--------|---------|--------|
| Tare | `scale` | — | Center: `Tare` |
| Calibrate | `scale_cal` | — | Center: `Calibrate` |
| +10g | `scale_cal_weight` | `10` | Center: `+10` |
| -10g | `scale_cal_weight` | `-10` | Center: `-10` |
| +0.5g | `scale_cal_weight` | `0.5` | Center: `+0.5` |
| -0.5g | `scale_cal_weight` | `-0.5` | Center: `-0.5` |
| Set 250g | `scale_cal_set` | `250` | Center: `250g` |
| Weight | — | — | Top: `Weight`, Center: `[scale:weight;%.1f] g`, Bottom: `Cal: [scale:cal_weight] g` |
| Status | — | — | Center: `[scale:status]` |

> **Tip**: The `scale_cal_weight` delta supports decimals (e.g. `0.5`, `-0.1`), allowing 0.1 g precision for the reference weight.

### Status-Aware UI

Use `[scale:status]` with `[expr:]` for dynamic button feedback:

**Label that changes during operations:**
```
[expr:[scale:status]=="taring"?"Taring...":"Tare"]
```

**Color that highlights during activity:**
```
[expr:[scale:status]=="idle"?"333333":"FF8800"]
```

---

## REST API

The scale exposes three REST endpoints for external access and tooling.

### GET /api/scale

Returns the current scale state.

**Response:**
```json
{
  "available": true,
  "weight_g": 123.4,
  "flow_rate": 2.1,
  "calibration_factor": 1123.6415,
  "offset": 71839
}
```

### POST /api/scale/tare

Zeros the scale. No request body needed. Persists the new offset to NVS.

**Response:**
```json
{ "ok": true }
```

### POST /api/scale/calibrate

Calibrates with a known weight. The scale must be loaded with the specified weight before calling.

**Request body:**
```json
{ "known_weight_g": 251.5 }
```

**Response (success):**
```json
{ "ok": true, "calibration_factor": 1123.6415 }
```

**Response (error):**
```json
{ "ok": false, "error": "Raw delta is zero — no load cell signal. Check wiring." }
```

---

## Timers

The device includes 3 independent on-device timers — perfect for tracking brew time, bloom phase, and pour intervals. Timers support count-up (stopwatch) and countdown modes, with optional beep alerts when a countdown expires.

### Timer Bindings

The `[timer:]` binding scheme provides live timer values for labels, colors, and expressions.

| Binding | Returns | Example |
|---------|---------|---------|
| `[timer:1]` | Elapsed/remaining in mm:ss | `4:05` |
| `[timer:1;hh:mm:ss]` | Hours, minutes, seconds | `0:04:05` |
| `[timer:1;mm:ss.d]` | With decisecond precision | `4:05.3` |
| `[timer:1;ss]` | Total seconds | `245` |
| `[timer:1_state]` | Timer state | `running`, `paused`, `stopped` |
| `[timer:1_expired]` | Countdown crossed zero? | `ON` or `OFF` |
| `[timer:1_mode]` | Timer direction | `up` or `down` |

Countdown timers that run past zero show negative values (e.g., `-0:05`, `-1:23`) so you can see overtime at a glance.

### Timer Actions

Assign timer actions to pad buttons. The payload format is `N:command` or `N:command:arg`, where N is the timer number (1–3).

| Command | Payload Example | Description |
|---------|----------------|-------------|
| **toggle** | `1:toggle` | Stopped → start, running → pause, paused → resume |
| **start** | `1:start` | Start the timer |
| **stop** | `1:stop` | Stop and reset to 0 (count-up) or the countdown preset |
| **pause** | `1:pause` | Freeze at current value |
| **resume** | `1:resume` | Continue from paused value |
| **reset** | `1:reset` | Reset without changing running state |
| **lap** | `1:lap` | Reset and start fresh (step timing) |
| **countdown** | `1:countdown:240` | Set countdown duration (seconds) and switch to countdown mode |
| **adjust** | `1:adjust:15` | Add/subtract seconds from countdown preset (e.g., +15, -10) |
| **mode** | `1:mode:down` | Switch between `up` (stopwatch) and `down` (countdown) |

### Timer Button Configuration

In the pad editor, the timer action type provides additional settings:

- **Default Countdown (seconds)** — When navigating to a pad, the first button referencing each timer automatically sets its countdown preset. Only applied when the timer is stopped at zero (fresh). This means entering a brewing pad auto-configures your timers.
- **Expire Beep Pattern** — A beep DSL string (e.g., `1000:300 200 1000:300`) that plays when a countdown reaches zero. Fires exactly once per countdown cycle.
- **Expire Beep Volume** — Optional volume override (0 = device volume, 1–100).

### Using Timers with Scale Bindings

Timers and scale bindings combine naturally in expressions:

**Conditional color when bloom timer expires:**
```
[expr:[timer:1_expired]=="ON" ? "FF4444" : "333333"]
```

**Weight-per-second calculated from timer:**
```
[expr:[scale:weight] / max([timer:1;ss], 1);%.1f] g/s avg
```

**Dynamic label showing timer state:**
```
[expr:[timer:1_state]=="running" ? [timer:1;mm:ss] : "Tap to start"]
```

---

## Brew Manager (Guided Brew Mode)

The brew manager adds a simple guided brew workflow on top of the raw scale and timer features. It provides a 4-state state machine with **auto-start** — tap a button to arm the brew, and the timer starts automatically when you begin pouring.

### How It Works

1. **Start** — Tares the scale and enters the **Ready** state (armed, waiting for pour)
2. **Auto-start** — When weight exceeds 2 g above tare, the timer starts automatically and the state moves to **Brewing**
3. **Stop** — Freezes the timer and enters **Done** (manual tap)
4. **Reset** — Clears everything back to **Idle**

The brew manager owns its own internal timer (independent of the 3 general-purpose timers), so you can still use timer 1–3 for bloom phases or other purposes alongside a guided brew.

### Brew Bindings

The `[brew:]` binding scheme provides live brew workflow data. Syntax:

```
[brew:key]
[brew:key;format]
```

| Key | Type | Default Format | Description |
|-----|------|---------------|-------------|
| `weight` | float | `%.1f` | Current weight (same as `[scale:weight]`, included for convenience) |
| `flow_rate` | float | `%.1f` | Current flow rate (same as `[scale:flow_rate]`) |
| `timer` | time | `mm:ss` | Brew elapsed time (0 when idle/ready, frozen when done) |
| `phase` | string | — | Current phase: `Idle`, `Ready`, `Brewing`, or `Done` |
| `active` | string | — | `1` if Ready or Brewing, `0` otherwise |

#### Timer Format Options

The `[brew:timer]` key supports the same format options as regular timers:

| Format | Example | Description |
|--------|---------|-------------|
| `mm:ss` | `4:05` | Minutes and seconds (default) |
| `hh:mm:ss` | `0:04:05` | Hours, minutes, seconds |
| `ss` | `245` | Total seconds |
| `mm:ss.d` | `4:05.3` | With decisecond precision |

#### Binding Examples

**Brew timer with decisecond precision:**
```
[brew:timer;mm:ss.d]
```
→ `4:05.3`

**Phase-aware button label:**
```
[expr:[brew:phase]=="Idle"?"Start Brew":[brew:phase]=="Ready"?"Waiting...":[brew:phase]=="Brewing"?"Stop":"Reset"]
```

**Background color that tracks brew state:**
```
[expr:[brew:active]=="1"?"1a3a1a":"1a1a2e"]
```

**Conditional flow rate — only show during active brew:**
```
[expr:[brew:active]=="1"?[brew:flow_rate;%.1f]:"--.-"] g/s
```

### Brew Actions

Assign brew actions to pad buttons in the pad editor. The action type is `brew` and the payload selects the command.

| Payload | Description |
|---------|-------------|
| `start` | Tare scale, arm auto-start (default if no payload) |
| `stop` | Freeze timer, enter Done |
| `reset` | Clear all state, return to Idle |

### Brew Pad Example

A simple brew control layout — three dedicated buttons plus display labels:

| Button | Action | Payload | Labels |
|--------|--------|---------|--------|
| Start | `brew` | `start` | Center: `[expr:[brew:phase]=="Idle"?"Start Brew":"Waiting..."]` |
| Stop | `brew` | `stop` | Center: `Stop` |
| Reset | `brew` | `reset` | Center: `Reset` |
| Timer | — | — | Top: `Brew Time`, Center: `[brew:timer;mm:ss.d]` |
| Weight | — | — | Top: `Weight`, Center: `[brew:weight;%.1f] g` |
| Flow | — | — | Top: `Flow`, Center: `[brew:flow_rate;%.1f] g/s` |
| Phase | — | — | Center: `[brew:phase]` |

### Brew vs. Manual Timer

| Feature | Brew Manager | Manual Timer |
|---------|-------------|--------------|
| Auto-start on pour | Yes (2 g threshold) | No |
| Auto-tare on start | Yes | No |
| Independent slots | 1 brew session | 3 timers |
| Countdown mode | No (count-up only) | Yes |
| Expire beep | No | Yes |
| Phase awareness | Yes (Idle/Ready/Brewing/Done) | No |

Use the brew manager for the overall brew workflow (start → pour → done), and use manual timers alongside it for things like bloom countdowns.

---

## Example: V60 Pour-Over Dashboard

A practical 4×2 pad layout for V60 coffee brewing with the brew manager and a bloom timer:

| Col 1 | Col 2 | Col 3 | Col 4 |
|-------|-------|-------|-------|
| **Weight** | **Flow Rate** | **Brew Timer** | **Tare / Start** |
| Weight sparkline | Flow sparkline | **Bloom Timer** | **Stop / Reset** |

### Button Configurations

**Button (0,0) — Weight display:**
- Top label: `Weight`
- Center label: `[brew:weight;%.1f] g`
- Background color: `#1a1a2e`

**Button (1,0) — Flow rate with color:**
- Top label: `Flow`
- Center label: `[brew:flow_rate;%.1f] g/s`
- Background color: `[expr:threshold([scale:flow_rate],0,1a1a2e,1.5,1a3a1a,2.5,3a3a1a,3.5,3a1a1a)]`

**Button (2,0) — Brew timer display:**
- Top label: `Brew`
- Center label: `[brew:timer;mm:ss.d]`
- Bottom label: `[brew:phase]`
- Background color: `[expr:[brew:active]=="1"?"1a3a1a":"1a1a2e"]`

**Button (3,0) — Start brew (tare + arm):**
- Center label: `[expr:[brew:phase]=="Idle"?"Start Brew":[brew:phase]=="Ready"?"Waiting...":[brew:phase]=="Brewing"?"Brewing...":"Done"]`
- Action: `brew`, payload: `start`
- Background color: `[expr:[brew:phase]=="Ready"?"2a2a1a":"16213e"]`

> The brew manager auto-tares and auto-starts the timer when weight exceeds 2 g. No need for separate tare and start buttons.

**Button (0,1) — Weight sparkline:**
- Widget type: `sparkline`
- Data binding: `[scale:weight]`
- Time window: 120 seconds
- Background color: `#0f0f23`

**Button (1,1) — Flow rate sparkline:**
- Widget type: `sparkline`
- Data binding: `[scale:flow_rate]`
- Time window: 60 seconds
- Line color binding: `[expr:threshold([scale:flow_rate],0,808080,1.5,00FF00,2.5,FFAA00,3.5,FF0000)]`
- Background color: `#0f0f23`

**Button (2,1) — Bloom timer (manual countdown):**
- Center label: `Bloom [timer:1;mm:ss]`
- Action: `timer`, payload: `1:toggle`
- Default countdown: `45` (45-second bloom)
- Expire beep: `800:200 100 800:200`
- Background color: `[expr:[timer:1_expired]=="ON"?"3a1a1a":"16213e"]`

> Use timer 1 as a standalone bloom countdown alongside the brew manager's auto-timed overall brew.

**Button (3,1) — Stop / Reset:**
- Top label: `Stop`
- Action (tap): `brew`, payload: `stop`
- Bottom label: `Reset`
- Action (long-press): `brew`, payload: `reset`
- Background color: `#16213e`

### Brewing Workflow

1. Tap **Start Brew** — the scale tares and the button shows "Waiting..."
2. Start pouring — when weight exceeds 2 g, the brew timer starts automatically
3. Pour water — watch the weight, flow rate sparkline, and brew timer simultaneously
4. Tap **Bloom Timer** to track your 45-second bloom phase separately
5. When done, tap **Stop** to freeze the brew timer
6. Long-press **Reset** to clear everything for the next brew

> **Tip**: The brew manager handles tare + timer start in one tap. You still have all 3 manual timers available — use one for bloom countdowns, another for pour phases, etc.

---

## Signal Processing Details

Understanding the signal processing helps when tuning your UI:

- **Sample rate**: ~80 Hz (HX711 at 80 SPS, single-sample reads)
- **EMA filter**: Alpha = 0.3 (30% new sample, 70% history). Smooths noise while remaining responsive to real weight changes.
- **Jump threshold**: 5g. If a single reading differs from the EMA by more than 5g, the EMA resets instantly — this makes placing/removing objects feel immediate.
- **Flow rate window**: 1 second. The derivative is computed as `(current_weight - weight_1s_ago) / elapsed_seconds`.
- **Tare**: Averages 20 raw samples (~250ms) for a stable zero point.
- **Calibration**: Averages 20 raw samples of the loaded scale, then divides by the known calibration weight to compute the factor.

### What This Means for Your UI

- Weight labels update smoothly without jitter
- Flow rate responds within ~1 second of pour changes
- Tare and calibrate take ~250ms each (handled in the background — UI stays responsive)
- The `[scale:status]` binding transitions: `idle` → `taring` → `idle` (or `idle` → `calibrating` → `idle`)

---

## Tips & Best Practices

### Scale
- **Always tare before calibrating** — calibration measures the raw ADC difference between zero and the known weight
- **Use a known, accurate weight** for calibration — kitchen scales or certified weights work well
- **Flow rate is most useful during pours** — at rest it hovers near 0.0 g/s with minor noise
- **Negative flow rate** means weight is decreasing (evaporation, dripping, object removal)
- **Calibration persists across reboots** — you don't need to recalibrate after power cycles
- **Use `[scale:available]` for conditional UI** — hide or gray out scale buttons when the sensor isn't connected
- **The cal_weight defaults to 500g** at boot and is not persisted — set it from a pad button before calibrating

### Timers
- **Default countdown auto-configures** — entering a pad sets up timer countdowns from button configs, so you don't need a separate "set countdown" button
- **Overtime is visible** — countdown timers keep running past zero and show negative values, so you always know how far over time you are
- **Expire beep fires once** — the beep triggers on the zero-crossing edge, not repeatedly. Reset or adjust the timer to re-arm it
- **Use `adjust` for on-the-fly changes** — add or subtract seconds mid-brew without stopping the timer
- **Lap for pour phases** — the `lap` command resets and immediately restarts a timer, perfect for timing individual pour phases (bloom, first pour, second pour)
- **3 timers = 3 purposes** — e.g., timer 1 for total brew time, timer 2 for bloom/phase timing, timer 3 for anything else
- **Timers are runtime-only** — they don't persist across reboots; countdown presets are reapplied when you navigate to the pad
