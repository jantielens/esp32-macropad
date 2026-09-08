#ifndef BOARD_OVERRIDES_JC4827W543C_H
#define BOARD_OVERRIDES_JC4827W543C_H

// ============================================================================
// GUITION JC4827W543C
// ESP32-S3-WROOM-1-N4R8, 4.3-inch 480x272 NV3041A QSPI IPS panel and GT911
// capacitive touch. Pin mapping follows the manufacturer Arduino examples:
// https://github.com/wegi1/ESP32-S3-JC4827W543C
//
// This target is exclusively for the capacitive-touch C SKU. The resistive
// XPT2046 variant requires a separate target and has not been configured.
// ============================================================================

#define HAS_DISPLAY true
#define HAS_TOUCH true
#define HAS_BACKLIGHT true
#define HAS_NATIVE_EXTENSIONS false
#define HAS_BLE_HID false
#define HAS_MCP false
#define HAS_IMAGE_FETCH false
// Generated driver-table validation note.
#define BOARD_DRIVER_TABLE_NOTES "hardware validated by @00lems00 (#81)"

// ESP32-S3 internal memory is constrained when WiFi and the display are active.
#define LVGL_TASK_CORE 1

// ============================================================================
// Driver Selection (HAL)
// ============================================================================
#define DISPLAY_DRIVER DISPLAY_DRIVER_ARDUINO_GFX_NV3041A
#define TOUCH_DRIVER TOUCH_DRIVER_GT911

// ============================================================================
// Pad Layout
// ============================================================================
#define DISPLAY_SHAPE DISPLAY_SHAPE_RECT
#define UI_SCALE_TIER UI_SCALE_SMALL
#define DISPLAY_WIDTH 480
#define DISPLAY_HEIGHT 272
#define DISPLAY_ROTATION 0
#define LVGL_BUFFER_PREFER_INTERNAL false
#define LVGL_BUFFER_SIZE (DISPLAY_WIDTH * 32)

// ============================================================================
// NV3041A QSPI display
// ============================================================================
#define LCD_QSPI_CS 45
#define LCD_QSPI_PCLK 47
#define LCD_QSPI_D0 21
#define LCD_QSPI_D1 48
#define LCD_QSPI_D2 40
#define LCD_QSPI_D3 39
#define LCD_BL_PIN 1
#define TFT_SPI_FREQ_HZ (32 * 1000 * 1000)

// ============================================================================
// GT911 capacitive touch
// ============================================================================
// Use Wire1 to keep touch I2C interrupts off WiFi's core on ESP32-S3.
#define TOUCH_I2C_BUS 1
#define TOUCH_I2C_SDA 8
#define TOUCH_I2C_SCL 4
#define TOUCH_RST 38
#define TOUCH_INT 3
#define TOUCH_I2C_ADDR 0x5D
#define TOUCH_I2C_ADDR_ALT 0x14

#endif // BOARD_OVERRIDES_JC4827W543C_H