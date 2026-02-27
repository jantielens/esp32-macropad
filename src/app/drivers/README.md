# Drivers (Display + Touch)

This folder contains reusable **display** and **touch** driver implementations used by multiple boards.

We intentionally keep this folder **flat** (no `display_*/touch_*` filename prefixes and no `display/` or `touch/` subfolders) to keep includes simple and minimize churn.

## Where driver selection lives

Board selection is done at compile time in:
- `src/boards/<board>/board_overrides.h` (see **Driver Selection (HAL)**)
- Defaults live in `src/app/board_config.h`

Only the selected driver implementations are compiled via the sketch-root translation units:
- `src/app/display_drivers.cpp`
- `src/app/touch_drivers.cpp`

## Generated board → drivers table

To make it easy to see which boards use which backends/controllers, we generate a table from the board override headers.

Regenerate this table:

```bash
python3 tools/generate-board-driver-table.py --update-drivers-readme
```

<!-- BOARD_DRIVER_TABLE_START -->

| Board | Display backend | Panel | Bus | Res | Rot | Touch backend | Notes |
|---|---|---|---:|---:|---:|---|---|
| cyd-v2 | TFT_ESPI | ILI9341 | SPI | 320×240 | 1 | XPT2046 | inversion on, gamma fix |
| esp32-4848S040 | ST7701_RGB | ST7701 | SPI | 480×480 | 0 | GT911 |  |
| esp32-p4-lcd4b | ST7703_DSI | ST7703 | DSI | 720×720 | 0 | GT911 |  |
| esp32c3-withsensors | ? | ? | ? | ?×? | ? | ? |  |
| jc3248w535 | ARDUINO_GFX | AXS15231B | QSPI | 320×480 | 1 | AXS15231B |  |
| jc3636w518 | ARDUINO_GFX_ST77916 | ST77916 | QSPI | 360×360 | 0 | CST816S |  |
| jc4880p433 | ST7701_DSI | ST7701 | DSI | 480×800 | 0 | GT911 |  |

<!-- BOARD_DRIVER_TABLE_END -->

## Conventions

- **Display backend (HAL)** is selected with `DISPLAY_DRIVER` (e.g., `DISPLAY_DRIVER_TFT_ESPI`, `DISPLAY_DRIVER_ARDUINO_GFX`).
- **Touch backend (HAL)** is selected with `TOUCH_DRIVER` (e.g., `TOUCH_DRIVER_XPT2046`, `TOUCH_DRIVER_AXS15231B`).
- TFT_eSPI-specific controller macros (like `DISPLAY_DRIVER_ILI9341_2`) are *controller/config flags*, not the HAL backend selector.

### Naming

Driver filenames are named by the "primary identity" of the implementation:

- **Library-backend wrappers** are named after the library/backend (because they can support many panels/controllers via configuration):
	- Examples: `tft_espi_driver.*`, `arduino_gfx_driver.*`
- **Chip/controller-specific drivers** are named after the controller/panel (because they are tightly coupled to that hardware):
	- Examples: `xpt2046_driver.*`, `axs15231b_touch_driver.*`
- **Hybrid implementations** may include both backend + panel/controller when the implementation is intentionally panel-specific within a backend:
	- Example: `arduino_gfx_st77916_driver.*`

### Vendored (3rd-party) code

If a driver depends on third-party source files that are not installed as an Arduino library, those files live **under the driver that uses them** (driver-scoped vendor code), not in a shared "vendor" bucket.

- Example: AXS15231B touch vendor files live under `drivers/axs15231b/vendor/`.
