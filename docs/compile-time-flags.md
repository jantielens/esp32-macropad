# Compile-Time Flags Report

This document is a template. Sections marked with `COMPILE_FLAG_REPORT` markers are auto-updated by `tools/compile_flags_report.py`.

## How to update

- Update this doc:
  - `python3 tools/compile_flags_report.py md --out docs/compile-time-flags.md`
- Print active flags during a build (example):
  - `python3 tools/compile_flags_report.py build --board cyd-v2`

## Build system notes

- Some Arduino libraries are compiled as separate translation units and may not see macros that are only defined in `src/boards/<board>/board_overrides.h`.
- The build script propagates a small allowlist of board override defines into global compiler flags so libraries are compiled with the same values.
  - Currently allowlisted:
    - `CONFIG_ASYNC_TCP_STACK_SIZE`
    - NimBLE tuning (roles + limits + log levels)
    - TFT_eSPI essentials for clean/CI builds (pins + SPI frequencies + controller/bus flags)
  - For TFT_eSPI specifically, `build.sh` also supports a per-board `src/boards/<board>/User_Setup.h` which is force-included for that board (so the build does not depend on a locally modified Arduino library install).

## Flags (generated)

<!-- BEGIN COMPILE_FLAG_REPORT:FLAGS -->
Total flags: 160

### Features (HAS_*)

- **HAS_BACKLIGHT** default: `false` — Enable backlight control (typically via PWM).
- **HAS_BLE** default: `false` — Enable BLE (NimBLE) advertising support.
- **HAS_BUILTIN_LED** default: `false` — Enable built-in status LED support.
- **HAS_BUTTON** default: `false` — User Button (optional)
- **HAS_DISPLAY** default: `false` — Enable display + LVGL UI support.
- **HAS_MQTT** default: `true` — Enable MQTT and Home Assistant integration.
- **HAS_SENSOR_BME280** default: `false` — Enable BME280 (I2C) environmental sensor adapter.
- **HAS_SENSOR_DUMMY** default: `false` — Enable dummy sensor adapter (synthetic values for testing).
- **HAS_SENSOR_LD2410_OUT** default: `false` — Enable LD2410 OUT pin presence sensor adapter.
- **HAS_TOUCH** default: `false` — Enable touch input support.

### Selectors (*_DRIVER)

- **DISPLAY_DRIVER** default: `DISPLAY_DRIVER_TFT_ESPI` (values: DISPLAY_DRIVER_ARDUINO_GFX, DISPLAY_DRIVER_ARDUINO_GFX_ST77916, DISPLAY_DRIVER_ST7701_DSI, DISPLAY_DRIVER_ST7701_RGB, DISPLAY_DRIVER_ST7703_DSI, DISPLAY_DRIVER_TFT_ESPI) — Select the display HAL backend (one of the DISPLAY_DRIVER_* constants).
- **ILI9341_2_DRIVER** default: `(no default)` — These macros are consumed by the TFT_eSPI library itself.
- **TOUCH_DRIVER** default: `TOUCH_DRIVER_XPT2046` (values: TOUCH_DRIVER_AXS15231B_I2C, TOUCH_DRIVER_CST816S_WIRE, TOUCH_DRIVER_GT911, TOUCH_DRIVER_XPT2046) — Select the touch HAL backend (one of the TOUCH_DRIVER_* constants).

### Hardware (Geometry)

- **DISPLAY_HEIGHT** default: `(no default)` — Panel height in pixels.
- **DISPLAY_ROTATION** default: `(no default)` — UI rotation (LVGL).
- **DISPLAY_WIDTH** default: `(no default)` — Panel width in pixels.

### Hardware (Pins)

- **BUTTON_PIN** default: `0` — GPIO pin for the optional user button (active level defined below).
- **LCD_B0_PIN** default: `(no default)` — RGB Blue 0 pin.
- **LCD_B1_PIN** default: `(no default)` — RGB Blue 1 pin.
- **LCD_B2_PIN** default: `(no default)` — RGB Blue 2 pin.
- **LCD_B3_PIN** default: `(no default)` — RGB Blue 3 pin.
- **LCD_B4_PIN** default: `(no default)` — RGB Blue 4 pin.
- **LCD_BL_PIN** default: `(no default)` — Backlight control pin.
- **LCD_CS_PIN** default: `(no default)` — SPI CS pin (ST7701 commands).
- **LCD_DE_PIN** default: `(no default)` — RGB DE pin.
- **LCD_G0_PIN** default: `(no default)` — RGB Green 0 pin.
- **LCD_G1_PIN** default: `(no default)` — RGB Green 1 pin.
- **LCD_G2_PIN** default: `(no default)` — RGB Green 2 pin.
- **LCD_G3_PIN** default: `(no default)` — RGB Green 3 pin.
- **LCD_G4_PIN** default: `(no default)` — RGB Green 4 pin.
- **LCD_G5_PIN** default: `(no default)` — RGB Green 5 pin.
- **LCD_HSYNC_PIN** default: `(no default)` — RGB HSYNC pin.
- **LCD_MOSI_PIN** default: `(no default)` — SPI MOSI pin (ST7701 commands).
- **LCD_PCLK_PIN** default: `(no default)` — RGB PCLK pin.
- **LCD_QSPI_CS** default: `(no default)` — QSPI chip select pin.
- **LCD_QSPI_D0** default: `(no default)` — QSPI data line 0 pin.
- **LCD_QSPI_D1** default: `(no default)` — QSPI data line 1 pin.
- **LCD_QSPI_D2** default: `(no default)` — QSPI data line 2 pin.
- **LCD_QSPI_D3** default: `(no default)` — QSPI data line 3 pin.
- **LCD_QSPI_PCLK** default: `(no default)` — QSPI pixel clock pin.
- **LCD_QSPI_RST** default: `(no default)` — QSPI reset pin (-1 = none).
- **LCD_QSPI_TE** default: `(no default)` — Panel TE pin.
- **LCD_R0_PIN** default: `(no default)` — RGB Red 0 pin.
- **LCD_R1_PIN** default: `(no default)` — RGB Red 1 pin.
- **LCD_R2_PIN** default: `(no default)` — RGB Red 2 pin.
- **LCD_R3_PIN** default: `(no default)` — RGB Red 3 pin.
- **LCD_R4_PIN** default: `(no default)` — RGB Red 4 pin.
- **LCD_RST_PIN** default: `(no default)` — Panel Reset
- **LCD_SCK_PIN** default: `(no default)` — SPI SCK pin (ST7701 commands).
- **LCD_VSYNC_PIN** default: `(no default)` — RGB VSYNC pin.
- **LD2410_OUT_PIN** default: `-1` — LD2410 OUT pin (presence). Use -1 to disable.
- **LED_PIN** default: `2` — GPIO for the built-in LED (only used when HAS_BUILTIN_LED is true).
- **SENSOR_I2C_SCL** default: `-1` — I2C SCL pin for sensors.
- **SENSOR_I2C_SDA** default: `-1` — I2C pins for sensors. Use -1 to keep default Wire pins.
- **TFT_BL** default: `(no default)` — TFT_eSPI: backlight pin.
- **TFT_CS** default: `(no default)` — TFT_eSPI: CS pin.
- **TFT_DC** default: `(no default)` — TFT_eSPI: DC pin.
- **TFT_MISO** default: `(no default)` — TFT_eSPI: MISO pin.
- **TFT_MOSI** default: `(no default)` — TFT_eSPI: MOSI pin.
- **TFT_RST** default: `(no default)` — TFT_eSPI: RST pin (-1 = none).
- **TFT_SCLK** default: `(no default)` — TFT_eSPI: SCLK pin.
- **TOUCH_CS** default: `(no default)` — TFT_eSPI touch: CS pin.
- **TOUCH_I2C_SCL** default: `(no default)` — Touch I2C SCL pin.
- **TOUCH_I2C_SDA** default: `(no default)` — Touch I2C SDA pin.
- **TOUCH_INT** default: `(no default)` — Touch interrupt pin (-1 = not connected).
- **TOUCH_IRQ** default: `(no default)` — TFT_eSPI touch: IRQ pin (optional).
- **TOUCH_MISO** default: `(no default)` — TFT_eSPI touch: MISO pin.
- **TOUCH_MOSI** default: `(no default)` — TFT_eSPI touch: MOSI pin.
- **TOUCH_RST** default: `-1` — Touch reset pin (-1 = no hardware reset, GT911 boots normally).
- **TOUCH_SCLK** default: `(no default)` — TFT_eSPI touch: SCLK pin.
- **XPT2046_CLK** default: `(no default)` — XPT2046 CLK pin.
- **XPT2046_CS** default: `(no default)` — XPT2046 CS pin.
- **XPT2046_IRQ** default: `(no default)` — XPT2046 IRQ pin.
- **XPT2046_MISO** default: `(no default)` — XPT2046 MISO pin.
- **XPT2046_MOSI** default: `(no default)` — XPT2046 MOSI pin.

### Limits & Tuning

- **CONFIG_BT_NIMBLE_MAX_BONDS** default: `(no default)` — NimBLE max bonded devices (tuning for small footprint)
- **CONFIG_BT_NIMBLE_MAX_CCCDS** default: `(no default)` — NimBLE max CCCDs
- **CONFIG_BT_NIMBLE_MAX_CONNECTIONS** default: `(no default)` — NimBLE max connections
- **HEALTH_HISTORY_PERIOD_MS** default: `5000` — Sampling cadence for the device-side history (ms). Default aligns with UI poll.
- **LVGL_BUFFER_PREFER_INTERNAL** default: `false` — Prefer internal RAM over PSRAM for LVGL draw buffer allocation.
- **LVGL_BUFFER_SIZE** default: `(DISPLAY_WIDTH * 10)` — LVGL draw buffer size in pixels (larger = faster, more RAM).
- **LVGL_REFR_PERIOD_MS** default: `(no default)` — Default LVGL 8.4 is 30 ms (~33 fps). Panel hardware supports ~59 fps.
- **LVGL_TICK_PERIOD_MS** default: `5` — LVGL tick period in milliseconds.
- **MEMORY_TRIPWIRE_INTERNAL_MIN_BYTES** default: `0` — Default: disabled (0). Enable per-board if you want early warning logs.
- **SENSOR_I2C_FREQUENCY** default: `400000` — I2C clock for sensors (Hz).
- **SPI_FREQUENCY** default: `(no default)` — TFT_eSPI: SPI write frequency (Hz).
- **SPI_READ_FREQUENCY** default: `(no default)` — TFT_eSPI: SPI read frequency (Hz).
- **SPI_TOUCH_FREQUENCY** default: `(no default)` — TFT_eSPI: SPI touch frequency (Hz).
- **ST7701_DSI_DPI_CLK_HZ** default: `34000000L` — DPI pixel clock in Hz.
- **ST7703_DPI_CLK_HZ** default: `38000000L` — DPI pixel clock in Hz for ST7703 MIPI-DSI panels (ESP32-P4 only).
- **TFT_BACKLIGHT_PWM_FREQ** default: `1000` — Lower frequencies give wider dimming range but may cause audible coil whine.
- **TFT_SPI_FREQUENCY** default: `(no default)` — TFT SPI clock frequency.
- **TFT_SPI_FREQ_HZ** default: `(no default)` — QSPI clock frequency (Hz).
- **TOUCH_I2C_FREQ_HZ** default: `(no default)` — I2C frequency (Hz).
- **WEB_PORTAL_CONFIG_BODY_TIMEOUT_MS** default: `5000` — Timeout for an incomplete /api/config upload (ms) before freeing the buffer.
- **WEB_PORTAL_CONFIG_MAX_JSON_BYTES** default: `4096` — Max JSON body size accepted by /api/config.
- **WIFI_MAX_ATTEMPTS** default: `3` — Maximum WiFi connection attempts at boot before falling back.

### Other

- **BME280_I2C_ADDR** default: `0x76` — BME280 I2C address (0x76 or 0x77).
- **BUTTON_ACTIVE_LOW** default: `true` — Button polarity: true when pressed = LOW.
- **CONFIG_BT_NIMBLE_LOG_LEVEL** default: `(no default)` — NimBLE host log level
- **CONFIG_BT_NIMBLE_MSYS1_BLOCK_COUNT** default: `(no default)` — NimBLE msys1 block count
- **CONFIG_BT_NIMBLE_ROLE_BROADCASTER** default: `(no default)` — NimBLE role: broadcaster
- **CONFIG_BT_NIMBLE_ROLE_CENTRAL** default: `(no default)` — NimBLE role: central
- **CONFIG_BT_NIMBLE_ROLE_OBSERVER** default: `(no default)` — NimBLE role: observer
- **CONFIG_BT_NIMBLE_ROLE_PERIPHERAL** default: `(no default)` — NimBLE role: peripheral (required)
- **CONFIG_NIMBLE_CPP_LOG_LEVEL** default: `(no default)` — NimBLE C++ wrapper log level
- **DEVICE_TELEMETRY_BACKGROUND_TASKS** default: `1` — point-in-time values without min/max window bands or CPU %.
- **DISPLAY_COLOR_ORDER_BGR** default: `(no default)` — Panel uses BGR byte order.
- **DISPLAY_DRIVER_ILI9341_2** default: `(no default)` — Use the ILI9341_2 controller setup in TFT_eSPI.
- **DISPLAY_INVERSION_ON** default: `(no default)` — Enable display inversion (panel-specific).
- **DISPLAY_NEEDS_GAMMA_FIX** default: `(no default)` — Apply gamma correction fix for this panel variant.
- **DISPLAY_PANEL** default: `(no default)` — Panel IC name string (used by tools/generate-board-driver-table.py for the board→driver table).
- **HEALTH_HISTORY_ENABLED** default: `1` — Enable device-side health history ring buffer for charting in the web portal
- **HEALTH_HISTORY_SAMPLES** default: `((HEALTH_HISTORY_SECONDS * 1000) / HEALTH_HISTORY_PERIOD_MS)` — Derived number of samples.
- **HEALTH_HISTORY_SECONDS** default: `300` — How much client-side history (sparklines) to keep.
- **HEALTH_POLL_INTERVAL_MS** default: `5000` — How often the web UI polls /api/health.
- **LCD_HSYNC_BACK_PORCH** default: `(no default)` — HSYNC back porch.
- **LCD_HSYNC_FRONT_PORCH** default: `(no default)` — HSYNC front porch.
- **LCD_HSYNC_POLARITY** default: `(no default)` — HSYNC polarity (1 = active high).
- **LCD_HSYNC_PULSE_WIDTH** default: `(no default)` — HSYNC pulse width.
- **LCD_QSPI_HOST** default: `(no default)` — QSPI host peripheral.
- **LCD_VSYNC_BACK_PORCH** default: `(no default)` — VSYNC back porch.
- **LCD_VSYNC_FRONT_PORCH** default: `(no default)` — VSYNC front porch.
- **LCD_VSYNC_POLARITY** default: `(no default)` — VSYNC polarity (1 = active high).
- **LCD_VSYNC_PULSE_WIDTH** default: `(no default)` — VSYNC pulse width.
- **LD2410_OUT_DEBOUNCE_MS** default: `50` — Debounce for LD2410 OUT edge changes (ms).
- **LED_ACTIVE_HIGH** default: `true` — LED polarity: true if HIGH turns the LED on.
- **LVGL_TASK_CORE** default: `0` — Core to pin the LVGL render task to on dual-core chips (0 or 1).
- **LVGL_TASK_PRIORITY** default: `4` — Default 4 matches ESP-IDF BSP convention; keeps rendering above WiFi (pri 2-3).
- **LV_USE_PERF_MONITOR_POS** default: `(no default)` — LVGL perf monitor alignment.
- **MEMORY_TRIPWIRE_CHECK_INTERVAL_MS** default: `5000` — How often to check tripwires from the main loop.
- **POWERON_CONFIG_BURST_ENABLED** default: `false` — Intended for boards WITHOUT a reliable user button.
- **PROJECT_DISPLAY_NAME** default: `"ESP32 Device"` — Human-friendly project name used in the web UI and device name (can be set by build system).
- **ST7701_DSI_HSYNC_BACK_PORCH** default: `42` — HSYNC back porch in pixel clocks.
- **ST7701_DSI_HSYNC_FRONT_PORCH** default: `42` — HSYNC front porch in pixel clocks.
- **ST7701_DSI_HSYNC_PULSE_WIDTH** default: `12` — HSYNC pulse width in pixel clocks.
- **ST7701_DSI_LANE_BIT_RATE** default: `500` — MIPI-DSI lane bit rate in Mbps.
- **ST7701_DSI_VSYNC_BACK_PORCH** default: `8` — VSYNC back porch in lines.
- **ST7701_DSI_VSYNC_FRONT_PORCH** default: `166` — VSYNC front porch in lines.
- **ST7701_DSI_VSYNC_PULSE_WIDTH** default: `2` — VSYNC pulse width in lines.
- **ST7703_HSYNC_BACK_PORCH** default: `50` — HSYNC back porch in pixel clocks.
- **ST7703_HSYNC_FRONT_PORCH** default: `50` — HSYNC front porch in pixel clocks.
- **ST7703_HSYNC_PULSE_WIDTH** default: `20` — HSYNC pulse width in pixel clocks.
- **ST7703_LANE_BIT_RATE** default: `480` — MIPI-DSI lane bit rate in Mbps for ST7703 panels (ESP32-P4 only).
- **ST7703_VSYNC_BACK_PORCH** default: `20` — VSYNC back porch in lines.
- **ST7703_VSYNC_FRONT_PORCH** default: `20` — VSYNC front porch in lines.
- **ST7703_VSYNC_PULSE_WIDTH** default: `4` — VSYNC pulse width in lines.
- **TFT_BACKLIGHT_DUTY_MAX** default: `255` — Duty cycle at full saturation (before constant DC).
- **TFT_BACKLIGHT_DUTY_MIN** default: `0` — Duty cycle where backlight first turns on.
- **TFT_BACKLIGHT_ON** default: `(no default)` — Backlight "on" level.
- **TFT_BACKLIGHT_PWM_CHANNEL** default: `0` — LEDC channel used for backlight PWM.
- **TOUCH_CAL_X_MAX** default: `(no default)` — Touch calibration: X maximum.
- **TOUCH_CAL_X_MIN** default: `(no default)` — Touch calibration: X minimum.
- **TOUCH_CAL_Y_MAX** default: `(no default)` — Touch calibration: Y maximum.
- **TOUCH_CAL_Y_MIN** default: `(no default)` — Touch calibration: Y minimum.
- **TOUCH_I2C_ADDR** default: `(no default)` — Touch I2C address.
- **TOUCH_I2C_ADDR_ALT** default: `(no default)` — Optional alternate address (GT911 can be 0x5D or 0x14 depending on INT strap).
- **TOUCH_I2C_BUS** default: `1` — ESP32-P4 can use Wire (bus 0) since WiFi runs on external C6 over SDIO.
- **TOUCH_I2C_PORT** default: `(no default)` — I2C controller index.
- **USE_HSPI_PORT** default: `(no default)` — CYD uses HSPI for the display.
<!-- END COMPILE_FLAG_REPORT:FLAGS -->

## Board Matrix: Features (generated)

Legend: ✅ = enabled/true, blank = disabled/false, ? = unknown/undefined

<!-- BEGIN COMPILE_FLAG_REPORT:MATRIX_FEATURES -->
| board-name | HAS_BACKLIGHT | HAS_BLE | HAS_BUILTIN_LED | HAS_BUTTON | HAS_DISPLAY | HAS_MQTT | HAS_SENSOR_BME280 | HAS_SENSOR_DUMMY | HAS_SENSOR_LD2410_OUT | HAS_TOUCH |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| esp32-nodisplay |  |  |  |  |  | ✅ |  |  |  |  |
| cyd-v2 | ✅ |  |  |  | ✅ | ✅ |  |  |  | ✅ |
| esp32-4848S040 | ✅ |  |  |  | ✅ | ✅ |  |  |  | ✅ |
| jc3248w535 | ✅ |  |  |  | ✅ | ✅ |  |  |  | ✅ |
| jc3636w518 | ✅ |  |  |  | ✅ | ✅ |  |  |  | ✅ |
| esp32c3-withsensors |  | ✅ |  | ✅ |  | ✅ |  | ✅ |  |  |
| esp32-p4-lcd4b | ✅ |  |  |  | ✅ | ✅ |  |  |  | ✅ |
| jc4880p433 | ✅ |  |  |  | ✅ | ✅ |  |  |  | ✅ |
<!-- END COMPILE_FLAG_REPORT:MATRIX_FEATURES -->

## Board Matrix: Selectors (generated)

<!-- BEGIN COMPILE_FLAG_REPORT:MATRIX_SELECTORS -->
| board-name | DISPLAY_DRIVER | TOUCH_DRIVER |
| --- | --- | --- |
| esp32-nodisplay | — | — |
| cyd-v2 | DISPLAY_DRIVER_TFT_ESPI | TOUCH_DRIVER_XPT2046 |
| esp32-4848S040 | DISPLAY_DRIVER_ST7701_RGB | TOUCH_DRIVER_GT911 |
| jc3248w535 | DISPLAY_DRIVER_ARDUINO_GFX | TOUCH_DRIVER_AXS15231B_I2C |
| jc3636w518 | DISPLAY_DRIVER_ARDUINO_GFX_ST77916 | TOUCH_DRIVER_CST816S_WIRE |
| esp32c3-withsensors | — | — |
| esp32-p4-lcd4b | DISPLAY_DRIVER_ST7703_DSI | TOUCH_DRIVER_GT911 |
| jc4880p433 | DISPLAY_DRIVER_ST7701_DSI | TOUCH_DRIVER_GT911 |
<!-- END COMPILE_FLAG_REPORT:MATRIX_SELECTORS -->

## Usage Map (preprocessor only, generated)

<!-- BEGIN COMPILE_FLAG_REPORT:USAGE -->
- **HAS_BACKLIGHT**
  - src/app/app.ino
  - src/app/board_config.h
  - src/app/display_manager.cpp
  - src/app/drivers/arduino_gfx_driver.cpp
  - src/app/drivers/arduino_gfx_st77916_driver.cpp
  - src/app/drivers/mipi_dsi_driver.cpp
  - src/app/drivers/st7701_rgb_driver.cpp
  - src/app/drivers/tft_espi_driver.cpp
- **HAS_BLE**
  - src/app/app.ino
  - src/app/ble_advertiser.cpp
  - src/app/ble_advertiser.h
  - src/app/board_config.h
  - src/app/duty_cycle.cpp
- **HAS_BUILTIN_LED**
  - src/app/app.ino
  - src/app/board_config.h
  - src/app/power_manager.cpp
- **HAS_BUTTON**
  - src/app/app.ino
  - src/app/board_config.h
- **HAS_DISPLAY**
  - src/app/app.ino
  - src/app/board_config.h
  - src/app/config_manager.cpp
  - src/app/config_manager.h
  - src/app/device_telemetry.cpp
  - src/app/display_drivers.cpp
  - src/app/display_manager.cpp
  - src/app/ha_discovery.cpp
  - src/app/lv_conf.h
  - src/app/screen_saver_manager.cpp
  - src/app/screen_saver_manager.h
  - src/app/screens.cpp
  - src/app/touch_manager.cpp
  - src/app/web_portal.cpp
  - src/app/web_portal_config.cpp
  - src/app/web_portal_device_api.cpp
  - src/app/web_portal_display.cpp
  - src/app/web_portal_display.h
  - src/app/web_portal_routes.cpp
- **HAS_MQTT**
  - src/app/app.ino
  - src/app/board_config.h
  - src/app/config_manager.cpp
  - src/app/device_telemetry.cpp
  - src/app/duty_cycle.cpp
  - src/app/ha_discovery.cpp
  - src/app/ha_discovery.h
  - src/app/mqtt_manager.cpp
  - src/app/mqtt_manager.h
  - src/app/sensors/bme280_sensor.cpp
  - src/app/sensors/bme280_sensor.h
  - src/app/sensors/dummy_sensor.cpp
  - src/app/sensors/dummy_sensor.h
  - src/app/sensors/ld2410_out_sensor.cpp
  - src/app/sensors/sensor_manager.cpp
  - src/app/sensors/sensor_manager.h
- **HAS_SENSOR_BME280**
  - src/app/board_config.h
  - src/app/sensors.cpp
  - src/app/sensors/bme280_sensor.cpp
- **HAS_SENSOR_DUMMY**
  - src/app/board_config.h
  - src/app/sensors.cpp
  - src/app/sensors/dummy_sensor.cpp
  - src/app/sensors/dummy_sensor.h
- **HAS_SENSOR_LD2410_OUT**
  - src/app/board_config.h
  - src/app/sensors.cpp
  - src/app/sensors/ld2410_out_sensor.cpp
- **HAS_TOUCH**
  - src/app/app.ino
  - src/app/board_config.h
  - src/app/config_manager.cpp
  - src/app/display_manager.cpp
  - src/app/display_manager.h
  - src/app/lv_conf.h
  - src/app/screen_saver_manager.cpp
  - src/app/screens.cpp
  - src/app/screens/touch_test_screen.cpp
  - src/app/screens/touch_test_screen.h
  - src/app/touch_drivers.cpp
  - src/app/touch_manager.cpp
  - src/app/touch_manager.h
- **DISPLAY_DRIVER**
  - src/app/board_config.h
  - src/app/display_drivers.cpp
  - src/app/display_manager.cpp
- **TOUCH_DRIVER**
  - src/app/board_config.h
  - src/app/touch_drivers.cpp
  - src/app/touch_manager.cpp
- **BME280_I2C_ADDR**
  - src/app/board_config.h
- **BUTTON_ACTIVE_LOW**
  - src/app/board_config.h
- **BUTTON_PIN**
  - src/app/board_config.h
- **DEVICE_TELEMETRY_BACKGROUND_TASKS**
  - src/app/app.ino
  - src/app/board_config.h
- **DISPLAY_INVERSION_ON**
  - src/app/drivers/tft_espi_driver.cpp
- **DISPLAY_NEEDS_GAMMA_FIX**
  - src/app/drivers/tft_espi_driver.cpp
- **DISPLAY_ROTATION**
  - src/app/touch_manager.cpp
- **HEALTH_HISTORY_ENABLED**
  - src/app/app.ino
  - src/app/board_config.h
  - src/app/health_history.cpp
  - src/app/web_portal_device_api.cpp
  - src/app/web_portal_routes.cpp
- **HEALTH_HISTORY_PERIOD_MS**
  - src/app/board_config.h
- **HEALTH_HISTORY_SAMPLES**
  - src/app/board_config.h
- **HEALTH_HISTORY_SECONDS**
  - src/app/board_config.h
- **HEALTH_POLL_INTERVAL_MS**
  - src/app/board_config.h
- **LCD_BL_PIN**
  - src/app/drivers/arduino_gfx_driver.cpp
  - src/app/drivers/arduino_gfx_st77916_driver.cpp
  - src/app/drivers/mipi_dsi_driver.cpp
  - src/app/drivers/st7701_rgb_driver.cpp
- **LCD_CS_PIN**
  - src/app/drivers/st7701_rgb_driver.cpp
- **LCD_MOSI_PIN**
  - src/app/drivers/st7701_rgb_driver.cpp
- **LCD_QSPI_CS**
  - src/app/drivers/arduino_gfx_driver.cpp
  - src/app/drivers/arduino_gfx_st77916_driver.cpp
- **LCD_QSPI_RST**
  - src/app/drivers/arduino_gfx_st77916_driver.cpp
- **LCD_RST_PIN**
  - src/app/drivers/mipi_dsi_driver.cpp
- **LCD_SCK_PIN**
  - src/app/drivers/st7701_rgb_driver.cpp
- **LD2410_OUT_DEBOUNCE_MS**
  - src/app/board_config.h
- **LD2410_OUT_PIN**
  - src/app/board_config.h
- **LED_ACTIVE_HIGH**
  - src/app/board_config.h
- **LED_PIN**
  - src/app/board_config.h
- **LVGL_BUFFER_PREFER_INTERNAL**
  - src/app/board_config.h
- **LVGL_BUFFER_SIZE**
  - src/app/board_config.h
- **LVGL_REFR_PERIOD_MS**
  - src/app/display_manager.cpp
- **LVGL_TASK_CORE**
  - src/app/board_config.h
- **LVGL_TASK_PRIORITY**
  - src/app/board_config.h
- **LVGL_TICK_PERIOD_MS**
  - src/app/board_config.h
- **MEMORY_TRIPWIRE_CHECK_INTERVAL_MS**
  - src/app/board_config.h
- **MEMORY_TRIPWIRE_INTERNAL_MIN_BYTES**
  - src/app/board_config.h
  - src/app/device_telemetry.cpp
- **POWERON_CONFIG_BURST_ENABLED**
  - src/app/board_config.h
  - src/app/power_manager.cpp
- **PROJECT_DISPLAY_NAME**
  - src/app/board_config.h
- **SENSOR_I2C_FREQUENCY**
  - src/app/board_config.h
- **SENSOR_I2C_SCL**
  - src/app/board_config.h
- **SENSOR_I2C_SDA**
  - src/app/board_config.h
- **ST7701_DSI_DPI_CLK_HZ**
  - src/app/board_config.h
- **ST7701_DSI_HSYNC_BACK_PORCH**
  - src/app/board_config.h
- **ST7701_DSI_HSYNC_FRONT_PORCH**
  - src/app/board_config.h
- **ST7701_DSI_HSYNC_PULSE_WIDTH**
  - src/app/board_config.h
- **ST7701_DSI_LANE_BIT_RATE**
  - src/app/board_config.h
- **ST7701_DSI_VSYNC_BACK_PORCH**
  - src/app/board_config.h
- **ST7701_DSI_VSYNC_FRONT_PORCH**
  - src/app/board_config.h
- **ST7701_DSI_VSYNC_PULSE_WIDTH**
  - src/app/board_config.h
- **ST7703_DPI_CLK_HZ**
  - src/app/board_config.h
  - src/boards/esp32-p4-lcd4b/board_overrides.h
- **ST7703_HSYNC_BACK_PORCH**
  - src/app/board_config.h
  - src/boards/esp32-p4-lcd4b/board_overrides.h
- **ST7703_HSYNC_FRONT_PORCH**
  - src/app/board_config.h
  - src/boards/esp32-p4-lcd4b/board_overrides.h
- **ST7703_HSYNC_PULSE_WIDTH**
  - src/app/board_config.h
  - src/boards/esp32-p4-lcd4b/board_overrides.h
- **ST7703_LANE_BIT_RATE**
  - src/app/board_config.h
  - src/boards/esp32-p4-lcd4b/board_overrides.h
- **ST7703_VSYNC_BACK_PORCH**
  - src/app/board_config.h
  - src/boards/esp32-p4-lcd4b/board_overrides.h
- **ST7703_VSYNC_FRONT_PORCH**
  - src/app/board_config.h
  - src/boards/esp32-p4-lcd4b/board_overrides.h
- **ST7703_VSYNC_PULSE_WIDTH**
  - src/app/board_config.h
  - src/boards/esp32-p4-lcd4b/board_overrides.h
- **TFT_BACKLIGHT_DUTY_MAX**
  - src/app/board_config.h
- **TFT_BACKLIGHT_DUTY_MIN**
  - src/app/board_config.h
- **TFT_BACKLIGHT_ON**
  - src/app/drivers/arduino_gfx_driver.cpp
  - src/app/drivers/arduino_gfx_st77916_driver.cpp
  - src/app/drivers/mipi_dsi_driver.cpp
  - src/app/drivers/st7701_rgb_driver.cpp
  - src/app/drivers/tft_espi_driver.cpp
- **TFT_BACKLIGHT_PWM_CHANNEL**
  - src/app/board_config.h
- **TFT_BACKLIGHT_PWM_FREQ**
  - src/app/board_config.h
- **TFT_BL**
  - src/app/drivers/tft_espi_driver.cpp
- **TFT_SPI_FREQ_HZ**
  - src/app/drivers/arduino_gfx_st77916_driver.cpp
- **TOUCH_CAL_X_MAX**
  - src/app/touch_manager.cpp
- **TOUCH_CAL_X_MIN**
  - src/app/touch_manager.cpp
- **TOUCH_CAL_Y_MAX**
  - src/app/touch_manager.cpp
- **TOUCH_CAL_Y_MIN**
  - src/app/touch_manager.cpp
- **TOUCH_I2C_ADDR**
  - src/app/drivers/axs15231b_touch_driver.cpp
- **TOUCH_I2C_ADDR_ALT**
  - src/app/drivers/gt911_touch_driver.cpp
- **TOUCH_I2C_BUS**
  - src/app/board_config.h
  - src/app/drivers/gt911_touch_driver.h
- **TOUCH_I2C_SCL**
  - src/app/drivers/axs15231b_touch_driver.cpp
  - src/app/drivers/wire_cst816s_touch_driver.cpp
- **TOUCH_I2C_SDA**
  - src/app/drivers/wire_cst816s_touch_driver.cpp
- **TOUCH_INT**
  - src/app/drivers/axs15231b_touch_driver.cpp
  - src/app/drivers/gt911_touch_driver.cpp
- **TOUCH_MISO**
  - src/app/drivers/xpt2046_driver.cpp
- **TOUCH_MOSI**
  - src/app/drivers/xpt2046_driver.cpp
- **TOUCH_RST**
  - src/app/board_config.h
  - src/app/drivers/gt911_touch_driver.cpp
  - src/app/drivers/wire_cst816s_touch_driver.cpp
- **TOUCH_SCLK**
  - src/app/drivers/xpt2046_driver.cpp
- **WEB_PORTAL_CONFIG_BODY_TIMEOUT_MS**
  - src/app/board_config.h
- **WEB_PORTAL_CONFIG_MAX_JSON_BYTES**
  - src/app/board_config.h
- **WIFI_MAX_ATTEMPTS**
  - src/app/board_config.h
<!-- END COMPILE_FLAG_REPORT:USAGE -->
