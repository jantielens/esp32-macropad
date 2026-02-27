#ifndef BOARD_OVERRIDES_JC4880P433_H
#define BOARD_OVERRIDES_JC4880P433_H

// ============================================================================
// GUITION JC4880P433 Board Configuration Overrides
// ============================================================================
// Hardware: ESP32-P4 (dual RISC-V 400 MHz) + ST7701S MIPI-DSI 480x800 + GT911 touch
// WiFi: External ESP32-C6 co-processor over SDIO (no onboard Bluetooth)
// Reference: https://github.com/csvke/esp32_p4_jc4880p433c_bsp

// ============================================================================
// Capabilities
// ============================================================================
#define HAS_DISPLAY true
#define HAS_TOUCH true
#define HAS_BACKLIGHT true
#define HAS_BLE false          // No onboard Bluetooth (C6 co-proc is WiFi-only via SDIO)

// Pin LVGL render task to Core 1 (Core 0 handles WiFi SDIO + system tasks)
#define LVGL_TASK_CORE 1

// ============================================================================
// Driver Selection (HAL)
// ============================================================================
#define DISPLAY_DRIVER DISPLAY_DRIVER_ST7701_DSI
// Panel IC name string (used by tools/generate-board-driver-table.py for the board→driver table).
#define DISPLAY_PANEL "ST7701"
#define TOUCH_DRIVER TOUCH_DRIVER_GT911

// ============================================================================
// Display geometry
// ============================================================================
#define DISPLAY_WIDTH 480
#define DISPLAY_HEIGHT 800
#define DISPLAY_ROTATION 0

// LVGL draw buffer: PSRAM is fine — DMA2D handles the copy to the framebuffer.
#define LVGL_BUFFER_PREFER_INTERNAL false
#define LVGL_BUFFER_SIZE (DISPLAY_WIDTH * 80)

// LVGL refresh period — 15 ms (~66 fps target).
#define LVGL_REFR_PERIOD_MS 15

// ============================================================================
// Backlight (LEDC) — Active-High
// ============================================================================
#define LCD_BL_PIN 23
#define TFT_BACKLIGHT_ON HIGH  // Active-high backlight
#define TFT_BACKLIGHT_PWM_CHANNEL 0
#define TFT_BACKLIGHT_PWM_FREQ 20000  // 20 kHz (from BSP defaults)

// ============================================================================
// Panel Reset
// ============================================================================
#define LCD_RST_PIN 5          // From GUITION BSP

// ============================================================================
// DSI Timing (from Arduino_GFX + GUITION BSP)
// ============================================================================
// MIPI-DSI: 2-lane, 500 Mbps/lane, 34 MHz DPI clock
// Defaults in board_config.h match JC4880P433 — no overrides needed.
// Uncomment only if tuning is required:
// #define ST7701_DSI_DPI_CLK_HZ        34000000L
// #define ST7701_DSI_LANE_BIT_RATE      500
// #define ST7701_DSI_HSYNC_PULSE_WIDTH  12
// #define ST7701_DSI_HSYNC_BACK_PORCH   42
// #define ST7701_DSI_HSYNC_FRONT_PORCH  42
// #define ST7701_DSI_VSYNC_PULSE_WIDTH  2
// #define ST7701_DSI_VSYNC_BACK_PORCH   8
// #define ST7701_DSI_VSYNC_FRONT_PORCH  166

// ============================================================================
// Touch (GT911 on I2C bus 0)
// ============================================================================
// ESP32-P4: WiFi runs on external ESP32-C6 over SDIO, so no ISR contention
// on Wire (bus 0). Use Wire instead of Wire1.
#define TOUCH_I2C_BUS 0
#define TOUCH_I2C_SDA 7
#define TOUCH_I2C_SCL 8
#define TOUCH_I2C_ADDR 0x5D
#define TOUCH_I2C_ADDR_ALT 0x14
#define TOUCH_RST -1           // No hardware reset (NC per BSP)
#define TOUCH_INT -1           // No interrupt pin (NC per BSP)

// ============================================================================
// Telemetry tuning
// ============================================================================
// Disable background telemetry tasks to reduce scheduler contention with
// display rendering on Core 0.
#define DEVICE_TELEMETRY_BACKGROUND_TASKS 0
#define HEALTH_HISTORY_ENABLED 0

#endif // BOARD_OVERRIDES_JC4880P433_H
