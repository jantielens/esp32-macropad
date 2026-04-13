# PRD 02 — Paper Calibration (Phase 1a)

**Feature:** Paper Calibration — Read Lref  
**Depends on:** PRD 01 (TSL2591 driver + shared memory)  
**Unlocks:** PRD 03 (auto-populated Lref on Meter pad)  
**Estimated scope:** ~100 lines, 2 new files, 1 modified file (pad config)

---

## Goal

Enable the user to take a bare-bulb light reading (Lref) with one button press. The
reading is stored in shared memory so Phase 2 (Meter) can auto-populate it. This is
Phase 1a of the metering workflow.

---

## Deliverables

### 1. Paper Cal Binding (`cal` scheme)

**New files:** `src/app/paper_cal.h`, `src/app/paper_cal.cpp`

**Public API (same pattern as expose_timer/test_strip):**

```c
void paper_cal_init();             // Register "cal" binding scheme
void paper_cal_dispatch(const char* cmd);  // Handle action commands
void paper_cal_tick();             // Drive state machine (called from LVGL task)
void paper_cal_loop();             // Deferred I/O: sensor read + Shelly HTTP
```

**State machine:**

```
idle ──start──→ reading ──complete──→ done
                                       │ start → reading (re-read)
any ──cancel──→ idle
```

**Action commands:**

| Command | Effect |
|---------|--------|
| `start` | Shelly ON → wait for lamp settle (~500ms) → TSL2591 read → Shelly OFF → write `mem:lref` → done |
| `cancel` | → idle. Shelly OFF if on. Clear reading. |

**Binding tokens:**

| Token | Description | Example |
|-------|-------------|---------|
| `[cal:state]` | Current state | `"idle"`, `"reading"`, `"done"` |
| `[cal:lref]` | Lref reading (lux) | `"1847.3"` or `"---"` |

**Requirements:**
- On successful read, call `shared_mem_set("lref", lux_value)` to populate shared memory
- Sensor read runs in `paper_cal_loop()` (deferred from LVGL task via flag)
- Shelly relay control follows the same fire-and-forget HTTP pattern as expose_timer
- Lamp settle time: ~500ms after Shelly ON before reading sensor (configurable constant)
- Compile-gated by `#if IS_DARKROOM_TIMER`
- Register in `app.ino` init/loop and `display_task.cpp` tick

**Test criteria:**
- Tap "Read Lref" → enlarger turns on briefly → turns off → lux value displayed
- `[cal:lref]` shows the reading on screen
- `[mem:lref]` also shows the reading (shared memory populated)
- Re-tapping "Read Lref" takes a fresh reading
- Long-press cancel returns to idle, clears reading
- Sensor disconnected → reading shows error / `"---"`

### 2. Paper Cal Pad Config

**Modified file:** `sample/deviceexport.json` — add a "Paper Cal" pad.

**Layout (4 cols × 8 rows):**

```
Row 0:  [ Instructions: "No negative. Lens open.               ]
         Grade 2 filter. Puck on easel."
Row 1:  [ State: [cal:state]                                    ]
Row 2:  [ Lref: [cal:lref] lux                                  ]
Row 3:  [ (spacer)                                               ]
Row 4:  [ "Next: go to Strip pad for bare-bulb test strip"      ]
Row 5:  [ "Use base 5.0s, 7 segs, 1/3 stop"                    ]
Row 6:  [ (spacer)                                               ]
Row 7:  [ Read Lref (tap) / Cancel (hold)                        ]
```

See `research/metering-firmware-ux.md` section 2.5 for full JSON button config.

### 3. Integration

| File | Change |
|------|--------|
| `src/app/app.ino` | Add `paper_cal_init()` in setup, `paper_cal_loop()` in loop |
| `src/app/display_task.cpp` | Add `paper_cal_tick()` call |
| `src/app/action_dispatch.cpp` | Replace cal stub with `paper_cal_dispatch(cmd)` |

---

## Phase 1b — User runs existing Strip pad

After Phase 1a, the user navigates to the existing Strip pad for the bare-bulb test strip.
No new firmware needed for Phase 1b. The following are documented as **instructions shown
on the Cal pad screen:**

**Ideal bare-bulb test strip settings:**

| Setting | Value | Rationale |
|---------|-------|-----------|
| Base time | 5.0s | Zone V typically falls in 3–8s range under bare bulb |
| Segments | 7 | Default, covers ±1 stop at ⅓-stop steps |
| Step | ⅓ stop | Default, fine enough for Zone V identification |
| Range | 2.5s – 10.1s | Brackets Zone V for most enlarger setups |

The user identifies the medium grey segment, notes its cumulative time (e.g., 6.3s) —
this is the **Zone V time** for use in Phase 2 (PRD 03).

---

## Out of Scope

- Meter flow (PRD 03)
- Pre-configured bare-bulb Strip pad (PRD 04)
- NVS persistence for Lref (v2)
- Suggested base time calculation (dropped — user follows on-screen instructions)
