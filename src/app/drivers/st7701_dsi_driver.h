/*
 * ST7701 MIPI-DSI Display Driver
 * 
 * Thin subclass of MipiDsiDriver for ST7701-based MIPI-DSI panels.
 * Provides vendor init commands and DSI timing for the GUITION JC4880P433
 * (480×800 IPS MIPI-DSI panel, ESP32-P4).
 * 
 * All DSI bus setup, DMA2D flush, backlight, and LVGL integration
 * are handled by the MipiDsiDriver base class.
 * 
 * Previous implementation wrapped Arduino_GFX (blocking CPU memcpy).
 * This version uses direct ESP-IDF with DMA2D async flush for ~2× FPS.
 */

#ifndef ST7701_DSI_DRIVER_H
#define ST7701_DSI_DRIVER_H

#include "mipi_dsi_driver.h"

class ST7701_DSI_Driver : public MipiDsiDriver {
protected:
    const mipi_dsi_init_cmd_t* getInitCommands() const override;
    size_t getInitCommandCount() const override;
    const char* getLogTag() const override;
    MipiDsiTimingConfig getTimingConfig() const override;
};

#endif // ST7701_DSI_DRIVER_H
