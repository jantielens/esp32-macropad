---
description: "Display and touch driver HAL conventions — driver selection, compilation units, buffered vs direct rendering, and board overrides"
applyTo: "**/drivers/*, **/display_driver*, **/display_manager*, **/touch_driver*, **/touch_manager*, **/display_drivers.cpp, **/touch_drivers.cpp, **/board_config.h, **/board_overrides.h"
---

# Display/Touch Driver Conventions

- **Single source of defaults**: default `DISPLAY_DRIVER` / `TOUCH_DRIVER` live in `src/app/board_config.h`.
- **Per-board selection**: each board override should have a clear **Driver Selection (HAL)** block that explicitly sets the HAL selectors:
  - `#define DISPLAY_DRIVER DISPLAY_DRIVER_...`
  - `#define TOUCH_DRIVER TOUCH_DRIVER_...` (or `#define HAS_TOUCH false`)
- **Direct vs Buffered**:
  - Direct drivers push pixels during the LVGL flush callback.
  - Buffered drivers return `renderMode() == Buffered` and implement `present()`.
- **Arduino build limitation**: do not include driver `.cpp` files in manager files; add conditional includes to `src/app/display_drivers.cpp` or `src/app/touch_drivers.cpp` instead.
- **Board→driver visibility**: after editing board overrides, regenerate the table in `src/app/drivers/README.md`:
  - `python3 tools/generate-board-driver-table.py --update-drivers-readme`
- **Vendored code placement**: third-party source that is not an Arduino library should live under the driver that uses it (driver-scoped vendor code), not in a shared `drivers/vendor/` bucket.

## Driver Implementations

| Category | Drivers |
|---|---|
| Display | TFT_eSPI, Arduino_GFX, ST77916, ST7701_RGB, MIPI-DSI base, ST7703_DSI, ST7701_DSI, JD9165_DSI |
| Touch | XPT2046, AXS15231B, CST816S, GT911 |

## Key Files

- `display_driver.h` — DisplayDriver HAL interface (`RenderMode`, `present()`, `configureLVGL()`)
- `display_manager.cpp/h` — Hardware lifecycle, LVGL init, FreeRTOS rendering task
- `touch_driver.h` — TouchDriver HAL interface
- `touch_manager.cpp/h` — Touch input registration and calibration
- `display_drivers.cpp` — Sketch-root compilation unit that conditionally includes exactly one display driver `.cpp`
- `touch_drivers.cpp` — Sketch-root compilation unit that conditionally includes exactly one touch driver `.cpp`
- `screens/screen.h` — Screen base class interface
- `drivers/README.md` — Driver selection conventions + generated board→drivers table
