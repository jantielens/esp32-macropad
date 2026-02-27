/*
 * ST7703 MIPI-DSI Display Driver
 * 
 * Thin subclass of MipiDsiDriver for ST7703-based MIPI-DSI panels.
 * Provides vendor init commands and DSI timing for the Waveshare
 * ESP32-P4-WIFI6-Touch-LCD-4B (720×720 IPS MIPI-DSI panel).
 * 
 * All DSI bus setup, DMA2D flush, backlight, and LVGL integration
 * are handled by the MipiDsiDriver base class.
 */

#ifndef ST7703_DSI_DRIVER_H
#define ST7703_DSI_DRIVER_H

#include "mipi_dsi_driver.h"

class ST7703_DSI_Driver : public MipiDsiDriver {
protected:
    const mipi_dsi_init_cmd_t* getInitCommands() const override;
    size_t getInitCommandCount() const override;
    const char* getLogTag() const override;
    MipiDsiTimingConfig getTimingConfig() const override;
};

#endif // ST7703_DSI_DRIVER_H
