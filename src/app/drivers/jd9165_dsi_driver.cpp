/*
 * JD9165 MIPI-DSI Display Driver — Vendor Init Commands + Timing
 * 
 * Thin subclass of MipiDsiDriver providing JD9165-specific init commands
 * and DSI timing for the GUITION JC1060P470C (1024×600, ESP32-P4).
 * 
 * Init sequence: From JC1060P470C BSP (esp_lcd_jd9165 managed component),
 *   adapted for the HKC 7.0" IPS panel (QD070AS01-1).
 * DSI timing: 2-lane, 550 Mbps/lane, 51.2 MHz DPI clock.
 * 
 * Reference: https://github.com/cheops/JC1060P470C_I_W
 */

#include "jd9165_dsi_driver.h"

// ============================================================================
// JD9165 vendor init commands (from JC1060P470C BSP esp_lcd_jd9165.c,
// adapted for HKC 7.0" IPS panel QD070AS01-1)
// ============================================================================
static const mipi_dsi_init_cmd_t jd9165_dsi_init_operations[] = {
    {0x30, (uint8_t[]){0x00}, 1, 0},
    {0xF7, (uint8_t[]){0x49, 0x61, 0x02, 0x00}, 4, 0},
    {0x30, (uint8_t[]){0x01}, 1, 0},
    {0x04, (uint8_t[]){0x0C}, 1, 0},
    {0x05, (uint8_t[]){0x00}, 1, 0},
    {0x06, (uint8_t[]){0x00}, 1, 0},
    {0x0B, (uint8_t[]){0x11}, 1, 0},
    {0x17, (uint8_t[]){0x00}, 1, 0},
    {0x20, (uint8_t[]){0x04}, 1, 0},
    {0x1F, (uint8_t[]){0x05}, 1, 0},
    {0x23, (uint8_t[]){0x00}, 1, 0},
    {0x25, (uint8_t[]){0x19}, 1, 0},
    {0x28, (uint8_t[]){0x18}, 1, 0},
    {0x29, (uint8_t[]){0x04}, 1, 0},
    {0x2A, (uint8_t[]){0x01}, 1, 0},
    {0x2B, (uint8_t[]){0x04}, 1, 0},
    {0x2C, (uint8_t[]){0x01}, 1, 0},
    {0x30, (uint8_t[]){0x02}, 1, 0},
    {0x01, (uint8_t[]){0x22}, 1, 0},
    {0x03, (uint8_t[]){0x12}, 1, 0},
    {0x04, (uint8_t[]){0x00}, 1, 0},
    {0x05, (uint8_t[]){0x64}, 1, 0},
    {0x0A, (uint8_t[]){0x08}, 1, 0},
    {0x0B, (uint8_t[]){0x0A, 0x1A, 0x0B, 0x0D, 0x0D, 0x11, 0x10, 0x06, 0x08, 0x1F, 0x1D}, 11, 0},
    {0x0C, (uint8_t[]){0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D}, 11, 0},
    {0x0D, (uint8_t[]){0x16, 0x1B, 0x0B, 0x0D, 0x0D, 0x11, 0x10, 0x07, 0x09, 0x1E, 0x1C}, 11, 0},
    {0x0E, (uint8_t[]){0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D}, 11, 0},
    {0x0F, (uint8_t[]){0x16, 0x1B, 0x0D, 0x0B, 0x0D, 0x11, 0x10, 0x1C, 0x1E, 0x09, 0x07}, 11, 0},
    {0x10, (uint8_t[]){0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D}, 11, 0},
    {0x11, (uint8_t[]){0x0A, 0x1A, 0x0D, 0x0B, 0x0D, 0x11, 0x10, 0x1D, 0x1F, 0x08, 0x06}, 11, 0},
    {0x12, (uint8_t[]){0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D}, 11, 0},
    {0x14, (uint8_t[]){0x00, 0x00, 0x11, 0x11}, 4, 0},
    {0x18, (uint8_t[]){0x99}, 1, 0},
    {0x30, (uint8_t[]){0x06}, 1, 0},
    {0x12, (uint8_t[]){0x36, 0x2C, 0x2E, 0x3C, 0x38, 0x35, 0x35, 0x32, 0x2E, 0x1D, 0x2B, 0x21, 0x16, 0x29}, 14, 0},
    {0x13, (uint8_t[]){0x36, 0x2C, 0x2E, 0x3C, 0x38, 0x35, 0x35, 0x32, 0x2E, 0x1D, 0x2B, 0x21, 0x16, 0x29}, 14, 0},
    {0x30, (uint8_t[]){0x0A}, 1, 0},
    {0x02, (uint8_t[]){0x4F}, 1, 0},
    {0x0B, (uint8_t[]){0x40}, 1, 0},
    {0x12, (uint8_t[]){0x3E}, 1, 0},
    {0x13, (uint8_t[]){0x78}, 1, 0},
    {0x30, (uint8_t[]){0x0D}, 1, 0},
    {0x0D, (uint8_t[]){0x04}, 1, 0},
    {0x10, (uint8_t[]){0x0C}, 1, 0},
    {0x11, (uint8_t[]){0x0C}, 1, 0},
    {0x12, (uint8_t[]){0x0C}, 1, 0},
    {0x13, (uint8_t[]){0x0C}, 1, 0},
    {0x30, (uint8_t[]){0x00}, 1, 0},
    {0x11, (uint8_t[]){0x00}, 0, 120},   // Sleep Out + 120 ms delay
    {0x29, (uint8_t[]){0x00}, 0, 50},    // Display On + 50 ms delay
};

// ============================================================================
// JD9165_DSI_Driver — subclass overrides
// ============================================================================

const mipi_dsi_init_cmd_t* JD9165_DSI_Driver::getInitCommands() const {
    return jd9165_dsi_init_operations;
}

size_t JD9165_DSI_Driver::getInitCommandCount() const {
    return sizeof(jd9165_dsi_init_operations) / sizeof(jd9165_dsi_init_operations[0]);
}

const char* JD9165_DSI_Driver::getLogTag() const {
    return "JD9165";
}

MipiDsiTimingConfig JD9165_DSI_Driver::getTimingConfig() const {
    return {
        .dpi_clock_hz = JD9165_DSI_DPI_CLK_HZ,
        .lane_bit_rate_mbps = JD9165_DSI_LANE_BIT_RATE,
        .hsync_pulse_width = JD9165_DSI_HSYNC_PULSE_WIDTH,
        .hsync_back_porch = JD9165_DSI_HSYNC_BACK_PORCH,
        .hsync_front_porch = JD9165_DSI_HSYNC_FRONT_PORCH,
        .vsync_pulse_width = JD9165_DSI_VSYNC_PULSE_WIDTH,
        .vsync_back_porch = JD9165_DSI_VSYNC_BACK_PORCH,
        .vsync_front_porch = JD9165_DSI_VSYNC_FRONT_PORCH,
        // Keep the D-PHY in continuous high-speed mode during blanking, matching
        // the ST7703 panel. With LP signaling the host re-runs the LP->HS ramp on
        // every blanking interval, and this panel's blanking budget is large
        // (320 pixel clocks horizontally, 35 lines vertically).
        // Measured on hardware: true vs false is FPS-neutral here (25 fps either
        // way on the benchmark screen), so this is kept as the safer default.
        // Note: it does NOT eliminate the intermittent full-screen cyan frames —
        // those are a separate PSRAM-bandwidth / DPI-underrun issue.
        .disable_lp = true,
    };
}
