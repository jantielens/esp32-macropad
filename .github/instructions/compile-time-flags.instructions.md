---
description: "Compile-time flag conventions — HAS_* feature gates, driver selectors, board overrides, and conditional compilation patterns"
applyTo: "**/board_config.h, **/board_overrides.h, **/display_drivers.cpp, **/touch_drivers.cpp, **/widgets.cpp, **/screens.cpp, **/custom_fonts.cpp"
---

# Compile-Time Flag Conventions

## Flag Categories

| Category | Pattern | Example |
|---|---|---|
| Feature gates | `HAS_*` | `HAS_DISPLAY`, `HAS_MQTT`, `HAS_AUDIO` |
| Driver selectors | `*_DRIVER` | `DISPLAY_DRIVER`, `TOUCH_DRIVER` |
| Hardware geometry | `DISPLAY_*` | `DISPLAY_WIDTH`, `DISPLAY_HEIGHT`, `DISPLAY_ROTATION` |
| Hardware pins | `*_PIN` | `LCD_BL_PIN`, `TOUCH_INT`, `AUDIO_I2S_BCLK` |
| Limits & tuning | `MAX_*`, `*_MS`, `*_HZ` | `MAX_PADS`, `LVGL_TICK_PERIOD_MS` |

## Defaults and Overrides

- **Defaults**: `src/app/board_config.h` defines all defaults for every flag.
- **Per-board overrides**: `src/boards/[board-name]/board_overrides.h` overrides specific flags. Included before `board_config.h` so that `#ifndef` guards in `board_config.h` pick up the override.
- **Derived flags**: Some flags derive from others (e.g., `HAS_CUSTOM_FONTS` defaults to `HAS_DISPLAY`, `HAS_SOUND_PLAYER` defaults to `HAS_AUDIO`).

## Conditional Compilation Patterns

Use `#if HAS_xxx` (not `#ifdef`) for feature gates — all `HAS_*` flags are defined as `true` or `false`:

```cpp
#if HAS_DISPLAY
  // Display-dependent code
#endif
```

When a module is entirely gated by a feature flag, wrap the entire `.cpp` body:

```cpp
#include "board_config.h"
#if HAS_MY_FEATURE
// ... entire implementation ...
#endif
```

## Compilation Unit Pattern

Arduino does not auto-compile `.cpp` files in subdirectories. The project uses sketch-root compilation units that conditionally `#include` the selected implementation:

- `display_drivers.cpp` — includes the one display driver `.cpp` selected by `DISPLAY_DRIVER`
- `touch_drivers.cpp` — includes the one touch driver `.cpp` selected by `TOUCH_DRIVER`
- `widgets.cpp` — includes all widget `.cpp` files
- `screens.cpp` — includes all screen `.cpp` files
- `custom_fonts.cpp` — includes all generated font `.c` files

When adding a new driver or widget, add its `#include` to the corresponding compilation unit — do not place it in a manager file.

## Adding a New Feature Flag

1. Add default in `board_config.h` with `#ifndef` guard and descriptive comment
2. Override in relevant `board_overrides.h` files
3. Gate all related code with `#if HAS_xxx`
4. Update `docs/compile-time-flags.md` by running: `python3 tools/compile_flags_report.py md --out docs/compile-time-flags.md`

## Reference

Full auto-generated flag reference: `docs/compile-time-flags.md` (155 flags across all categories).
