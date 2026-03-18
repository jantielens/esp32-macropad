# Scale Guide

This guide covers the HX711 load cell scale integration — bindings, actions, calibration workflow, and REST API. It's aimed at users building rich touch-screen UIs (pad dashboards) for a scale application on an ESP32 Macropad device.

> **Prerequisite**: The scale requires an HX711 load cell amplifier connected to two GPIO pins and `HAS_SENSOR_HX711` enabled in the board overrides. See the hardware setup section below.

---

## Overview

The scale subsystem provides:

- **Real-time weight** — EMA-smoothed readings at ~80 Hz, displayed via bindings
- **Flow rate** — weight derivative computed over a 1-second window (grams per second)
- **On-device calibration** — tare and calibrate directly from pad buttons
- **Status feedback** — binding-driven status for taring/calibrating operations
- **REST API** — tare, calibrate, and status endpoints for external tooling

All scale data is exposed through the `[scale:]` binding scheme, which means it works everywhere bindings are supported: labels, colors, widget data, conditional expressions, and more.

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

## Example: V60 Pour-Over Dashboard

A practical 4×2 pad layout for V60 coffee brewing:

| Col 1 | Col 2 | Col 3 | Col 4 |
|-------|-------|-------|-------|
| **Weight** | **Flow Rate** | **Tare** | **Calibrate** |
| Weight sparkline | Flow sparkline | +10g | -10g |

### Button Configurations

**Button (0,0) — Weight display:**
- Top label: `Weight`
- Center label: `[scale:weight;%.1f] g`
- Background color: `#1a1a2e`

**Button (1,0) — Flow rate with color:**
- Top label: `Flow`
- Center label: `[scale:flow_rate;%.1f] g/s`
- Background color: `[expr:threshold([scale:flow_rate],0,1a1a2e,1.5,1a3a1a,2.5,3a3a1a,3.5,3a1a1a)]`

**Button (2,0) — Tare:**
- Center label: `[expr:[scale:status]=="taring"?"Taring...":"Tare"]`
- Action: `scale` (Scale Tare)
- Background color: `#16213e`

**Button (3,0) — Calibrate:**
- Center label: `[expr:[scale:status]=="calibrating"?"Cal...":"Calibrate"]`
- Bottom label: `[scale:cal_weight] g`
- Action: `scale_cal` (Scale Calibrate)
- Background color: `#16213e`

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

**Button (2,1) — Cal weight +10:**
- Center label: `+10`
- Action: `scale_cal_weight`, payload: `10`

**Button (3,1) — Cal weight -10:**
- Center label: `-10`
- Action: `scale_cal_weight`, payload: `-10`

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

- **Always tare before calibrating** — calibration measures the raw ADC difference between zero and the known weight
- **Use a known, accurate weight** for calibration — kitchen scales or certified weights work well
- **Flow rate is most useful during pours** — at rest it hovers near 0.0 g/s with minor noise
- **Negative flow rate** means weight is decreasing (evaporation, dripping, object removal)
- **Calibration persists across reboots** — you don't need to recalibrate after power cycles
- **Use `[scale:available]` for conditional UI** — hide or gray out scale buttons when the sensor isn't connected
- **The cal_weight defaults to 500g** at boot and is not persisted — set it from a pad button before calibrating
