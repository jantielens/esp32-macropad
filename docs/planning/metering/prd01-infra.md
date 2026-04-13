# PRD 01 — Infrastructure (TSL2591 Driver + Shared Memory)

**Feature:** Metering Infrastructure  
**Depends on:** Nothing  
**Unlocks:** PRD 02, PRD 03  
**Estimated scope:** ~200 lines, 4 new files, 5 modified files

---

## Goal

Lay the foundation for all metering features: a working TSL2591 sensor driver and a
shared memory binding for cross-phase data flow. After this PRD, the sensor can be read
programmatically and values can be stored/retrieved via binding tokens.

---

## Deliverables

### 1. TSL2591 Sensor Driver

**New files:** `src/app/sensors/tsl2591_sensor.h`, `src/app/sensors/tsl2591_sensor.cpp`

**API:**

```c
bool  tsl2591_init();           // Init sensor on I2C bus. Returns true if detected.
float tsl2591_read_lux();       // Blocking read (~100-600ms). Returns lux or -1.0f.
bool  tsl2591_is_connected();   // Check if sensor responds.
```

**Requirements:**
- I2C address 0x29 (TSL2591 default)
- Use existing `i2c_bus_lock()` / `i2c_bus_unlock()` mutex for thread safety
- Medium gain, 100ms integration time as defaults (tunable later)
- Reads are blocking — callers must run from `loop()` context (main task), not LVGL task
- Register in `sensors.cpp` via `sensor_manager_register_all()`
- Compile-gated by `#if IS_DARKROOM_TIMER`

**Test criteria:**
- Sensor detected on boot (log message)
- Raw lux value readable via serial monitor
- Reads return sensible values (0–88,000 lux range)
- Graceful -1.0f return when sensor disconnected

### 2. Shared Memory Binding

**New files:** `src/app/shared_mem.h`, `src/app/shared_mem.cpp`

**API:**

```c
void  shared_mem_init();                      // Register "mem" binding scheme
void  shared_mem_dispatch(const char* cmd);   // Handle set commands
float shared_mem_get(const char* key);        // Read value (0.0f if not set)
void  shared_mem_set(const char* key, float value);  // Write value
```

**Binding scheme:** `"mem"`

**Tokens:** `[mem:<key>]` — returns formatted float or `"---"` if key not set.

**Action commands:** `set_<key>:<value>` — e.g., `set_lref:1847.3`

**Requirements:**
- Static array of key-value pairs (key = char[16], value = float), max 8 entries
- RAM-only, no NVS persistence
- Thread-safe (called from LVGL task via resolver, from main loop via dispatch)
- Register in `app.ino` init sequence
- No `tick()` or `loop()` needed — pure data store
- Compile-gated by `#if IS_DARKROOM_TIMER`

**Test criteria:**
- `shared_mem_set("lref", 1847.3)` → `shared_mem_get("lref")` returns 1847.3
- `[mem:lref]` resolves to `"1847.3"` in a button label
- `[mem:undefined_key]` resolves to `"---"`

### 3. Integration Scaffolding

**Modified files:**

| File | Change |
|------|--------|
| `src/app/pad_config.h` | Add `ACTION_TYPE_CAL "cal"`, `ACTION_TYPE_METER "meter"`, `ACTION_TYPE_MEM "mem"` |
| `src/app/action_dispatch.cpp` | Add dispatch cases for all three new action types (stub: log + forward to dispatch fn) |
| `src/app/action_parse.cpp` | Add `cal_command`, `meter_command`, `mem_command` JSON serialization |
| `src/app/app.ino` | Add `#include` + `shared_mem_init()` in setup, TSL2591 init in sensor registration |
| `src/app/display_task.cpp` | Add placeholder `// paper_cal_tick()` and `// meter_tick()` comments for PRD 02/03 |

**Requirements:**
- Action types registered but cal/meter dispatch functions are stubs (log warning "not implemented")
- Shared memory dispatch is fully functional
- All new code gated by `#if IS_DARKROOM_TIMER`
- Existing functionality unchanged — no regressions

**Test criteria:**
- Firmware compiles and boots cleanly
- Existing expose/strip pads work unchanged
- `{"type": "mem", "mem_command": "set_lref:1847.3"}` button action works
- `[mem:lref]` displays on a button label
- `{"type": "cal", "cal_command": "start"}` logs "not implemented" (stub)

---

## Out of Scope

- Paper calibration flow (PRD 02)
- Metering flow (PRD 03)
- Pad configurations for Cal/Meter screens (PRD 02/03)
- NVS persistence for shared memory (v2)
