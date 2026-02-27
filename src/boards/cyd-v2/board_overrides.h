#ifndef BOARD_OVERRIDES_CYD2USB_V2_H
#define BOARD_OVERRIDES_CYD2USB_V2_H

// ============================================================================
// ESP32-2432S028R v2 (CYD - 1 USB Port) Board Configuration Overrides
// ============================================================================
// This file overrides default settings in src/app/board_config.h for the
// ESP32-2432S028R v2 "Cheap Yellow Display" with single USB port.
//
// Hardware: ESP32 + 2.8" ILI9341 TFT (320x240) + XPT2046 Touch
// Display: ILI9341 driver with TFT_INVERSION_ON + gamma correction
//
// Reference: https://github.com/witnessmenow/ESP32-Cheap-Yellow-Display

// ============================================================================
// Display Configuration
// ============================================================================
// Enable display support on this board.
#define HAS_DISPLAY true

// ============================================================================
// Driver Selection (HAL)
// ============================================================================
// Display backend: TFT_eSPI (ILI9341 over SPI)
// Touch backend:   XPT2046 (SPI)
// Select TFT_eSPI as the display HAL backend.
#define DISPLAY_DRIVER DISPLAY_DRIVER_TFT_ESPI
// Select XPT2046 as the touch HAL backend.
#define TOUCH_DRIVER   TOUCH_DRIVER_XPT2046

// ============================================================================
// Display Controller Config (TFT_eSPI)
// ============================================================================
// Must match library's User_Setup.h or use build flags
// For v2 (1 USB port): ILI9341_2_DRIVER with TFT_INVERSION_ON
// Use the ILI9341_2 controller setup in TFT_eSPI.
#define DISPLAY_DRIVER_ILI9341_2

// TFT_eSPI library configuration (for clean builds / CI)
// These macros are consumed by the TFT_eSPI library itself.
#define ILI9341_2_DRIVER

// CYD uses HSPI for the display.
#define USE_HSPI_PORT
// Enable display inversion (panel-specific).
#define DISPLAY_INVERSION_ON true

// Gamma Correction Fix Required for v2
// Both 1-USB and 2-USB variants need gamma correction
// See: https://github.com/witnessmenow/ESP32-Cheap-Yellow-Display/blob/main/cyd.md
// Apply gamma correction fix for this panel variant.
#define DISPLAY_NEEDS_GAMMA_FIX true

// Display Pins (HSPI)
// TFT_eSPI: MISO pin.
#define TFT_MISO 12
// TFT_eSPI: MOSI pin.
#define TFT_MOSI 13
// TFT_eSPI: SCLK pin.
#define TFT_SCLK 14
// TFT_eSPI: CS pin.
#define TFT_CS   15
// TFT_eSPI: DC pin.
#define TFT_DC   2
// TFT_eSPI: RST pin (-1 = none).
#define TFT_RST  -1   // No reset pin
// TFT_eSPI: backlight pin.
#define TFT_BL   21   // Backlight

// Display Properties
// Panel width in pixels.
#define DISPLAY_WIDTH  320
// Panel height in pixels.
#define DISPLAY_HEIGHT 240
// UI rotation (LVGL).
#define DISPLAY_ROTATION 1  // Landscape (0=portrait, 1=landscape, 2=portrait_flip, 3=landscape_flip)

// SPI Frequency
// TFT SPI clock frequency.
#define TFT_SPI_FREQUENCY 40000000  // 40MHz (known-good for CYD)

// TFT_eSPI library SPI frequency knobs
// TFT_eSPI: SPI write frequency (Hz).
#define SPI_FREQUENCY 40000000
// TFT_eSPI: SPI read frequency (Hz).
#define SPI_READ_FREQUENCY 20000000
// TFT_eSPI: SPI touch frequency (Hz).
#define SPI_TOUCH_FREQUENCY 2500000

// Color Order
// Panel uses BGR byte order.
#define DISPLAY_COLOR_ORDER_BGR true  // BGR color order (not RGB)

// Backlight Control
// Enable backlight control on this board.
#define HAS_BACKLIGHT true
// Backlight "on" level.
#define TFT_BACKLIGHT_ON HIGH
// LEDC channel used for backlight PWM.
#define TFT_BACKLIGHT_PWM_CHANNEL 0  // LEDC channel for PWM

// TFT_eSPI Touch Controller Pins (required for TFT_eSPI touch extensions)
// TFT_eSPI touch: CS pin.
#define TOUCH_CS 33     // Touch chip select
// TFT_eSPI touch: SCLK pin.
#define TOUCH_SCLK 25   // Touch SPI clock
// TFT_eSPI touch: MISO pin.
#define TOUCH_MISO 39   // Touch SPI MISO
// TFT_eSPI touch: MOSI pin.
#define TOUCH_MOSI 32   // Touch SPI MOSI
// TFT_eSPI touch: IRQ pin (optional).
#define TOUCH_IRQ 36    // Touch interrupt (optional)

// ============================================================================
// Touch Screen Configuration (XPT2046)
// ============================================================================
// Touch uses separate VSPI bus
// Enable touch support on this board.
#define HAS_TOUCH true

// XPT2046 pins (VSPI bus - separate from display)
// XPT2046 IRQ pin.
#define XPT2046_IRQ  36
// XPT2046 MOSI pin.
#define XPT2046_MOSI 32
// XPT2046 MISO pin.
#define XPT2046_MISO 39
// XPT2046 CLK pin.
#define XPT2046_CLK  25
// XPT2046 CS pin.
#define XPT2046_CS   33

// Calibration values (from macsbug.wordpress.com)
// Touch calibration: X minimum.
#define TOUCH_CAL_X_MIN 300
// Touch calibration: X maximum.
#define TOUCH_CAL_X_MAX 3900
// Touch calibration: Y minimum.
#define TOUCH_CAL_Y_MIN 200
// Touch calibration: Y maximum.
#define TOUCH_CAL_Y_MAX 3700

// ============================================================================
// Additional Hardware on CYD
// ============================================================================
// RGB LED (active low)
// #define HAS_RGB_LED true
// #define RGB_LED_RED   4
// #define RGB_LED_GREEN 16
// #define RGB_LED_BLUE  17

// SD Card (VSPI)
// #define HAS_SD_CARD true
// #define SD_CS   5
// #define SD_MISO 19
// #define SD_MOSI 23
// #define SD_SCLK 18

// Light Sensor
// #define HAS_LDR true
// #define LDR_PIN 34


#endif // BOARD_OVERRIDES_CYD2USB_V2_H
