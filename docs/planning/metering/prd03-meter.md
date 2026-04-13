# PRD 03 — Print Prep Meter (Phase 2)

**Feature:** Print Prep — Bright/Dark Spot Metering  
**Depends on:** PRD 01 (TSL2591 driver + shared memory)  
**Enhanced by:** PRD 02 (Lref auto-populated via shared memory)  
**Estimated scope:** ~250 lines, 2 new files, 1 modified file (pad config)

---

## Goal

Enable the user to take two spot readings through a negative (brightest and darkest areas)
and receive a **contrast grade recommendation** and **base exposure time**. This is Phase 2
of the metering workflow — the core value of the entire feature.

**Grade recommendation works without any calibration** (purely relative — SBR is a ratio).
Exposure time requires Lref + Zone V time from Phase 1.

---

## Deliverables

### 1. Meter Binding (`meter` scheme)

**New files:** `src/app/meter.h`, `src/app/meter.cpp`

**Public API:**

```c
void meter_init();                     // Register "meter" binding scheme
void meter_dispatch(const char* cmd);  // Handle action commands
void meter_tick();                     // Drive state machine
void meter_loop();                     // Deferred I/O: sensor reads + Shelly HTTP
```

**State machine:**

```
idle ──focus_on──→ awaiting_bright ──read_bright──→ awaiting_dark ──read_dark──→ results
                                                                                    │
results ──focus_on──→ awaiting_bright (re-meter)                                    │
any ──cancel──→ idle                                                                │
```

**User inputs (manual entry via +/- buttons):**

| Setting | Entry | Token |
|---------|-------|-------|
| Lref | Auto from `[mem:lref]` on init. Override via `set_lref`/`add_lref`. | `[meter:lref]` |
| Zone V time | Manual via `set_zone5`/`add_zone5` (±0.1s and ±1s buttons). | `[meter:zone5_time]` |

**Action commands:**

| Command | When | Effect |
|---------|------|--------|
| `set_lref:N.N` | any | Override Lref value |
| `add_lref:N.N` | any | Adjust Lref ±N.N |
| `set_zone5:N.N` | any | Set Zone V time |
| `add_zone5:N.N` | any | Adjust Zone V time ±N.N |
| `focus_on` | idle or results | Shelly ON → awaiting_bright |
| `read_bright` | awaiting_bright | TSL2591 read → store L_bright → awaiting_dark |
| `read_dark` | awaiting_dark | TSL2591 read → Shelly OFF → compute → results |
| `cancel` | any | → idle. Shelly OFF. Clear readings (keep lref + zone5). |

**Computed outputs (after `read_dark`):**

```c
// SBR — always available (no calibration needed)
float SBR = log10f(L_bright / L_dark);  // auto-swap if inverted
float grade = sbr_to_grade(SBR);

// Exposure time — only if Lref > 0 and zone5_time > 0
float D_bright = log10f(Lref / L_bright);
float D_dark   = log10f(Lref / L_dark);
float D_mid    = (D_bright + D_dark) / 2.0f;
float T_base   = zone5_time * powf(10.0f, D_mid);
```

**SBR-to-Grade lookup table:**

| SBR ≤ | Grade | Label |
|-------|-------|-------|
| 0.60 | 5.0 | Very flat |
| 0.70 | 4.5 | Flat |
| 0.80 | 4.0 | Flat |
| 0.90 | 3.5 | Slightly flat |
| 1.00 | 3.0 | Normal- |
| 1.10 | 2.5 | Normal |
| 1.20 | 2.0 | Normal |
| 1.35 | 1.5 | Contrasty |
| 1.50 | 1.0 | Contrasty |
| 1.65 | 0.5 | Very contrasty |
| ∞ | 0.0 | Extremely contrasty |

> **Calibration note:** These SBR breakpoints are Zone System estimates. They need tuning
> with real hardware and real paper during bring-up. Expect ±0.1 SBR adjustments per entry.

**Binding tokens:**

| Token | Description | Example |
|-------|-------------|---------|
| `[meter:state]` | Current state | `"idle"`, `"awaiting_bright"`, `"awaiting_dark"`, `"results"` |
| `[meter:lref]` | Lref (auto or manual) | `"1847"` or `"---"` |
| `[meter:zone5_time]` | Zone V time (manual) | `"6.3"` or `"---"` |
| `[meter:l_bright]` | Bright spot reading | `"423.7"` or `"---"` |
| `[meter:l_dark]` | Dark spot reading | `"14.2"` or `"---"` |
| `[meter:sbr]` | SBR (computed) | `"1.25"` or `"---"` |
| `[meter:grade]` | Recommended grade | `"2"` or `"2.5"` or `"---"` |
| `[meter:grade_label]` | Human-friendly label | `"Normal"`, `"Contrasty"` |
| `[meter:time]` | Recommended time (seconds) | `"14.2"` or `"---"` |
| `[meter:relay]` | Relay state | `"ON"` / `"OFF"` |

**Requirements:**
- On init, read `shared_mem_get("lref")` to pre-populate Lref (zero-tap happy path)
- Auto-swap L_bright/L_dark if user reads them in wrong order
- SBR + grade always computed (even without Lref/Zone V)
- Exposure time only computed when both Lref > 0 and zone5_time > 0, else `"---"`
- Sensor reads deferred to `meter_loop()` via flag
- Shelly control follows existing fire-and-forget HTTP pattern
- Compile-gated by `#if IS_DARKROOM_TIMER`

**Test criteria:**
- Full flow: Focus ON → enlarger on → BRIGHT → DARK → enlarger off → results displayed
- SBR + grade shown even without Lref/Zone V configured
- With Lref + Zone V set, exposure time computed and shown
- Re-meter (tap Focus ON from results) → clears readings, keeps lref/zone5, enlarger back on
- Cancel from any state → idle, enlarger off
- Sensor disconnected → readings show `"---"`, graceful error

### 2. Meter Pad Config

**Modified file:** `sample/deviceexport.json` — add a "Meter" pad.

**Layout (4 cols × 8 rows):**

```
Row 0:  [ Lref: [meter:lref] (auto)  ][ Zone V: [meter:zone5_time] s    ]
Row 1:  [ -0.1  ][ -1  ][ +1  ][ +0.1 ]   ← Zone V adjustment
Row 2:  [ Bright: [meter:l_bright]    ][ Dark: [meter:l_dark]            ]
Row 3:  [ SBR: [meter:sbr]           ][ Grade: [meter:grade]             ]
Row 4:  [        [meter:grade_label]                                      ]
Row 5:  [ Suggested time: [meter:time] s                                  ]
Row 6:  [ 1. Focus ON                ][ State: [meter:state]              ]
Row 7:  [ 2. BRIGHT                  ][ 3. DARK                           ]
```

See `research/metering-firmware-ux.md` section 3.7 for full JSON button config.

### 3. Integration

| File | Change |
|------|--------|
| `src/app/app.ino` | Add `meter_init()` in setup, `meter_loop()` in loop |
| `src/app/display_task.cpp` | Add `meter_tick()` call |
| `src/app/action_dispatch.cpp` | Replace meter stub with `meter_dispatch(cmd)` |

---

## User Workflow

```
First time (after Phase 1):
1. Navigate to "Meter" pad
2. Lref auto-populated from shared memory ✅
3. Set Zone V time (e.g., 6.3s — ±0.1 and ±1 buttons)

Per negative:
4. Insert negative, focus
5. Tap "1. Focus ON" → enlarger on
6. Place puck on brightest spot → tap "2. BRIGHT"
7. Move puck to darkest spot → tap "3. DARK" → enlarger off
8. Read results: SBR, Grade, Time
9. Navigate to Expose or Strip pad → enter recommended values → print
```

---

## Out of Scope

- Paper calibration (PRD 02)
- Pre-configured bare-bulb Strip pad (PRD 04)
- Split-grade filtration breakdown (future)
- Third spot reading for midtone (future)
- NVS persistence for Zone V time (v2)
- Auto-populate Expose/Strip pad with recommended time (v2 — via `mem:rec_time`)
