# Smart Coffee / V60 Scale — Architecture Brainstorm

> **Status**: Brainstorm / Pre-design (March 2026)
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
