#ifndef BOARD_OVERRIDES_ESP32_4848S040_H
#define BOARD_OVERRIDES_ESP32_4848S040_H

// ============================================================================
// Guition ESP32-S3-4848S040 Board Configuration Overrides
// ============================================================================
// Hardware: ESP32-S3 + ST7701 RGB 480x480 + GT911 touch
// Reference: https://github.com/aquaElectronics/esp32-4848s040-st7701

// ============================================================================
// Capabilities
// ============================================================================
#define HAS_DISPLAY true
#define HAS_TOUCH true
#define HAS_BACKLIGHT true   // PWM brightness control (LEDC attached before LCD init to avoid glitch)

// Pin LVGL render task to Core 1 (reduces PSRAM bus contention with WiFi on Core 0)
#define LVGL_TASK_CORE 1

// ============================================================================
// Driver Selection (HAL)
// ============================================================================
#define DISPLAY_DRIVER DISPLAY_DRIVER_ST7701_RGB
#define TOUCH_DRIVER TOUCH_DRIVER_GT911

// ============================================================================
// Display geometry
// ============================================================================
#define DISPLAY_WIDTH 480
#define DISPLAY_HEIGHT 480
#define DISPLAY_ROTATION 0

// LVGL draw buffer in PSRAM: Arduino_GFX handles cache coherency via
// auto_flush (Cache_WriteBack_Addr after each draw16bitRGBBitmap),
// and the 40-line bounce buffer shields LCD DMA from PSRAM stalls.
// Saves ~19 KB internal SRAM vs LVGL_BUFFER_PREFER_INTERNAL=true.
#define LVGL_BUFFER_PREFER_INTERNAL false
#define LVGL_BUFFER_SIZE (DISPLAY_WIDTH * 20)  // 20 lines partial updates

// ============================================================================
// Backlight (LEDC)
// ============================================================================
// Backlight control pin.
#define LCD_BL_PIN 38
// Backlight active level.
#define TFT_BACKLIGHT_ON HIGH
// LEDC channel for backlight PWM.  Use a high channel (7) to avoid
// collision with timers the ESP-IDF RGB LCD peripheral may claim.
#define TFT_BACKLIGHT_PWM_CHANNEL 7
// PWM frequency tuned for this board's MOSFET backlight circuit.
// 3.5 kHz: no audible whine, smooth dimming from ~30% to 99% duty.
#define TFT_BACKLIGHT_PWM_FREQ 3500
// Usable duty range at 3.5 kHz: MOSFET turns on at duty 77, saturates ~252.
// Duty cycle where backlight first turns on.
#define TFT_BACKLIGHT_DUTY_MIN 77
// Duty cycle at full saturation (before constant DC).
#define TFT_BACKLIGHT_DUTY_MAX 252

// ============================================================================
// 9-bit SPI bus (ST7701 command/config)
// ============================================================================
// SPI SCK pin (ST7701 commands).
#define LCD_SCK_PIN 48
// SPI MOSI pin (ST7701 commands).
#define LCD_MOSI_PIN 47
// SPI CS pin (ST7701 commands).
#define LCD_CS_PIN 39

// ============================================================================
// RGB panel pins (ST7701)
// ============================================================================
// RGB DE pin.
#define LCD_DE_PIN 18
// RGB VSYNC pin.
#define LCD_VSYNC_PIN 17
// RGB HSYNC pin.
#define LCD_HSYNC_PIN 16
// RGB PCLK pin.
#define LCD_PCLK_PIN 21

// RGB Red 0 pin.
#define LCD_R0_PIN 11
// RGB Red 1 pin.
#define LCD_R1_PIN 12
// RGB Red 2 pin.
#define LCD_R2_PIN 13
// RGB Red 3 pin.
#define LCD_R3_PIN 14
// RGB Red 4 pin.
#define LCD_R4_PIN 0

// RGB Green 0 pin.
#define LCD_G0_PIN 8
// RGB Green 1 pin.
#define LCD_G1_PIN 20
// RGB Green 2 pin.
#define LCD_G2_PIN 3
// RGB Green 3 pin.
#define LCD_G3_PIN 46
// RGB Green 4 pin.
#define LCD_G4_PIN 9
// RGB Green 5 pin.
#define LCD_G5_PIN 10

// RGB Blue 0 pin.
#define LCD_B0_PIN 4
// RGB Blue 1 pin.
#define LCD_B1_PIN 5
// RGB Blue 2 pin.
#define LCD_B2_PIN 6
// RGB Blue 3 pin.
#define LCD_B3_PIN 7
// RGB Blue 4 pin.
#define LCD_B4_PIN 15

// ============================================================================
// RGB timing
// ============================================================================
// HSYNC polarity (1 = active high).
#define LCD_HSYNC_POLARITY 1
// HSYNC front porch.
#define LCD_HSYNC_FRONT_PORCH 10
// HSYNC pulse width.
#define LCD_HSYNC_PULSE_WIDTH 8
// HSYNC back porch.
#define LCD_HSYNC_BACK_PORCH 50

// VSYNC polarity (1 = active high).
#define LCD_VSYNC_POLARITY 1
// VSYNC front porch.
#define LCD_VSYNC_FRONT_PORCH 10
// VSYNC pulse width.
#define LCD_VSYNC_PULSE_WIDTH 8
// VSYNC back porch.
#define LCD_VSYNC_BACK_PORCH 20

// ============================================================================
// Touch (GT911)
// ============================================================================
// Touch I2C SDA pin.
#define TOUCH_I2C_SDA 19
// Touch I2C SCL pin.
#define TOUCH_I2C_SCL 45
// Touch I2C address.
#define TOUCH_I2C_ADDR 0x5D
// Optional alternate address (GT911 can be 0x5D or 0x14 depending on INT strap).
#define TOUCH_I2C_ADDR_ALT 0x14
// Touch reset pin (-1 = not connected).
#define TOUCH_RST -1
// Touch interrupt pin (-1 = not connected).
#define TOUCH_INT -1

#endif // BOARD_OVERRIDES_ESP32_4848S040_H
