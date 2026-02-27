// Board-specific TFT_eSPI setup for ESP32-2432S028R v2 (CYD - 1 USB Port)
// This file is force-included for cyd-v2 builds via build.sh so CI/web builds
// do not depend on a developer's global TFT_eSPI/User_Setup.h.

#ifndef CYD_V2_TFT_ESPI_USER_SETUP_H
#define CYD_V2_TFT_ESPI_USER_SETUP_H

// Display inversion is required for this panel variant
#define TFT_INVERSION_ON

// Color order
#define TFT_RGB_ORDER TFT_BGR

// Note: TFT_eSPI core config (controller, pins, bus selection, SPI frequencies)
// is provided via build flags exported from src/boards/cyd-v2/board_overrides.h.

// Optional: Fonts (keep default feature set reasonably complete)
#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define LOAD_FONT6
#define LOAD_FONT7
#define LOAD_FONT8
#define LOAD_GFXFF

#define SMOOTH_FONT

#endif // CYD_V2_TFT_ESPI_USER_SETUP_H
