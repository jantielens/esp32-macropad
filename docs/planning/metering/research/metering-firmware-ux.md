# Sensor-Assisted Metering — Firmware UX Specification

**Author:** Burns (Project Lead)  
**Date:** 2026-04-13  
**Status:** Draft v3 — MVE Scope  
**Source:** `metering-ux.md` (Moe's product vision)  
**Target Codebase:** `esp32-macropad` branch `feature/darkroom-timer`

---

## 1. Overview

This document translates Moe's metering UX vision into concrete firmware building blocks
using the binding patterns already established by `expose_timer` and `test_strip`.

**Four phases, three new bindings, one new sensor driver:**

| Phase | Purpose | Firmware Module | Binding Scheme |
|-------|---------|----------------|----------------|
| 1a — Read Lref | Measure bare-bulb light level | `paper_cal.cpp` | `"cal"` |
| 1b — Paper Test Strip | Run bare-bulb test strip, user identifies Zone V time | — | `"strip"` (existing) |
| 2 — Print Prep | Measure bright + dark spots → grade + time recommendation | `meter.cpp` | `"meter"` |
| 3 — Print | Expose or test strip with recommended values | — | `"expose"` / `"strip"` (existing) |

**Shared memory binding:** `shared_mem.cpp` — a simple key-value store (`"mem"` scheme)
accessible to all bindings. Carries values between phases so the user doesn't have to
write them down and re-enter them manually.

**New sensor driver:** `tsl2591_sensor.cpp` — I2C driver for the TSL2591 light sensor puck.

**Data flow between phases (via shared memory):**
- Phase 1a writes: **Lref** (lux)
- Phase 1b: user identifies Zone V time from the test strip segment table and enters it
  via +/- buttons on the Meter pad (or a dedicated "set Zone V" screen)
- Phase 2 reads: **Lref** (auto-populated from shared memory)
- Phase 2 reads: **Zone V time** (user enters it once, stays set)
- Phase 2 writes: **recommended exposure time** and **grade**
- Phase 3: user navigates to Expose or Strip pad and manually enters the recommended time

**The +/- override buttons remain on Phase 2** for all shared values, so the user can
tweak anything the shared memory pre-populated. The happy path is zero manual entry for
Lref; Zone V time is entered once after the paper calibration and stays set for the session.

---

## 2. Phase 1a — Read Lref (`cal` binding)

### 2.1 What It Does

The user places the sensor puck on the easel with **no negative** in the enlarger. The
timer turns on the enlarger, reads the light sensor, turns off the enlarger, and displays
the **Lref** value (bare-bulb light level in lux).

The `cal` binding also writes Lref to shared memory (`mem:lref`) so Phase 2 can read it
automatically.

### 2.2 State Machine

```
          ┌──────────┐
          │   idle   │◄──── cancel (from reading)
          └────┬─────┘
               │ start
               ▼
          ┌──────────┐
          │ reading  │  Shelly ON → settle → TSL2591 read → Shelly OFF
          └────┬─────┘           → write Lref to shared memory
               │ reading complete
               ▼
          ┌──────────┐
          │   done   │  Display Lref.
          └──────────┘
               │ start → back to reading (re-read)
               │ cancel → back to idle
```

### 2.3 Action Commands

Dispatched via `{"type": "cal", "cal_command": "..."}` in pad button config.

| Command | When | Effect |
|---------|------|--------|
| `start` | idle or done | Shelly ON → read TSL2591 → Shelly OFF → write `mem:lref` → done |
| `cancel` | any | → idle. Shelly OFF if on. Clears reading. |

Two commands. After a successful read, the binding internally calls
`shared_mem_dispatch("set_lref:1847.3")` to make Lref available to Phase 2.

### 2.4 Binding Tokens

Registered as scheme `"cal"`.

| Token | Description | Example |
|-------|-------------|---------|
| `[cal:state]` | Current state | `"idle"`, `"reading"`, `"done"` |
| `[cal:lref]` | Lref reading (lux) | `"1847.3"` or `"---"` if not read |

Two tokens. Minimal.

### 2.5 Sample Pad Config — Paper Calibration (Read Lref)

4 columns × 8 rows grid. Most of the screen is informational — only one action button.

```
Row 0:  [ Instructions (full width)                                       ]
        "No negative in enlarger. Lens at working aperture.
         Grade 2 filter. Sensor puck on easel."
Row 1:  [ State: [cal:state]                                              ]
Row 2:  [ Lref: [cal:lref] lux                                           ]
Row 3:  [ (empty — visual spacer)                                         ]
Row 4:  [ "Next: go to Strip pad for bare-bulb test strip"               ]
Row 5:  [ "Use base time 5.0s, 7 segments, 1/3 stop"                    ]
Row 6:  [ (empty)                                                         ]
Row 7:  [ Read Lref (start)           ][ (hold to cancel)                 ]
```

**Button details:**

```jsonc
// Row 0: Instructions (read-only display)
{ "col": 0, "row": 0, "col_span": 4, "row_span": 1,
  "label_center": "No negative. Lens open.\nGrade 2 filter. Puck on easel.",
  "bg_color": "000000", "border_width": "0" },

// Row 1: State
{ "col": 0, "row": 1, "col_span": 4,
  "label_top": "State", "label_center": "[cal:state]",
  "bg_color": "000000", "border_width": "0" },

// Row 2: Lref reading
{ "col": 0, "row": 2, "col_span": 4,
  "label_top": "Lref (lux)", "label_center": "[cal:lref]",
  "bg_color": "000000", "border_width": "0" },

// Row 4-5: Next step instructions
{ "col": 0, "row": 4, "col_span": 4,
  "label_center": "Next: go to Strip pad\nfor bare-bulb test strip",
  "bg_color": "000000", "border_width": "0" },
{ "col": 0, "row": 5, "col_span": 4,
  "label_center": "Use base 5.0s, 7 segs, 1/3 stop",
  "bg_color": "000000", "border_width": "0" },

// Row 7: Action button
{ "col": 0, "row": 7, "col_span": 4,
  "label_center": "Read Lref",
  "label_bottom": "(hold to cancel)",
  "actions": [{ "type": "cal", "cal_command": "start" }],
  "lp_actions": [{ "type": "cal", "cal_command": "cancel" }] }
```

### 2.6 Phase 1b — Bare-Bulb Paper Test Strip (existing Strip binding)

After Phase 1a, the user navigates to the existing **Strip** pad. No new firmware needed.

**Ideal bare-bulb test strip settings:**

| Setting | Value | Why |
|---------|-------|-----|
| Base time | **5.0s** | Zone V for most multigrade papers at Grade 2 falls in the 3–8s range under typical enlarger bulbs. 5.0s as center covers 2.5–10.1s. |
| Segments | **7** | Odd number centers the base time. 7 segments at ⅓-stop covers ±1 stop. |
| Step | **⅓ stop** | Fine enough to see meaningful differences, coarse enough to cover a useful range. |
| Range | **2.5s – 10.1s** | Brackets Zone V for most setups. |

> **Note:** The Strip pad defaults to 8.0s base time (optimized for through-negative printing).
> For bare-bulb calibration, the user should change it to 5.0s. This is called out in
> the Cal pad's on-screen instructions. A future enhancement could provide a dedicated
> "Bare-Bulb Strip" pad pre-configured with 5.0s.

**Segment table at 5.0s / 7 segments / ⅓ stop:**

| Segment | F-stop offset | Cumulative time |
|---------|--------------|-----------------|
| 1 | +1.0 | 10.1s |
| 2 | +⅔ | 8.0s |
| 3 | +⅓ | 6.3s |
| 4 | 0 (center) | 5.0s |
| 5 | -⅓ | 4.0s |
| 6 | -⅔ | 3.2s |
| 7 | -1.0 | 2.5s |

**What the user does:**

1. Removes sensor puck from easel
2. Places a strip of paper on easel (still no negative, Grade 2 filter)
3. Navigates to Strip pad
4. Sets the base time to **5.0s** (`strip:set_base:5.0`)
   — 7 segments, ⅓-stop steps are the defaults
5. Taps Start — the existing test strip sequence runs
6. Develops, stops, fixes the test strip
7. Evaluates the strip: finds the segment that looks like **medium grey**
8. Notes the **cumulative time** for that segment (shown in the Strip table widget)
   — this is the **Zone V time** (e.g., 6.3 seconds)

**What if the strip is all black or all white?**
- All black: Zone V is shorter than 2.5s. Unusual — check lens aperture (try f/11 or f/16).
  Or re-run with base time 2.0s.
- All white: Zone V is longer than 10.1s. Dim enlarger or small aperture. Re-run with
  base time 16.0s.

### 2.7 User Workflow (Complete Phase 1)

```
Phase 1a — Read Lref:
1. Navigate to "Paper Cal" pad
2. Ensure: no negative, lens open, Grade 2 filter, puck on easel
3. Tap "Read Lref"
   → Enlarger ON, sensor reads, enlarger OFF
   → Screen shows: "Lref = 1847.3 lux"
   → Lref auto-saved to shared memory
4. Done — proceed to Phase 1b

Phase 1b — Test Strip:
5. Remove puck, place paper strip on easel
6. Navigate to "Strip" pad
7. Set base time to 5.0s (for bare-bulb calibration)
   (7 segments, ⅓ stop = default = good)
8. Tap Start → test strip runs
9. Develop, stop, fix
10. Find medium grey segment → note time (e.g., 6.3s = Zone V)

Ready for Phase 2: Lref already in shared memory, Zone V = 6.3s to enter on Meter pad
```

---

## 3. Phase 2 — Print Prep (`meter` binding)

### 3.1 What It Does

The user takes two light readings through the negative — one at the brightest spot on
the easel, one at the darkest. The timer computes the negative's contrast range (SBR),
recommends a filter grade, and calculates a base exposure time.

**Lref** is auto-populated from shared memory (written by Phase 1a). The user enters
**Zone V time** once via +/- buttons (from Phase 1b). Both can be overridden.

### 3.2 User Inputs

| Setting | Source | Entry method | Token |
|---------|--------|-------------|-------|
| Lref | Phase 1a via shared memory | Auto-populated. +/- buttons for override. | `[mem:lref]` read at startup |
| Zone V time | Phase 1b (user noted from strip) | +/- buttons (±1s and ±0.1s) | `[meter:zone5_time]` |

**Lref:** Pre-filled from `[mem:lref]` when the user enters the Meter pad. If Phase 1a
was done, this is automatic — zero taps. The +/- buttons allow correction if the user
changed enlarger height/aperture since calibration.

**Zone V time:** Entered once per paper calibration. The user reads the cumulative time
of their chosen medium-grey segment from the Strip table (e.g., 6.3s) and dials it in
with ±1s and ±0.1s buttons. Stays set for subsequent negatives on the same paper.

**Without inputs:** If Lref or Zone V are not set, the meter still computes SBR and gives
a grade recommendation (SBR is purely the ratio `log10(L_bright/L_dark)` — no calibration
needed). The exposure time shows `"---"`.

### 3.3 State Machine

```
          ┌──────────┐
          │   idle   │◄──── cancel (from any state)
          └────┬─────┘
               │ focus_on
               ▼
        ┌────────────────┐
        │ awaiting_bright │  Shelly ON (enlarger on for framing).
        │                 │  User positions puck on bright spot.
        └───────┬────────┘
                │ read_bright
                ▼
        ┌────────────────┐
        │ awaiting_dark   │  Enlarger still on.
        │                 │  User repositions puck to dark spot.
        └───────┬────────┘
                │ read_dark → compute SBR, grade, time
                ▼
          ┌──────────┐
          │ results  │  Shelly OFF. Display: SBR, grade, time.
          └──────────┘
               │ focus_on → back to awaiting_bright (re-meter)
               │ cancel → back to idle
```

**Key design choices:**

- **Lref comes from shared memory, not a sensor reading in Phase 2.** No bare-bulb step
  needed before each print. The user does Phase 1a once per session (or per setup change).

- **`focus_on` turns on the enlarger.** The enlarger stays on throughout both readings so
  the user can see the projected image to find bright/dark spots. Turned off after `read_dark`.

- **Automatic swap if bright < dark.** If the user accidentally reads dark first, the
  firmware silently swaps (bright should always be the higher lux reading).

- **SBR works without calibration.** `SBR = log10(L_bright / L_dark)` — Lref cancels out.
  Grade recommendation is always available. Only the exposure time needs Lref + Zone V.

### 3.4 Action Commands

Dispatched via `{"type": "meter", "meter_command": "..."}`.

| Command | When | Effect |
|---------|------|--------|
| `set_lref:N.N` | any | Override Lref value (lux) |
| `add_lref:N.N` | any | Adjust Lref ±N.N |
| `set_zone5:N.N` | any | Set Zone V time (seconds) |
| `add_zone5:N.N` | any | Adjust Zone V time ±N.N seconds |
| `focus_on` | idle or results | Shelly ON → awaiting_bright |
| `read_bright` | awaiting_bright | Read TSL2591 → store L_bright → awaiting_dark |
| `read_dark` | awaiting_dark | Read TSL2591 → Shelly OFF → compute all → results |
| `cancel` | any | → idle. Shelly OFF if on. Clears readings (keeps lref + zone5). |

**On init:** the `meter` binding reads `[mem:lref]` from shared memory to pre-populate
its internal Lref value. If shared memory has no Lref, the meter starts with Lref = 0
(shown as `"---"`).

### 3.5 The Math (Firmware Implementation)

All computed after `read_dark` is captured:

```c
// Auto-correct if user swapped bright/dark
if (L_bright < L_dark) { swap(L_bright, L_dark); }

// SBR — always computable (no calibration needed)
float SBR = log10f(L_bright / L_dark);

// Grade recommendation (lookup table) — always available
float grade = sbr_to_grade(SBR);

// Exposure time — only if both Lref and zone5_time are set
if (Lref > 0 && zone5_time > 0) {
    float D_bright = log10f(Lref / L_bright);
    float D_dark   = log10f(Lref / L_dark);
    float D_mid    = (D_bright + D_dark) / 2.0f;
    float T_base   = zone5_time * powf(10.0f, D_mid);
}
```

**SBR-to-Grade lookup table** (compiled into firmware):

```c
static constexpr struct { float sbr_max; float grade; } SBR_GRADE_MAP[] = {
    { 0.60f, 5.0f },
    { 0.70f, 4.5f },
    { 0.80f, 4.0f },
    { 0.90f, 3.5f },
    { 1.00f, 3.0f },
    { 1.10f, 2.5f },
    { 1.20f, 2.0f },
    { 1.35f, 1.5f },
    { 1.50f, 1.0f },
    { 1.65f, 0.5f },
    { 999.f, 0.0f },
};
```

> **Calibration note:** These SBR breakpoints are standard Zone System estimates. They
> will need tuning with real hardware and real paper. After the first build, print a known
> negative at each recommended grade and verify the results match. Expect adjustments of
> ±0.1 SBR per entry — a one-line firmware change each. Flag this as a task during
> hardware bring-up.

### 3.6 Binding Tokens

Registered as scheme `"meter"`.

| Token | Description | Example |
|-------|-------------|---------|
| `[meter:state]` | Current state | `"idle"`, `"awaiting_bright"`, `"awaiting_dark"`, `"results"` |
| `[meter:lref]` | Lref (lux, from shared mem or manual override) | `"1847"` or `"---"` |
| `[meter:zone5_time]` | Zone V time (seconds, manual entry) | `"6.3"` or `"---"` |
| `[meter:l_bright]` | Bright spot reading (lux, from sensor) | `"423.7"` or `"---"` |
| `[meter:l_dark]` | Dark spot reading (lux, from sensor) | `"14.2"` or `"---"` |
| `[meter:sbr]` | Subject Brightness Range (computed) | `"1.25"` or `"---"` |
| `[meter:grade]` | Recommended grade (computed) | `"2"` or `"2.5"` or `"---"` |
| `[meter:grade_label]` | Human-friendly description | `"Normal"`, `"Contrasty"`, etc. |
| `[meter:time]` | Recommended base time in seconds (computed) | `"14.2"` or `"---"` |
| `[meter:relay]` | Current relay state | `"ON"` / `"OFF"` |

### 3.7 Sample Pad Config — Print Prep (Meter)

```
Row 0:  [ Lref: [meter:lref] (auto)  ][ Zone V: [meter:zone5_time] s    ]
Row 1:  [ -0.1  ][ -1  ][ +1  ][ +0.1 ]   ← Zone V adjustment
Row 2:  [ Bright: [meter:l_bright] ][ Dark: [meter:l_dark]              ]
Row 3:  [ SBR: [meter:sbr]        ][ Grade: [meter:grade]               ]
Row 4:  [        [meter:grade_label]                                     ]
Row 5:  [ Suggested time: [meter:time] s                                 ]
Row 6:  [ 1. Focus ON              ][ State: [meter:state]               ]
Row 7:  [ 2. BRIGHT                ][ 3. DARK                            ]
```

**Button details:**

```jsonc
// Row 0: Lref (auto from shared memory, read-only display) + Zone V display
{ "col": 0, "row": 0, "col_span": 2, "label_top": "Lref (auto)",
  "label_center": "[meter:lref]", "bg_color": "000000" },
{ "col": 2, "row": 0, "col_span": 2, "label_top": "Zone V time",
  "label_center": "[meter:zone5_time] s", "bg_color": "000000" },

// Row 1: Zone V fine adjustment (±0.1s and ±1s)
{ "col": 0, "row": 1, "label_center": "-0.1",
  "actions": [{ "type": "meter", "meter_command": "add_zone5:-0.1" }] },
{ "col": 1, "row": 1, "label_center": "-1",
  "actions": [{ "type": "meter", "meter_command": "add_zone5:-1" }] },
{ "col": 2, "row": 1, "label_center": "+1",
  "actions": [{ "type": "meter", "meter_command": "add_zone5:1" }] },
{ "col": 3, "row": 1, "label_center": "+0.1",
  "actions": [{ "type": "meter", "meter_command": "add_zone5:0.1" }] },

// Row 2: Spot readings (read-only, filled by sensor)
{ "col": 0, "row": 2, "col_span": 2, "label_top": "Bright spot (lux)",
  "label_center": "[meter:l_bright]", "bg_color": "000000" },
{ "col": 2, "row": 2, "col_span": 2, "label_top": "Dark spot (lux)",
  "label_center": "[meter:l_dark]", "bg_color": "000000" },

// Row 3: Computed results
{ "col": 0, "row": 3, "col_span": 2, "label_top": "SBR",
  "label_center": "[meter:sbr]", "bg_color": "000000" },
{ "col": 2, "row": 3, "col_span": 2, "label_top": "Grade",
  "label_center": "[meter:grade]", "bg_color": "000000" },

// Row 4: Grade description (full width)
{ "col": 0, "row": 4, "col_span": 4, "label_center": "[meter:grade_label]",
  "bg_color": "000000" },

// Row 5: Recommended exposure time (full width, prominent)
{ "col": 0, "row": 5, "col_span": 4, "label_top": "Suggested exposure",
  "label_center": "[meter:time] s", "bg_color": "000000" },

// Row 6: Focus ON + state display
{ "col": 0, "row": 6, "col_span": 2,
  "label_center": "1. Focus ON", "label_bottom": "(insert neg first)",
  "actions": [{ "type": "meter", "meter_command": "focus_on" }],
  "lp_actions": [{ "type": "meter", "meter_command": "cancel" }] },
{ "col": 2, "row": 6, "col_span": 2, "label_top": "State",
  "label_center": "[meter:state]", "bg_color": "000000" },

// Row 7: Read BRIGHT + Read DARK
{ "col": 0, "row": 7, "col_span": 2,
  "label_center": "2. BRIGHT",
  "actions": [{ "type": "meter", "meter_command": "read_bright" }] },
{ "col": 2, "row": 7, "col_span": 2,
  "label_center": "3. DARK",
  "actions": [{ "type": "meter", "meter_command": "read_dark" }] }
```

### 3.8 User Workflow (Step by Step)

```
First time (after Phase 1):
1. Navigate to "Meter" pad
2. Lref is already populated (from shared memory via Phase 1a) ✅
3. Set Zone V time to match Phase 1b (e.g., 6.3s — use ±1 and ±0.1 buttons)
   (One-time per paper calibration. Stays set for subsequent negatives.)

Per negative:
4. Insert negative in enlarger, focus image on easel
5. Tap "1. Focus ON"
   → Enlarger turns ON (stays on for framing/reading)
   → [meter:state] shows "awaiting_bright"
6. Place puck on BRIGHTEST spot on easel
7. Tap "2. BRIGHT"
   → Sensor reads, [meter:l_bright] shows value
   → [meter:state] shows "awaiting_dark"
8. Move puck to DARKEST spot on easel
9. Tap "3. DARK"
   → Sensor reads, enlarger turns OFF
   → Timer computes SBR, grade, time
   → [meter:sbr] shows "1.25"
   → [meter:grade] shows "2"
   → [meter:grade_label] shows "Normal+"
   → [meter:time] shows "14.2"
   → [meter:state] shows "results"
10. Navigate to "Exposure" pad → set 14.2s → print
    OR navigate to "Strip" pad → set base 14.2s → run targeted test strip
```

---

## 4. Phase 3 — Print (existing bindings)

No new firmware needed. The user takes the recommended values from Phase 2 and manually
enters them into the existing `expose` or `strip` bindings:

**Direct print:**
- Navigate to Core Exposure Timer pad
- Set time: `expose:set_time:14.2`
- Set Grade 2 filter physically on enlarger
- Start exposure: `expose:start`

**Targeted test strip:**
- Navigate to Strip pad
- Set base time: `strip:set_base:14.2`
- Adjust range if desired (tighter: ⅓-stop, 5 segments = ±⅔ stop)
- Run strip, develop, evaluate, print at chosen time

---

## 5. Shared Memory (`mem` binding)

### 5.1 What It Is

A simple key-value store accessible to all bindings. No state machine, no relay, no I/O.
Just named floats with a binding resolver and a command dispatcher.

This is the glue between phases: Phase 1a writes Lref, Phase 2 reads it. Each binding
remains independent — they just read/write the same shared store.

### 5.2 API

```c
// shared_mem.h

void shared_mem_init();                      // Register "mem" binding scheme
void shared_mem_dispatch(const char* cmd);   // Handle set commands
// No tick() or loop() needed — pure data, no I/O.

// Direct access for other bindings to read/write programmatically
float shared_mem_get(const char* key);       // Returns 0.0f if not set
void  shared_mem_set(const char* key, float value);
```

### 5.3 Keys

| Key | Written by | Read by | Description |
|-----|-----------|---------|-------------|
| `lref` | `cal` (Phase 1a) | `meter` (Phase 2) | Bare-bulb light level (lux) |

> **Future keys:** `zone5_time` (if Strip binding learns to write the picked segment
> time), `rec_time` and `rec_grade` (from meter, for auto-populating Expose/Strip pads).

### 5.4 Binding Tokens

Registered as scheme `"mem"`.

| Token | Description | Example |
|-------|-------------|---------|
| `[mem:lref]` | Stored Lref value | `"1847.3"` or `"---"` |

Any key can be read as `[mem:keyname]`. Returns `"---"` if not set.

### 5.5 Action Commands

Dispatched via `{"type": "mem", "mem_command": "..."}`.

| Command | Effect |
|---------|--------|
| `set_lref:N.N` | Store Lref value |

Any key can be set as `set_<key>:<value>`. The shared memory binding is generic.

### 5.6 No Persistence (MVE)

Shared memory is RAM-only. Values are lost on reboot. This is intentional for MVE:
- The user redoes Phase 1a at the start of each session anyway
- NVS persistence is a v2 enhancement

### 5.7 Implementation Size

Estimated ~60-80 lines of C++. A static array of key-value pairs (key = char[16],
value = float), a resolver that formats them, and a dispatcher that parses `set_key:value`.

---

## 6. TSL2591 Sensor Driver

### 6.1 Integration Pattern

Follows the existing sensor manager pattern (`src/app/sensors/`). But unlike BME280
(which publishes periodically), the TSL2591 is **on-demand only** — reads happen when
explicitly triggered by `cal` or `meter` commands.

### 6.2 API

```c
// tsl2591_sensor.h

// Initialize TSL2591 on I2C bus. Returns true if sensor detected.
bool tsl2591_init();

// Take a single light reading. Blocks for integration time (~100-600ms).
// Returns illuminance in lux. Returns -1.0f on error.
// Must be called with I2C bus lock held.
float tsl2591_read_lux();

// Check if sensor is connected and responding.
bool tsl2591_is_connected();
```

### 6.3 I2C Bus Safety

Uses the existing `i2c_bus_lock()` / `i2c_bus_unlock()` mutex pattern:

```c
float read_sensor() {
    if (!i2c_bus_lock(pdMS_TO_TICKS(100))) return -1.0f;
    float lux = tsl2591_read_lux();
    i2c_bus_unlock();
    return lux;
}
```

### 6.4 Deferred Reads

Sensor reads block for 100-600ms. Like Shelly HTTP requests, they should run from
`loop()` (main task, internal-RAM stack), not from the LVGL render task.

Pattern: `_tick()` detects "read requested" flag → `_loop()` performs the actual I2C
read → stores result → `_tick()` advances state machine on next cycle.

---

## 7. Integration Points (Firmware Changes)

### 7.1 New Files

| File | Purpose |
|------|---------|
| `src/app/paper_cal.h` | Paper calibration header (public API: init/dispatch/tick/loop) |
| `src/app/paper_cal.cpp` | Paper calibration binding (`cal` scheme) — sensor read only |
| `src/app/meter.h` | Print prep metering header (public API: init/dispatch/tick/loop) |
| `src/app/meter.cpp` | Print prep metering binding (`meter` scheme) |
| `src/app/shared_mem.h` | Shared memory header (public API: init/dispatch + get/set) |
| `src/app/shared_mem.cpp` | Shared memory binding (`mem` scheme) — key-value store |
| `src/app/sensors/tsl2591_sensor.h` | TSL2591 driver header |
| `src/app/sensors/tsl2591_sensor.cpp` | TSL2591 I2C driver |

### 7.2 Modified Files

| File | Change |
|------|--------|
| `src/app/app.ino` | Add `#include`, `_init()`, `_loop()` calls for cal, meter, shared_mem |
| `src/app/display_task.cpp` | Add `_tick()` calls for cal and meter |
| `src/app/action_dispatch.cpp` | Add `ACTION_TYPE_CAL`, `ACTION_TYPE_METER`, `ACTION_TYPE_MEM` dispatch |
| `src/app/action_parse.cpp` | Add `cal_command` / `meter_command` / `mem_command` serialization |
| `src/app/pad_config.h` | Add `ACTION_TYPE_CAL`, `ACTION_TYPE_METER`, `ACTION_TYPE_MEM` defines |
| `src/app/sensors.cpp` | Register TSL2591 sensor |
| `sample/deviceexport.json` | Add Paper Cal and Meter pad configs |

### 7.3 Compile Guard

All new code gated by `#if IS_DARKROOM_TIMER`, same as `expose_timer` and `test_strip`.

---

## 8. MVE Limitations & Future Path

### What the MVE does NOT do (by design):

| Limitation | Why | Future fix |
|---|---|---|
| Zone V time not auto-populated | Strip binding doesn't write to shared memory (yet) | Strip writes picked/last segment time to `mem:zone5_time` |
| No "navigate to Strip" button from Cal pad | No `ACTION_TYPE_NAVIGATE_PAD` exists yet | Add pad navigation action type |
| Recommended time not auto-populated in Expose/Strip | Expose/Strip don't read from shared memory (yet) | Read `mem:rec_time` to pre-fill |
| Grade recommendation is ±½ grade | Single default SBR mapping | Per-paper profiles (MET06) |
| Midtone estimated, not measured | Only 2 spot readings | Optional 3rd reading |
| Shared memory lost on reboot | RAM-only, no NVS persistence in MVE | Save to NVS |

### Upgrade path (all firmware-only, same hardware):

1. **Strip → shared memory:** Strip binding writes picked segment time to `mem:zone5_time` — eliminates manual Zone V entry
2. **Meter → shared memory:** Meter writes `mem:rec_time` + `mem:rec_grade` — Expose/Strip auto-populate
3. **NVS persistence:** Save shared memory across reboots
4. **Pad navigation action:** "Go to Strip" and "Go to Meter" buttons for seamless flow
5. **3rd spot reading:** Optional midtone reading for more accurate exposure time
6. **Per-grade calibration:** Run paper cal at 2–3 grades for tighter SBR-to-grade mapping
7. **MET07 integration:** Sensor monitors lamp during exposure (light output compensation)
8. **Sensor grid (MET15):** Hardware add-on for instant tonal histogram
