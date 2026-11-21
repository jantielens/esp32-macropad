---
title: Display, touch, audio, and RTC drivers
description: Driver ownership, compile-time selection, and integration with board initialization.
---

This folder contains reusable **display**, **touch**, **audio**, and **RTC** driver
implementations. Boards select the implementations they need at compile time.

We intentionally keep this folder **flat** (no `display_*/touch_*` filename prefixes and no `display/` or `touch/` subfolders) to keep includes simple and minimize churn.

Board-exclusive drivers belong in `src/boards/<board>/drivers/`. Group a
driver and its supporting files in a subdirectory when useful; keep board-wide
initialization in the board hardware module. Board variants may share an
existing base board's drivers. Reusable controller drivers and shared
protocol/rendering helpers remain central, even when only one board family
currently selects them. A board-specific filename alone does not establish
exclusivity.

The CrowPanel and Inkplate interactive adapters are board-local. The Tab5
display and touch adapters are owned by the base Tab5 board and used by its SD
variant. The E1003 frame implementation is board-local; the Inkplate frame
implementation remains shared across Inkplate 5 V2 and 6FLICK. The Tab5 panel
command license is retained with that table and with the shared ES8388 codec
implementation.

## Where driver selection lives

Board selection is done at compile time in:
- `src/boards/<board>/board_overrides.h` (see **Driver Selection (HAL)**)
- Defaults live in `src/app/board_config.h`

Only the selected driver implementations are compiled via the sketch-root translation units:
- `src/app/display_drivers.cpp`
- `src/app/touch_drivers.cpp`
- `src/app/audio_output_drivers.cpp`
- `src/app/audio_input_drivers.cpp` (for independently selected input drivers)
- `src/app/rtc_drivers.cpp`
- `src/app/device_classes/epaper_frame/epaper_frame_drivers.cpp` (included by
  the sketch-root device-class compilation unit)

Board-local `.cpp` files also require the appropriate explicit sketch-root
include. Removing a board directory does not remove its integration references.

## RTC and time ownership

`HAS_RTC` defaults to false. An enabled board selects `RTC_DRIVER`; the
sketch-root RTC aggregator includes only that controller implementation.
`RtcDriver` exposes initialization, UTC reads, and UTC writes. The selected
instance has static lifetime and is not heap-allocated. The PCF8563 driver uses
`RTC_I2C_ADDR` on the board-initialized Wire0 bus with the shared I2C mutex;
it does not initialize the bus or manage board power.

Call `time_service_init_rtc()` during setup after successful board initialization
and before starting NTP. The time service restores validated RTC time to the
system clock and treats RTC-restored time as ready. NTP callbacks only flag a
pending write; the Arduino loop writes current system UTC after OTA or power-off
activity ends. A failed write is logged and retried on the next NTP sync, not
polled. Boards without RTC retain the NTP-only path.

`HAS_RTC` with `IS_EPAPER_FRAME` is rejected at compile time. Enabling it
requires an explicit checkpoint that processes pending RTC writes after NTP
synchronization and before deep sleep, rather than relying on the normal
Arduino loop.

PCF8563 uses century=0 for 2000-2099 and rejects century=1, stopped/test modes,
voltage-loss flags, malformed BCD, and invalid calendar dates. The time service
also rejects dates before 2024. Writes stop the clock, transfer the calendar,
and restart it only after success; interrupted writes leave time invalid.
Other controllers implement the same UTC interface and their own validity
checks. Alarm interrupts and manual time-setting controls are not implemented.

## Generated board → drivers table

To make it easy to see which boards use which backends/controllers, we generate a table from the board override headers.

Regenerate this table:

```bash
python3 tools/generate-board-driver-table.py --update-drivers-readme
```

<!-- BOARD_DRIVER_TABLE_START -->

| Board | Display backend | Panel | Bus | Res | Rot | Touch backend | Notes |
|---|---|---|---:|---:|---:|---|---|
| crowpanel-4.2-interactive | CROWPANEL_42 | SSD1683 | SPI | 400×300 | 0 | none |  |
| cyd-v2 | TFT_ESPI | ILI9341 | SPI | 320×240 | 1 | XPT2046 | inversion on, gamma fix |
| esp32-4848S040 | ST7701_RGB | ST7701 | SPI | 480×480 | 0 | GT911 |  |
| esp32-p4-lcd4b | ST7703_DSI | ST7703 | DSI | 720×720 | 0 | GT911 |  |
| esp32-p4-lcd4b-sd | ST7703_DSI | ST7703 | DSI | 720×720 | 0 | GT911 |  |
| esp32-p4-lcd4b-voice | ST7703_DSI | ST7703 | DSI | 720×720 | 0 | GT911 |  |
| esp32c3-withsensors | ? | ? | ? | ?×? | ? | none |  |
| firebeetle2-esp32c6-aht10 | ? | ? | ? | ?×? | ? | none |  |
| inkplate5v2-frame | ? | ? | ? | ?×? | ? | none |  |
| inkplate6flick-frame | ? | ? | ? | ?×? | ? | none |  |
| inkplate6flick-interactive | INKPLATE6FLICK | ? | ? | 1024×758 | 0 | INKPLATE6FLICK |  |
| jc1060p470c | JD9165_DSI | JD9165 | DSI | 1024×600 | 1 | GT911 |  |
| jc1060p470c-sd | JD9165_DSI | JD9165 | DSI | 1024×600 | 1 | GT911 |  |
| jc3248w535 | ARDUINO_GFX | AXS15231B | QSPI | 320×480 | 1 | AXS15231B |  |
| jc3636w518 | ARDUINO_GFX_ST77916 | ST77916 | QSPI | 360×360 | 0 | CST816S |  |
| jc3636w518-sd | ARDUINO_GFX_ST77916 | ST77916 | QSPI | 360×360 | 0 | CST816S |  |
| jc3636w518-sd-ble | ARDUINO_GFX_ST77916 | ST77916 | QSPI | 360×360 | 0 | CST816S |  |
| jc3636w518-sd-usb | ARDUINO_GFX_ST77916 | ST77916 | QSPI | 360×360 | 0 | CST816S |  |
| jc4827w543c | ARDUINO_GFX_NV3041A | NV3041A | QSPI | 480×272 | 0 | GT911 | hardware validated by @00lems00 (#81) |
| jc4880p433 | ST7701_DSI | ST7701 | DSI | 480×800 | 0 | GT911 |  |
| jc4880p433-darkroom | ST7701_DSI | ST7701 | DSI | 480×800 | 0 | GT911 |  |
| jc4880p433-hx711 | ST7701_DSI | ST7701 | DSI | 480×800 | 0 | GT911 |  |
| jc4880p433-nau7802 | ST7701_DSI | ST7701 | DSI | 480×800 | 0 | GT911 |  |
| jc4880p433-sd | ST7701_DSI | ST7701 | DSI | 480×800 | 0 | GT911 |  |
| jc4880p433-shutter | ST7701_DSI | ST7701 | DSI | 480×800 | 0 | GT911 |  |
| m5stack-stopwatch | ARDUINO_GFX_CO5300 | CO5300 | QSPI | 466×466 | 0 | CST820B |  |
| m5stack-tab5 | TAB5_DSI | ST7121/ST7123 | DSI | 720×1280 | 0 | ST7123 |  |
| m5stack-tab5-sd | TAB5_DSI | ST7121/ST7123 | DSI | 720×1280 | 0 | ST7123 |  |
| reterminal-e1003-frame | ? | ? | ? | ?×? | ? | none |  |
| reterminal-e1003-interactive | RETERMINAL_E1003 | IT8951 | SPI | 1872×1404 | 1 | GT911 |  |
| reterminal-sticky-interactive | STICKY_EPAPER | SSD1677/SSD2677 | SPI | 800×480 | 0 | GT911 |  |
| reterminal-sticky-interactive-sd | STICKY_EPAPER | SSD1677/SSD2677 | SPI | 800×480 | 0 | GT911 |  |
| waveshare-7.8-interactive | IT8951 | IT8951 | SPI | 1872×1404 | 2 | none |  |

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
