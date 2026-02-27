/*
 * MIPI-DSI Display Driver Base Class (Direct ESP-IDF)
 * 
 * Shared base class for all MIPI-DSI panel drivers on ESP32-P4.
 * Uses direct ESP-IDF MIPI-DSI APIs for maximum performance:
 *   - DMA2D hardware-accelerated async flush (decouples CPU from pixel copy)
 *   - Per-panel disable_lp flag (HS vs LP mode during blanking, panel-dependent)
 * 
 * Why bypass Arduino_GFX for DSI?
 *   Arduino_GFX's Arduino_ESP32DSIPanel does not expose the `disable_lp` flag in
 *   ESP-IDF's esp_lcd_dpi_panel_config_t.  With disable_lp=false (the default),
 *   the DSI host enters Low-Power mode during every blanking interval (HBP, HFP,
 *   VBP, VFP, VSA).  The resulting hundreds of HS↔LP D-PHY transitions per frame
 *   cause visible flashes on some panels.
 *   Additionally, Arduino_GFX uses blocking CPU memcpy for pixel flush, while
 *   direct ESP-IDF with use_dma2d=true offloads the copy to hardware DMA2D.
 * 
 * Panel lifecycle (shared):
 *   1. LDO powers MIPI D-PHY
 *   2. esp_lcd_new_dsi_bus()       — DSI bus (2-lane, clock)
 *   3. esp_lcd_new_panel_io_dbi()  — command channel (DBI virtual-channel 0)
 *   4. esp_lcd_new_panel_dpi()     — DPI panel (framebuffer, timing, disable_lp)
 *   5. Vendor init commands via esp_lcd_panel_io_tx_param()
 *   6. esp_lcd_panel_init()        — starts continuous DMA refresh from PSRAM FB
 *   7. esp_lcd_dpi_panel_get_frame_buffer() — LVGL writes pixels here
 * 
 * LVGL v9 flush path:
 *   setAddrWindow → pushColors → esp_lcd_panel_draw_bitmap()
 *   → DMA2D hardware async copy → on_color_trans_done ISR callback
 *   → lv_display_flush_ready() (decouples CPU from pixel copy)
 * 
 * Subclasses provide:
 *   - Vendor init command table (panel-specific register programming)
 *   - DSI timing configuration (DPI clock, lane rate, HSYNC/VSYNC)
 *   - Log tag for debug output
 */

#ifndef MIPI_DSI_DRIVER_H
#define MIPI_DSI_DRIVER_H

#include "../display_driver.h"
#include "../board_config.h"

// ESP-IDF MIPI-DSI and LCD panel APIs (ESP32-P4 only)
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_mipi_dsi.h>
#include <esp_lcd_panel_io.h>
#include <esp_ldo_regulator.h>
#include <esp_cache.h>

// Vendor init command descriptor — matches Arduino_GFX lcd_init_cmd_t layout
typedef struct {
    int cmd;
    const void* data;
    size_t data_bytes;
    unsigned int delay_ms;
} mipi_dsi_init_cmd_t;

// DSI timing configuration provided by each panel subclass
struct MipiDsiTimingConfig {
    uint32_t dpi_clock_hz;
    uint32_t lane_bit_rate_mbps;
    uint32_t hsync_pulse_width;
    uint32_t hsync_back_porch;
    uint32_t hsync_front_porch;
    uint32_t vsync_pulse_width;
    uint32_t vsync_back_porch;
    uint32_t vsync_front_porch;
    // Keep D-PHY in HS mode during blanking intervals.
    // true:  eliminates HS↔LP flicker on panels like ST7703
    // false: required by panels like ST7701S that expect LP signaling
    bool disable_lp;
};

class MipiDsiDriver : public DisplayDriver {
protected:
    uint16_t* framebuffer;                   // DPI panel PSRAM framebuffer (from ESP-IDF)
    esp_lcd_panel_handle_t panel_handle;      // DPI panel handle
    uint8_t currentBrightness;               // Current brightness level (0-100%)
    uint16_t displayWidth;
    uint16_t displayHeight;
    uint8_t displayRotation;
    bool backlightOn;
    int16_t flushX, flushY;
    uint16_t flushW, flushH;
    lv_display_t* lvglDisplay;

    // Subclass must provide these
    virtual const mipi_dsi_init_cmd_t* getInitCommands() const = 0;
    virtual size_t getInitCommandCount() const = 0;
    virtual const char* getLogTag() const = 0;
    virtual MipiDsiTimingConfig getTimingConfig() const = 0;

public:
    MipiDsiDriver();
    ~MipiDsiDriver() override;

    void init() override;
    void setRotation(uint8_t rotation) override;
    int width() override;
    int height() override;
    void setBacklight(bool on) override;
    void setBacklightBrightness(uint8_t brightness) override;
    uint8_t getBacklightBrightness() override;
    bool hasBacklightControl() override;
    void applyDisplayFixes() override;

    void startWrite() override;
    void endWrite() override;
    void setAddrWindow(int16_t x, int16_t y, uint16_t w, uint16_t h) override;
    void pushColors(uint16_t* data, uint32_t len, bool swap_bytes = true) override;

    void configureLVGL(lv_display_t* disp, uint8_t rotation) override;
    bool asyncFlush() const override;
};

#endif // MIPI_DSI_DRIVER_H
