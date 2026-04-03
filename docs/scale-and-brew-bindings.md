# Scale & Brew Bindings Reference

A complete reference for all `[scale:]` and `[brew:]` bindings available when designing pad screens for the scale features. All bindings work everywhere bindings are supported: labels, colors, widget data, and `[expr:]` expressions.

> **Prerequisite**: Scale bindings require a scale sensor enabled (`HAS_SENSOR_HX711` or `HAS_SENSOR_NAU7802`) in your board overrides. Brew bindings additionally require `HAS_DISPLAY`.

---

## Binding Syntax

Both schemes use the standard binding format:

```
[scheme:key]
[scheme:key;format]
```

- **key** — selects what data to read
- **format** — optional printf-style format override (e.g. `%.2f`, `%u`). When omitted, the binding uses its default format.
- **pipe fallback** — `[scheme:key|fallback]` shows `fallback` text when the binding can't resolve (e.g. `[brew:instruction|Tap Start]`)

---

## Scale Bindings — `[scale:]`

Live data from the load cell (HX711 or NAU7802).

| Key | Type | Default Format | Description |
|-----|------|---------------|-------------|
| `weight` | float | `%.1f` | Current EMA-smoothed weight in grams |
| `flow_rate` | float | `%.1f` | Weight change rate in g/s (1-second window) |
| `calibration_factor` | float | `%.4f` | Current calibration divisor |
| `offset` | long | `%ld` | Raw ADC tare offset |
| `available` | string | — | `ON` if scale sensor detected, `OFF` otherwise |
| `cal_weight` | float | `%.1f` | Current calibration reference weight (grams) |
| `status` | string | — | `idle`, `taring`, or `calibrating` |

### Examples

```
[scale:weight] g                       → 123.4 g
[scale:weight;%.0f] g                  → 123 g
[scale:flow_rate] g/s                  → 2.3 g/s
[scale:status]                         → idle
Scale: [scale:available]               → Scale: ON
```

---

## Brew Bindings — `[brew:]`

Live data from the brew manager's guided brew workflow. Organized into sections below.

### Core Measurements

| Key | Type | Default Format | Description |
|-----|------|---------------|-------------|
| `weight` | float | `%.1f` | Current weight in grams (same as `[scale:weight]`, included for convenience) |
| `flow_rate` | float | `%.1f` | Current flow rate in g/s (same as `[scale:flow_rate]`) |
| `timer` | time | `mm:ss` | Brew elapsed time — 0 before first pour, frozen when done |

#### Timer Format Options

| Format | Output | Description |
|--------|--------|-------------|
| `mm:ss` | `4:05` | Minutes and seconds (default) |
| `hh:mm:ss` | `0:04:05` | Hours, minutes, seconds |
| `ss` | `245` | Total seconds only |
| `mm:ss.d` | `4:05.3` | With decisecond precision |

### Brew State

| Key | Type | Default Format | Description |
|-----|------|---------------|-------------|
| `stage` | string | — | Current stage name: `Idle`, `Done`, or the template stage name (e.g. `Dose beans`, `Bloom`, `Main pour`) |
| `active` | string | — | `1` if a brew is running (Active meta-phase), `0` otherwise |
| `template` | string | — | Machine name of active or hinted template (`free_pour`, `v60`, `rao_v60`); reflects the last set template even while idle |
| `display_name` | string | — | Human-friendly template name (e.g. `James Rao V60`); reflects the last set template even while idle |

### Captured Data

| Key | Type | Default Format | Description |
|-----|------|---------------|-------------|
| `dose` | float | `%.1f` | Captured dose weight in grams — 0 until captured via a dose stage |
| `water` | float | `%.1f` | Water poured in grams — live during brew, frozen after Done, 0 when idle |
| `ratio` | float | `%.1f` | Brew ratio (water ÷ dose) — shows `---` when no dose captured |

### UI Guidance

| Key | Type | Default Format | Description |
|-----|------|---------------|-------------|
| `instruction` | string | — | Current stage instruction text. Empty when Idle/Done — use with pipe fallback: `[brew:instruction\|Tap Start to begin]`. May contain inner bindings that are auto-resolved. |
| `next_label` | string | — | Label for the advance button — changes with each stage (e.g. `Start Rao V60` → `Log dose` → `Ready` → `Armed` → `Blooming...` → `Done` → `Brew again`) |

### Stage Weight Bindings

Per-stage weight progress. All return numeric values (0 when no target is set).

| Key | Type | Default Format | Description |
|-----|------|---------------|-------------|
| `stage_weight_target` | float | `%.0f` | Target weight for the current stage in grams (0 if none) |
| `stage_weight_current` | float | `%.1f` | Current weight in grams (same as `weight`, included for naming consistency) |
| `stage_weight_remaining` | float | `%.0f` | Grams remaining to reach target: max(0, target − weight) |
| `stage_weight_pct` | float | `%.0f` | Weight progress as percentage: weight ÷ target × 100. Not clamped — can exceed 100%. Returns 0 when no target. |

### Stage Time Bindings

Per-stage time progress. All values are in **whole seconds**. All return numeric values (0 when not applicable).

| Key | Type | Default Format | Description |
|-----|------|---------------|-------------|
| `stage_time_target` | uint | `%u` | Target duration for the current stage in seconds (only meaningful for auto-time stages, 0 otherwise) |
| `stage_time_current` | uint | `%u` | Seconds elapsed since entering the current stage |
| `stage_time_remaining` | uint | `%u` | Seconds remaining: max(0, target − elapsed). 0 when no time target. |
| `stage_time_pct` | float | `%.0f` | Time progress as percentage: elapsed ÷ target × 100. Not clamped — can exceed 100%. Returns 0 when no target. |

### Stage Flow Bindings

Per-stage flow rate guidance. All return numeric values (0 when no target is set).

| Key | Type | Default Format | Description |
|-----|------|---------------|-------------|
| `stage_flow_target` | float | `%.1f` | Target flow rate for the current stage in g/s (0 if none) |
| `stage_flow_current` | float | `%.1f` | Current flow rate in g/s (same as `flow_rate`, included for naming consistency) |
| `stage_flow_pct` | float | `%.0f` | Flow accuracy as percentage: current ÷ target × 100. Under 100% = too slow, over 100% = too fast. Returns 0 when no target. |

### Template Registry Bindings

Indexed access to the template registry. Use these to build dynamic template picker pads that auto-populate from available templates.

| Key | Type | Default Format | Description |
|-----|------|---------------|-------------|
| `template_count` | uint | `%u` | Number of registered templates (built-in + custom) |
| `tpl_N_name` | string | — | Machine name of template at index N (e.g. `free_pour`). Returns nothing when N ≥ `template_count`. |
| `tpl_N_display_name` | string | — | Human-friendly name of template at index N (e.g. `Free Pour`). Falls back to `name` when no display name is set. |
| `tpl_N_description` | string | — | Description of template at index N. Empty string when not set. |
| `tpl_N_stages` | uint | `%u` | Number of stages in template at index N |

Where N is a zero-based index (0, 1, 2, ...). Returns nothing when the index is beyond `template_count`.

See the [Brew Template Guide](brew-template-guide.md#template-picker-pad) for a complete example of building a template picker pad with these bindings.

---

## Naming Pattern

All stage bindings follow a consistent `stage_XXX_YYY` pattern:

|  | `_target` | `_current` | `_remaining` | `_pct` |
|--|-----------|-----------|-------------|--------|
| **weight** | `stage_weight_target` | `stage_weight_current` | `stage_weight_remaining` | `stage_weight_pct` |
| **time** | `stage_time_target` | `stage_time_current` | `stage_time_remaining` | `stage_time_pct` |
| **flow** | `stage_flow_target` | `stage_flow_current` | — | `stage_flow_pct` |

- **`_target`** — what the template asks for this stage
- **`_current`** — where you are right now
- **`_remaining`** — how far to go (weight and time only)
- **`_pct`** — progress as a percentage (unclamped, can exceed 100%)

All stage bindings are **always numeric** — they return `0` rather than `---` when no target is defined, making them safe to use directly in expressions and widgets without null checks.

---

## Built-in Templates

### free_pour

Simple two-stage workflow — arm, pour, stop.

| # | Stage Name | Type | Target Weight | Target Flow | Time |
|---|-----------|------|-------------|-------------|------|
| 0 | Ready | Auto-weight (2 g) | — | — | — |
| 1 | Brewing | Manual | — | — | — |

### v60

Five-stage pour-over with dose capture.

| # | Stage Name | Type | Target Weight | Target Flow | Time |
|---|-----------|------|-------------|-------------|------|
| 0 | Place cup | Manual | — | — | — |
| 1 | Dosing | Manual | — | — | — |
| 2 | Prep cup | Manual | — | — | — |
| 3 | Ready | Auto-weight (2 g) | — | — | — |
| 4 | Brewing | Manual | — | — | — |

### rao_v60

Six-stage Rao-method V60 with bloom stage, flow targets, and named captures.

| # | Stage Name | Type | Target Weight | Target Flow | Time | Notes |
|---|-----------|------|-------------|-------------|------|-------|
| 0 | Place cup | Manual | — | — | — | — |
| 1 | Dose beans | Manual | 16 g | — | — | Tares on enter; captures dose on exit |
| 2 | Prep | Manual | — | — | — | Tares on enter |
| 3 | Arm pour | Auto-weight (2 g) | 60 g | — | — | Tares on enter; starts timer on first pour |
| 4 | Bloom | Auto-time | 60 g | 6.0 g/s | 45 s | Beep on enter; captures "Bloom Water" on exit |
| 5 | Main pour | Manual | 250 g | 5.0 g/s | — | Beep on enter; user taps Done |

---

## Practical Examples

### Weight Display with Remaining

```
[brew:stage_weight_current;%.0f] / [brew:stage_weight_target;%.0f] g
```
→ `142 / 250 g` during Main pour

### Countdown During Bloom

```
[brew:stage_time_remaining]s left
```
→ `23s left` during the Bloom auto-time stage

### Pour Progress Gauge

```json
{
  "type": "gauge",
  "data_binding": "[brew:stage_weight_pct]",
  "min_binding": "0",
  "max_binding": "100"
}
```

### Flow Rate Guidance with Color Zones

Use `stage_flow_pct` (100% = on target) for color feedback:

```
[expr:threshold([brew:stage_flow_pct],0,808080,60,FF4444,80,FFAA00,95,00FF00,105,FFAA00,120,FF4444)]
```
- Gray: no flow
- Red: way too slow (< 60% of target)
- Orange: a bit slow (60–80%) or a bit fast (105–120%)
- Green: on target (95–105%)
- Red: way too fast (> 120%)

### Flow Rate Bar Chart

```json
{
  "type": "bar_chart",
  "data_binding": "[brew:stage_flow_current]",
  "min_binding": "0",
  "max_binding": "[expr:[brew:stage_flow_target] * 1.5]",
  "bar_color_binding": "[expr:threshold([brew:stage_flow_pct],0,808080,60,FF4444,80,FFAA00,95,00FF00,105,FFAA00,120,FF4444)]"
}
```

### Bloom Timer Progress

```
Bloom: [brew:stage_time_current]s / [brew:stage_time_target]s
```
→ `Bloom: 23s / 45s`

### Stage-Aware Background Color

```
[expr:[brew:active]=="1"?"1a3a1a":"1a1a2e"]
```

### Instruction with Fallback

```
[brew:instruction|Tap Start to begin]
```
→ Shows `Tap Start to begin` when idle, switches to stage instruction text during brew.

### Advance Button with Dynamic Label

Single button that handles the full brew cycle:

- Action: `brew`, payload: `advance`
- Center label: `[brew:next_label]`

> **Note**: Use a separate **Set Template** action (e.g. `set_template:rao_v60`) on a template selector pad to choose which template the Advance button uses. `[brew:next_label]` automatically reflects the selected template.

Label progression for Rao V60:

| State | `[brew:next_label]` |
|-------|---------------------|
| Idle | `Start Rao V60` |
| Place cup | `Weigh beans` |
| Dose beans | `Log dose` |
| Prep | `Ready` |
| Arm pour | `Armed` |
| Bloom | `Blooming...` |
| Main pour | `Done` |
| Done | `Brew again` |

### Dose and Ratio Summary

```
[brew:dose;%.1f]g → [brew:water;%.1f]g (1:[brew:ratio;%.1f])
```
→ `16.0g → 250.3g (1:15.6)`

### Weight Sparkline During Brew

```json
{
  "type": "sparkline",
  "data_binding": "[brew:weight]",
  "time_window": 300,
  "min_binding": "0",
  "max_binding": "[brew:stage_weight_target]"
}
```

### Conditional Flow Display

Only show flow rate during an active brew:

```
[expr:[brew:active]=="1"?[brew:flow_rate;%.1f]:"--.-"] g/s
```

---

## Quick Reference Card

### `[scale:]` — 7 bindings

| Key | Example Output |
|-----|---------------|
| `weight` | `123.4` |
| `flow_rate` | `2.3` |
| `calibration_factor` | `1123.6415` |
| `offset` | `71839` |
| `available` | `ON` |
| `cal_weight` | `251.5` |
| `status` | `idle` |

### `[brew:]` — 23 bindings

| Key | Example Output |
|-----|---------------|
| `weight` | `142.3` |
| `flow_rate` | `2.5` |
| `timer` | `4:05` |
| `stage` | `Bloom` |
| `active` | `1` |
| `template` | `rao_v60` |
| `display_name` | `James Rao V60` |
| `dose` | `16.0` |
| `water` | `142.3` |
| `ratio` | `8.9` |
| `instruction` | `Pour to 60g, then swirl gently. 23s remaining` |
| `next_label` | `Blooming...` |
| `stage_weight_target` | `60` |
| `stage_weight_current` | `142.3` |
| `stage_weight_remaining` | `0` |
| `stage_weight_pct` | `237` |
| `stage_time_target` | `45` |
| `stage_time_current` | `22` |
| `stage_time_remaining` | `23` |
| `stage_time_pct` | `49` |
| `stage_flow_target` | `6.0` |
| `stage_flow_current` | `2.5` |
| `stage_flow_pct` | `42` |
