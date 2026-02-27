/*
 * ST7701 MIPI-DSI Display Driver — Vendor Init Commands + Timing
 * 
 * Thin subclass of MipiDsiDriver providing ST7701-specific init commands
 * and DSI timing for the GUITION JC4880P433 (480×800, ESP32-P4).
 * 
 * Init sequence: From Arduino_GFX st7701_dsi_init_operations
 * DSI timing: 2-lane, 500 Mbps/lane, 34 MHz DPI clock (from GUITION BSP)
 * 
 * Previous implementation wrapped Arduino_GFX (blocking CPU memcpy, no disable_lp).
 * Now uses direct ESP-IDF with DMA2D async flush + configurable disable_lp.
 */

#include "st7701_dsi_driver.h"

// ============================================================================
// ST7701 vendor init commands (from Arduino_GFX st7701_dsi_init_operations)
// ============================================================================
static const mipi_dsi_init_cmd_t st7701_dsi_init_operations[] = {
    {0xFF, (uint8_t[]){0x77, 0x01, 0x00, 0x00, 0x13}, 5, 0},
    {0xEF, (uint8_t[]){0x08}, 1, 0},
    {0xFF, (uint8_t[]){0x77, 0x01, 0x00, 0x00, 0x10}, 5, 0},
    {0xC0, (uint8_t[]){0x63, 0x00}, 2, 0},
    {0xC1, (uint8_t[]){0x0D, 0x02}, 2, 0},
    {0xC2, (uint8_t[]){0x10, 0x08}, 2, 0},
    {0xCC, (uint8_t[]){0x10}, 1, 0},
    {0xB0, (uint8_t[]){0x80, 0x09, 0x53, 0x0C, 0xD0, 0x07, 0x0C, 0x09, 0x09, 0x28, 0x06, 0xD4, 0x13, 0x69, 0x2B, 0x71}, 16, 0},
    {0xB1, (uint8_t[]){0x80, 0x94, 0x5A, 0x10, 0xD3, 0x06, 0x0A, 0x08, 0x08, 0x25, 0x03, 0xD3, 0x12, 0x66, 0x6A, 0x0D}, 16, 0},
    {0xFF, (uint8_t[]){0x77, 0x01, 0x00, 0x00, 0x11}, 5, 0},
    {0xB0, (uint8_t[]){0x5D}, 1, 0},
    {0xB1, (uint8_t[]){0x58}, 1, 0},
    {0xB2, (uint8_t[]){0x87}, 1, 0},
    {0xB3, (uint8_t[]){0x80}, 1, 0},
    {0xB5, (uint8_t[]){0x4E}, 1, 0},
    {0xB7, (uint8_t[]){0x85}, 1, 0},
    {0xB8, (uint8_t[]){0x21}, 1, 0},
    {0xB9, (uint8_t[]){0x10, 0x1F}, 2, 0},
    {0xBB, (uint8_t[]){0x03}, 1, 0},
    {0xBC, (uint8_t[]){0x00}, 1, 0},
    {0xC1, (uint8_t[]){0x78}, 1, 0},
    {0xC2, (uint8_t[]){0x78}, 1, 0},
    {0xD0, (uint8_t[]){0x88}, 1, 0},
    {0xE0, (uint8_t[]){0x00, 0x3A, 0x02}, 3, 0},
    {0xE1, (uint8_t[]){0x04, 0xA0, 0x00, 0xA0, 0x05, 0xA0, 0x00, 0xA0, 0x00, 0x40, 0x40}, 11, 0},
    {0xE2, (uint8_t[]){0x30, 0x00, 0x40, 0x40, 0x32, 0xA0, 0x00, 0xA0, 0x00, 0xA0, 0x00, 0xA0, 0x00}, 13, 0},
    {0xE3, (uint8_t[]){0x00, 0x00, 0x33, 0x33}, 4, 0},
    {0xE4, (uint8_t[]){0x44, 0x44}, 2, 0},
    {0xE5, (uint8_t[]){0x09, 0x2E, 0xA0, 0xA0, 0x0B, 0x30, 0xA0, 0xA0, 0x05, 0x2A, 0xA0, 0xA0, 0x07, 0x2C, 0xA0, 0xA0}, 16, 0},
    {0xE6, (uint8_t[]){0x00, 0x00, 0x33, 0x33}, 4, 0},
    {0xE7, (uint8_t[]){0x44, 0x44}, 2, 0},
    {0xE8, (uint8_t[]){0x08, 0x2D, 0xA0, 0xA0, 0x0A, 0x2F, 0xA0, 0xA0, 0x04, 0x29, 0xA0, 0xA0, 0x06, 0x2B, 0xA0, 0xA0}, 16, 0},
    {0xEB, (uint8_t[]){0x00, 0x00, 0x4E, 0x4E, 0x00, 0x00, 0x00}, 7, 0},
    {0xEC, (uint8_t[]){0x08, 0x01}, 2, 0},
    {0xED, (uint8_t[]){0xB0, 0x2B, 0x98, 0xA4, 0x56, 0x7F, 0xFF, 0xFF, 0xFF, 0xFF, 0xF7, 0x65, 0x4A, 0x89, 0xB2, 0x0B}, 16, 0},
    {0xEF, (uint8_t[]){0x08, 0x08, 0x08, 0x45, 0x3F, 0x54}, 6, 0},
    {0xFF, (uint8_t[]){0x77, 0x01, 0x00, 0x00, 0x00}, 5, 0},
    {0x11, (uint8_t[]){0x00}, 0, 120},   // Sleep Out + 120 ms delay
    {0x29, (uint8_t[]){0x00}, 0, 20},    // Display On + 20 ms delay
};

// ============================================================================
// ST7701_DSI_Driver — subclass overrides
// ============================================================================

const mipi_dsi_init_cmd_t* ST7701_DSI_Driver::getInitCommands() const {
    return st7701_dsi_init_operations;
}

size_t ST7701_DSI_Driver::getInitCommandCount() const {
    return sizeof(st7701_dsi_init_operations) / sizeof(st7701_dsi_init_operations[0]);
}

const char* ST7701_DSI_Driver::getLogTag() const {
    return "ST7701DSI";
}

MipiDsiTimingConfig ST7701_DSI_Driver::getTimingConfig() const {
    return {
        .dpi_clock_hz = ST7701_DSI_DPI_CLK_HZ,
        .lane_bit_rate_mbps = ST7701_DSI_LANE_BIT_RATE,
        .hsync_pulse_width = ST7701_DSI_HSYNC_PULSE_WIDTH,
        .hsync_back_porch = ST7701_DSI_HSYNC_BACK_PORCH,
        .hsync_front_porch = ST7701_DSI_HSYNC_FRONT_PORCH,
        .vsync_pulse_width = ST7701_DSI_VSYNC_PULSE_WIDTH,
        .vsync_back_porch = ST7701_DSI_VSYNC_BACK_PORCH,
        .vsync_front_porch = ST7701_DSI_VSYNC_FRONT_PORCH,
        .disable_lp = false, // ST7701S expects LP signaling during blanking
    };
}
