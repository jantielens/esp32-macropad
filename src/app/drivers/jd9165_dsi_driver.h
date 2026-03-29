/*
 * JD9165 MIPI-DSI Display Driver
 * 
 * Thin subclass of MipiDsiDriver for JD9165-based MIPI-DSI panels.
 * Provides vendor init commands and DSI timing for the GUITION JC1060P470C
 * (1024×600 7-inch IPS MIPI-DSI panel, ESP32-P4).
 * 
 * All DSI bus setup, DMA2D flush, backlight, and LVGL integration
 * are handled by the MipiDsiDriver base class.
 */

#ifndef JD9165_DSI_DRIVER_H
#define JD9165_DSI_DRIVER_H

#include "mipi_dsi_driver.h"

class JD9165_DSI_Driver : public MipiDsiDriver {
protected:
    const mipi_dsi_init_cmd_t* getInitCommands() const override;
    size_t getInitCommandCount() const override;
    const char* getLogTag() const override;
    MipiDsiTimingConfig getTimingConfig() const override;
};

#endif // JD9165_DSI_DRIVER_H
