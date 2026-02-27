#ifndef BOARD_OVERRIDES_H
#define BOARD_OVERRIDES_H

// ============================================================================
// Board Overrides: jc3636w518 (ESP32-S3 + ST77916 QSPI 360x360 + CST816S touch)
// Mirrors the known-good setup from sample/jc3636w518-macropad.
// ============================================================================

// ---------------------------------------------------------------------------
// Capabilities
// ---------------------------------------------------------------------------
// Enable display support on this board.
#define HAS_DISPLAY true

// LVGL: place built-in CPU/FPS perf monitor at bottom-center (round display)
// LVGL perf monitor alignment.
#define LV_USE_PERF_MONITOR_POS LV_ALIGN_BOTTOM_MID
// Enable backlight control.
#define HAS_BACKLIGHT true
// Enable touch support.
#define HAS_TOUCH true

// ---------------------------------------------------------------------------
// Driver Selection (HAL)
// ---------------------------------------------------------------------------
// Select Arduino_GFX ST77916 as the display HAL backend.
#define DISPLAY_DRIVER DISPLAY_DRIVER_ARDUINO_GFX_ST77916
// Select CST816S (Wire I2C) as the touch HAL backend.
#define TOUCH_DRIVER TOUCH_DRIVER_CST816S_WIRE

// ---------------------------------------------------------------------------
// Display geometry
// ---------------------------------------------------------------------------
// Panel width in pixels.
#define DISPLAY_WIDTH 360
// Panel height in pixels.
#define DISPLAY_HEIGHT 360
// UI rotation (LVGL).
#define DISPLAY_ROTATION 0

// Match the sample: prefer PSRAM for LVGL draw buffer (fallback handled in DisplayManager).
// Prefer internal RAM over PSRAM for LVGL draw buffer allocation.
#define LVGL_BUFFER_PREFER_INTERNAL false
// LVGL draw buffer size in pixels.
#define LVGL_BUFFER_SIZE (DISPLAY_WIDTH * 16)  // 16 rows (matches sample default)

// ---------------------------------------------------------------------------
// Backlight (LEDC)
// ---------------------------------------------------------------------------
// Backlight pin.
#define LCD_BL_PIN 15

// ---------------------------------------------------------------------------
// QSPI panel pins (ST77916) — Arduino_GFX naming convention
// ---------------------------------------------------------------------------
// QSPI reset pin.
#define LCD_QSPI_RST 47
// QSPI chip select pin.
#define LCD_QSPI_CS 10
// QSPI pixel clock pin.
#define LCD_QSPI_PCLK 9
// QSPI data line 0 pin.
#define LCD_QSPI_D0 11
// QSPI data line 1 pin.
#define LCD_QSPI_D1 12
// QSPI data line 2 pin.
#define LCD_QSPI_D2 13
// QSPI data line 3 pin.
#define LCD_QSPI_D3 14

// QSPI clock frequency (Hz).
#define TFT_SPI_FREQ_HZ (50 * 1000 * 1000)

// ---------------------------------------------------------------------------
// Touch pins (CST816S over I2C)
// ---------------------------------------------------------------------------
// Touch I2C SCL pin.
#define TOUCH_I2C_SCL 8
// Touch I2C SDA pin.
#define TOUCH_I2C_SDA 7
// Touch interrupt pin.
#define TOUCH_INT 41
// Touch reset pin.
#define TOUCH_RST 40

#endif // BOARD_OVERRIDES_H
