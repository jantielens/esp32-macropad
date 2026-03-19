# Smart Coffee / V60 Scale — Architecture Brainstorm

> **Status**: V1 brew_manager implemented (March 2026)
> **Base**: ESP32 Macropad codebase
> **Prerequisite**: HX711 POC validated on headless ESP32 (see `sample/hx711-scale-poc.md`)

## Vision

A smart V60 pour-over scale with rich touchscreen UI: real-time weight display, flow rate sparkline, guided brew workflow with step-by-step instructions, brew history, and recipe templates. Built on top of the macropad firmware to reuse its display, widget, binding, and web portal infrastructure.

## Target Scenario (Brainstorm)

1. **Dashboard** — live weight (big font), flow rate sparkline, brew-at-a-glance stats
2. **Guided V60 brew** — step-by-step workflow:
   - User initiates new brew (button press)
   - Scale tares to zero
   - Dose beans (screen shows target weight, guides user)
   - Add bloom water (guided pour with target)
   - Bloom timer (countdown)
   - Main pour (flow rate guidance)
   - Drawdown (timer)
   - Brew complete (button press or auto-detect)
3. **Brew history** — stats stored per brew on LittleFS
4. **Brew templates** — reusable recipe definitions (JSON on LittleFS)

## Codebase Fitness Assessment

### Direct Reuse (no changes needed)

| Coffee Scale Need | Existing Subsystem |
|---|---|
| Big weight display | Pad labels with `[brew:weight;%.1f]` binding + label style DSL (font, alignment) |
| Flow rate sparkline | Sparkline widget with `[brew:flow_rate]` data binding |
| Weight history chart | Sparkline widget with `[brew:weight]` data binding |
| Gauge visualization | Gauge widget (arc + needle) |
| Bar chart (pour progress) | Bar chart widget |
| Button interactions | Pad buttons with tap/long-press actions |
| Screen navigation | Screen registry + `action_dispatch(type="screen")` |
| Screensaver / sleep | Screen saver manager |
| Web portal config | Full web portal (home, network, firmware pages) |
| OTA firmware updates | Firmware page |
| WiFi / mDNS | WiFi manager |
| MQTT / Home Assistant | MQTT subsystem + HA discovery (optional) |
| Audio feedback | Audio subsystem with beep patterns (if board has codec) |

### New Modules Needed

| Module | Purpose | Size Estimate |
|---|---|---|
| `brew_manager.cpp/h` | Headless brew state machine (no LVGL, no UI) | ~300 lines |
| `brew_binding.cpp/h` | `[brew:]` binding scheme registration | ~100 lines |
| `brew_guide_widget.cpp` | Widget rendering brew instructions + timer + progress | ~200 lines |
| `hx711_sensor.cpp/h` | HX711 sensor driver with EMA filter | ~150 lines (from POC) |
| `ACTION_TYPE_BREW` | New action type in `action_dispatch.cpp` | ~20 lines |
| `brew_storage.cpp/h` | LittleFS brew history + recipe templates | ~200 lines |
| Board overrides | `board_overrides.h` for scale-specific board | ~30 lines |

### Implementation Status (V1 — Free Pour with Auto-Start)

**Implemented:**
- `brew_manager.cpp/h` — Minimal state machine: IDLE → READY → BREWING → DONE
  - Auto-start: timer begins when weight exceeds 2g threshold after tare
  - Own `millis()`-based timer (independent of timer_engine)
  - `brew_start()` / `brew_stop()` / `brew_reset()` / `brew_tick()` API
- `brew_binding.cpp/h` — `[brew:]` scheme with keys: `weight`, `flow_rate`, `timer`, `phase`, `active`
- `ACTION_TYPE_BREW` in `pad_config.h` + `action_dispatch.cpp` — payloads: `start`, `stop`, `reset`
- `brew_tick()` wired into `hx711_loop_cb()` after each `poll_once()` sample
- Compile gate: `HAS_DISPLAY && HAS_SENSOR_HX711`

**Not yet implemented (future layers):**
- `brew_guide_widget.cpp` — Widget rendering brew instructions + timer + progress
- `brew_storage.cpp/h` — LittleFS brew history + recipe templates
- Phased recipes (dose → bloom → pour → drawdown)
- Auto-stop / lift-to-stop detection
- Ratio calculator
- Pour rate guidance bands

### Minor Changes to Existing Code

| Change | Reason |
|---|---|
| Relax `data_stream` compile gate | Currently `HAS_DISPLAY && HAS_MQTT` — should be `HAS_DISPLAY` (binding resolution is already scheme-agnostic) |
| Add `brew` to action type list | `pad_config.h` + `action_dispatch.cpp` |
| Register `[brew:]` scheme | `binding_template` scheme registry (same pattern as health/time/expr) |

## Key Architectural Decision: Pad-Based Hybrid

### Decision

Use **configurable pads** (not custom screens) for the brew UI. The brew state machine runs headlessly and exposes state through a **`[brew:]` binding scheme**. A **brew guide widget** renders the guided workflow inside one pad button, while adjacent buttons show sparklines, gauges, and action buttons.

### Why Not a Custom `BrewGuideScreen`?

- Loses access to sparkline, gauge, bar chart widgets (would need to reimplement)
- Loses pad editor configurability (layout becomes hardcoded)
- Loses binding engine integration (labels would need manual LVGL updates)
- More code for less flexibility

### Why Not Put the State Machine Inside the Widget?

- Widgets are passive data displayers — they receive values and render
- A brew state machine is active: it advances phases, checks timers, computes targets
- Mixing concerns makes testing harder and the widget non-reusable

### The Hybrid Pattern

```
┌─────────────────────────────────────────────────────────────┐
│  Pad (JSON-configured via web portal pad editor)            │
│  ┌───────────┬──────────────────────────┬──────────────┐    │
│  │ Weight    │  Brew Guide Widget       │ Flow Rate    │    │
│  │ gauge     │  (instructions, timer,   │ sparkline    │    │
│  │ [brew:]   │   progress — rendered    │ [brew:]      │    │
│  │           │   by widget type)        │              │    │
│  ├───────────┼────────────┬─────────────┼──────────────┤    │
│  │  [Tare]   │   [Next]   │  [Cancel]   │ Brew stats   │    │
│  │  action:  │  action:   │  action:    │ labels via   │    │
│  │  brew:tare│  brew:next │  brew:stop  │ [brew:...]   │    │
│  └───────────┴────────────┴─────────────┴──────────────┘    │
└─────────────────────────────────────────────────────────────┘
         │                                       │
         ▼                                       ▼
┌─────────────────────────┐    ┌──────────────────────────────┐
│  [brew:] binding scheme │    │  data_stream ring buffers    │
│  Resolves keys from     │    │  Poll [brew:weight] etc.     │
│  brew_manager state     │    │  Feed sparkline / gauge      │
└────────────┬────────────┘    └──────────────────────────────┘
             │
             ▼
┌─────────────────────────────────────────────────────────────┐
│  brew_manager.cpp  (headless, host-testable)                │
│                                                             │
│  State: IDLE → DOSE → BLOOM_POUR → BLOOM_WAIT → POUR →     │
│         DRAWDOWN → DONE                                     │
│                                                             │
│  API:  brew_start(recipe) / brew_advance() / brew_cancel()  │
│        brew_tare() / brew_get_weight() / brew_get_phase()   │
│        brew_get_instruction() / brew_get_timer_str()        │
│        brew_get_flow_rate() / brew_get_progress_pct()       │
│                                                             │
│  Reads HX711 sensor, manages timers, computes flow rate     │
│  Stores completed brew stats to LittleFS                    │
└─────────────────────────────────────────────────────────────┘
```

### Critical Insight: Binding Resolution is Scheme-Agnostic

The `data_stream_poll()` function calls `binding_template_resolve()` — the **general binding resolver** that iterates all registered schemes. It is NOT MQTT-only despite the `HAS_MQTT` compile gate.

This means:
- `[brew:weight;%.1f]` in a sparkline data binding → data_stream polls it every cycle → ring buffer fills → sparkline renders history
- `[brew:flow_rate;%.1f]` works the same way
- No special plumbing needed — just register the `[brew:]` scheme

### Brew Binding Scheme Keys

| Key | Example Output | Description |
|---|---|---|
| `weight` | `"16.2"` | Current weight (grams) |
| `flow_rate` | `"3.2"` | Current flow rate (g/s) |
| `phase` | `"Bloom Wait"` | Current brew phase name |
| `instruction` | `"Pour to 48g"` | Current step instruction text |
| `timer` | `"0:23"` | Elapsed or countdown timer |
| `progress` | `"45"` | Brew progress percentage |
| `target` | `"48.0"` | Current target weight |
| `total_time` | `"3:42"` | Total brew elapsed time |
| `dose` | `"16.0"` | Bean dose weight |
| `active` | `"1"` / `"0"` | Whether a brew is in progress |

### Action Types

| Action | Trigger | Effect |
|---|---|---|
| `brew:start` | Button tap | Start brew with active recipe, tare scale |
| `brew:next` | Button tap | Advance to next phase |
| `brew:cancel` | Button tap | Abort current brew |
| `brew:tare` | Button tap | Zero the scale |

### Brew Guide Widget

A new `WidgetType` registered in the widget system. Renders inside a (large, spanning) pad button:
- Current phase instruction (large text)
- Timer display
- Progress indicator (arc or bar)
- Phase indicator dots

The hosting button's tap action would typically be `brew:next` (advance step).

## Recipe Template Format (Draft)

```json
{
  "name": "V60 — 1 Cup",
  "dose_g": 16.0,
  "ratio": 15.0,
  "phases": [
    { "type": "dose",       "target_g": 16.0,  "instruction": "Add %g beans" },
    { "type": "pour",       "target_g": 48.0,  "instruction": "Bloom: pour to %g" },
    { "type": "wait",       "duration_s": 30,   "instruction": "Bloom for %ds" },
    { "type": "pour",       "target_g": 150.0,  "instruction": "Pour to %g" },
    { "type": "pour",       "target_g": 240.0,  "instruction": "Final pour to %g" },
    { "type": "drawdown",   "instruction": "Wait for drawdown" }
  ]
}
```

Stored on LittleFS as `/config/brew_recipe_N.json`. Selectable via web portal or a pad button.

## High-End Coffee Scale Feature Survey

Research into what premium scales (Acaia Lunar/Pearl, Decent Scale, Felicita Arc/Incline, Timemore Black Mirror Plus, Hiroia Jimmy) offer beyond basic weight + timer.

### Brew Workflow & Guidance

| Feature | Description | Relevance |
|---|---|---|
| **Auto-start timer** | Timer begins when scale detects first pour (weight increase after tare) | High — eliminates a manual step |
| **Auto-stop / drawdown detection** | Timer stops or marks phase complete when flow rate drops to ~0 for N seconds | High — hands-free brew completion |
| **Multi-recipe storage** | Save/recall named recipes with dose, ratio, and phase targets | High — already planned in recipe template format |
| **Ratio calculator** | Enter dose weight, auto-compute total water target (e.g., 16g × 15 = 240g) | High — simple math, big UX win |
| **Phase-by-phase guided pour targets** | Show "pour to Xg" with countdown per phase | High — core of the guided brew workflow |

### Real-Time Pour Analytics

| Feature | Description | Relevance |
|---|---|---|
| **Flow rate visualization** | Real-time g/s display | Already implemented (`[scale:flow_rate]`) |
| **Pour rate guidance bands** | Show ideal flow rate zone (e.g., 3–5 g/s shaded on sparkline), alert if too fast/slow | Medium — sparkline reference lines could support this |
| **Cumulative pour curve** | Weight-over-time graph showing actual vs. ideal pour profile | Medium — sparkline with `[brew:weight]` data binding |
| **Phase-level stats** | Per-phase pour time, total water poured, avg flow rate | Medium — useful for brew history/review |
| **Target weight countdown** | Show "Xg remaining" instead of or alongside absolute weight | High — simple binding: `[expr:[brew:target]-[brew:weight];%.0f]` |
| **Color-coded flow rate** | Green = ideal, yellow = too slow, red = too fast | Medium — sparkline bindable line colors could do this |

### Espresso-Specific (Future Expansion)

| Feature | Description | Relevance |
|---|---|---|
| **First-drip detection** | Detect when espresso first hits the cup (weight spike after tare) | Low — V60 focus first |
| **Extraction yield calculator** | TDS input + dose + yield → extraction percentage | Low — niche, requires refractometer |
| **Shot profiling** | Weight vs. time curve for espresso extraction analysis | Low — sparkline could visualize this later |

### Connectivity & Data

| Feature | Description | Relevance |
|---|---|---|
| **BLE scale protocol** | Expose weight/flow via BLE GATT (Acaia protocol); apps like Decent DE1 or Gaggiuino read scale data | Low — nice-to-have, not MVP |
| **Brew history with stats** | Date, recipe, dose, total time, total water, avg flow rate per phase | High — planned via `brew_storage.cpp` on LittleFS |
| **Cloud sync / export** | CSV or API export of brew logs | Low — REST API `/api/brew/history` could serve this later |
| **Espresso machine integration** | Decent DE1, Gaggiuino read scale data for profiling | Low — future BLE work |

### Hardware / UX Polish

| Feature | Description | Relevance |
|---|---|---|
| **Weighing vs. brewing modes** | "Weighing" mode (stable reading, no timer) vs. "brewing" mode (timer + flow rate active) | High — Dashboard pad vs. Brew pad achieves this |
| **Responsive tare** | Fast tare (<0.5s) with visual confirmation | High — already have non-blocking tare + beep feedback |
| **Auto-off / sleep** | Power save after inactivity | Already implemented (screensaver subsystem) |
| **Predictive stabilization** | Faster display convergence by predicting final weight from rate of change | Medium — could enhance EMA filter |
| **Water retention estimation** | Estimate water retained in coffee bed (total poured − yield in cup) | Low — interesting stat for brew history |

### Display Enhancements

| Feature | Description | Relevance |
|---|---|---|
| **Dual weight + timer display** | Large simultaneous weight and timer (Acaia Pearl style) | High — pad layout with label style DSL handles this |
| **Phase progress arc/bar** | Visual progress toward current phase target | High — gauge or bar chart widget with `[brew:progress]` |
| **Phase indicator dots** | Small dots showing current position in brew phases | Medium — planned for brew guide widget |

### Highest-Impact Features for V1

Based on the existing building blocks, these deliver the most value with least effort:

1. **Auto-start/stop timer** — timer starts on first pour detection, marks drawdown complete when flow → 0
2. **Ratio calculator** — enter beans weight, auto-fill water targets in recipe
3. **Target countdown** — "42g remaining" label (trivial with expr binding)
4. **Pour rate guidance bands** — colored zones on sparkline showing ideal flow rate
5. **Per-phase stats** — log timing and flow per phase for brew history
6. **Color-coded flow rate** — sparkline bindable line colors driven by flow rate vs. target range

## Open Questions

1. **Board selection** — Which ESP32 + display + HX711 combo? Needs display with touch, enough GPIOs for HX711 (2 pins: DOUT + SCK), and ideally PSRAM for smooth sparklines
2. **HX711 polling architecture** — Dedicated FreeRTOS task on core 1 (opposite LVGL on core 0)? Or sensor callback `loop()` hook?
3. **Flow rate algorithm** — Simple derivative (weight delta / time delta) with smoothing? Or something fancier?
4. **Brew auto-detection** — Can drawdown completion be detected automatically (flow rate → 0 for N seconds)?
5. **Multiple pads** — Dashboard pad (idle/between brews) + Brew pad (active brew)? Auto-navigate on `brew:start`?
6. **Web portal brew page** — Recipe editor, brew history viewer — how much portal work?
7. **Offline-first** — Scale should work without WiFi. MQTT/HA integration is a nice-to-have, not a requirement. Binding engine and data_stream need to work without `HAS_MQTT`
8. **Data stream compile gate** — `HAS_DISPLAY && HAS_MQTT` should become `HAS_DISPLAY` to support local-only bindings like `[brew:]` and `[health:]` in sparklines without MQTT

## Summary

The macropad codebase is a strong fit. The key enabler is the **scheme-agnostic binding resolver** — a `[brew:]` scheme plugs into labels, sparklines, gauges, bar charts, and conditional visibility with zero plumbing changes. The pad system provides configurable layouts with rich widget support. The brew state machine stays headless and testable. Total new code estimate: ~1000 lines across 6 new files + ~40 lines of changes to existing code.
