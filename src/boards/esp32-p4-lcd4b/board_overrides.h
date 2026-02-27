#ifndef BOARD_OVERRIDES_ESP32_P4_LCD4B_H
#define BOARD_OVERRIDES_ESP32_P4_LCD4B_H

// ============================================================================
// Waveshare ESP32-P4-WIFI6-Touch-LCD-4B Board Configuration Overrides
// ============================================================================
// Hardware: ESP32-P4 (dual RISC-V 400 MHz) + ST7703 MIPI-DSI 720x720 + GT911 touch
// WiFi: External ESP32-C6 co-processor over SDIO (no onboard Bluetooth)
// Reference: https://www.waveshare.com/wiki/ESP32-P4-WIFI6-Touch-LCD-4B

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
#define DISPLAY_DRIVER DISPLAY_DRIVER_ST7703_DSI
// Panel IC name string (used by tools/generate-board-driver-table.py for the board→driver table).
#define DISPLAY_PANEL "ST7703"
#define TOUCH_DRIVER TOUCH_DRIVER_GT911

// ============================================================================
// Display geometry
// ============================================================================
#define DISPLAY_WIDTH 720
#define DISPLAY_HEIGHT 720
#define DISPLAY_ROTATION 0

// LVGL draw buffer: single buffer in PSRAM.
// With DMA2D flush, PSRAM buffer is fine — the hardware DMA engine handles
// the copy to the framebuffer without CPU involvement.
#define LVGL_BUFFER_PREFER_INTERNAL false
#define LVGL_BUFFER_SIZE (DISPLAY_WIDTH * 80)

// LVGL refresh period — 15 ms (~66 fps target) to match IDF reference projects.
// Default LVGL 8.4 is 30 ms (~33 fps). Panel hardware supports ~59 fps.
#define LVGL_REFR_PERIOD_MS 15

// ============================================================================
// Backlight (LEDC) — Active-Low
// ============================================================================
#define LCD_BL_PIN 26
#define TFT_BACKLIGHT_ON LOW   // Active-low backlight on this board
#define TFT_BACKLIGHT_PWM_CHANNEL 0
#define TFT_BACKLIGHT_PWM_FREQ 1000

// ============================================================================
// Panel Reset
// ============================================================================
#define LCD_RST_PIN 27

// ============================================================================
// DSI Timing
// ============================================================================
// MIPI-DSI: 2-lane, 480 Mbps/lane
// These can be overridden further if needed.
// DPI pixel clock in Hz (default 38 MHz for ST7703 720x720 panel)
#ifndef ST7703_DPI_CLK_HZ
#define ST7703_DPI_CLK_HZ 38000000L
#endif

#ifndef ST7703_LANE_BIT_RATE
#define ST7703_LANE_BIT_RATE 480
#endif

#ifndef ST7703_HSYNC_PULSE_WIDTH
#define ST7703_HSYNC_PULSE_WIDTH 20
#endif
#ifndef ST7703_HSYNC_BACK_PORCH
#define ST7703_HSYNC_BACK_PORCH 50
#endif
#ifndef ST7703_HSYNC_FRONT_PORCH
#define ST7703_HSYNC_FRONT_PORCH 50
#endif

#ifndef ST7703_VSYNC_PULSE_WIDTH
#define ST7703_VSYNC_PULSE_WIDTH 4
#endif
#ifndef ST7703_VSYNC_BACK_PORCH
#define ST7703_VSYNC_BACK_PORCH 20
#endif
#ifndef ST7703_VSYNC_FRONT_PORCH
#define ST7703_VSYNC_FRONT_PORCH 20
#endif

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
#define TOUCH_RST 23
#define TOUCH_INT -1

// ============================================================================
// Telemetry tuning (temporary — flicker investigation)
// ============================================================================
// Disable ALL background telemetry tasks (CPU monitor, health-window timer,
// tripwires) to eliminate scheduler contention with the display Present task
// on Core 0.  /api/health still works with point-in-time values.
#define DEVICE_TELEMETRY_BACKGROUND_TASKS 0
#define HEALTH_HISTORY_ENABLED 0

#endif // BOARD_OVERRIDES_ESP32_P4_LCD4B_H
