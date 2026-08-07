#ifndef BOARD_OVERRIDES_JC1060P470C_H
#define BOARD_OVERRIDES_JC1060P470C_H

// ============================================================================
// GUITION JC1060P470C Board Configuration Overrides
// ============================================================================
// Hardware: ESP32-P4 (dual RISC-V 360 MHz) + JD9165 MIPI-DSI 1024x600 + GT911 touch
// WiFi: External ESP32-C6 co-processor over SDIO (no onboard Bluetooth)
// Display: 7-inch IPS, HKC QD070AS01-1 panel with JD9165 driver IC
// Reference: https://github.com/cheops/JC1060P470C_I_W

// ============================================================================
// Capabilities
// ============================================================================
#define HAS_DISPLAY true
#define HAS_TOUCH true
#define HAS_BACKLIGHT true
// Pin LVGL render task to Core 1 (Core 0 handles WiFi SDIO + system tasks)
#define LVGL_TASK_CORE 1
// Measured under portal, MQTT, and pad load: 4704 bytes remained from 8192.
#define LOOP_TASK_STACK_SIZE 6144
// Measured under portal load: 11680 bytes remained from the 16 KB default.
#define CONFIG_ASYNC_TCP_STACK_SIZE 8192
// minimp3's per-frame decode scratch is roughly 16 KB. Keep it in PSRAM so
// Music playback fits the internal audio task stack.
#define AUDIO_MP3_SCRATCH_PSRAM true

// ============================================================================
// Driver Selection (HAL)
// ============================================================================
#define DISPLAY_DRIVER DISPLAY_DRIVER_JD9165_DSI
// Panel IC name string (used by tools/generate-board-driver-table.py for the board→driver table).
#define DISPLAY_PANEL "JD9165"
#define TOUCH_DRIVER TOUCH_DRIVER_GT911

// ============================================================================
// Pad Layout
// ============================================================================
#define DISPLAY_SHAPE DISPLAY_SHAPE_RECT
#define UI_SCALE_TIER UI_SCALE_LARGE

// ============================================================================
// Display geometry
// ============================================================================
#define DISPLAY_WIDTH 1024
#define DISPLAY_HEIGHT 600
#define DISPLAY_ROTATION 1    // Portrait: 90° CW software rotation in flush path

// LVGL draw buffer: PSRAM is fine — DMA2D handles the copy to the framebuffer.
// 80 rows × 600 px ≈ 96 KB per buffer. 120 rows pushed per-flush DMA bursts
// to ~144 KB, increasing PSRAM bandwidth contention with the DPI scanout and
// the image-fetch task. 80 rows is the same value the other P4 boards use.
#define LVGL_BUFFER_PREFER_INTERNAL false
#define LVGL_BUFFER_SIZE (DISPLAY_HEIGHT * 80)  // portrait logical width × 80 rows
#define LVGL_DRAW_BUF_COUNT 2                   // double-buffer: overlap render + flush

// LVGL refresh period — 15 ms (~66 fps target).
#define LVGL_REFR_PERIOD_MS 15

// ============================================================================
// Backlight (LEDC) — Active-High
// ============================================================================
#define LCD_BL_PIN 23
#define TFT_BACKLIGHT_ON HIGH  // Active-high backlight
#define TFT_BACKLIGHT_PWM_CHANNEL 0
#define TFT_BACKLIGHT_PWM_FREQ 1000

// ============================================================================
// Panel Reset
// ============================================================================
#define LCD_RST_PIN 27         // From BSP pin definitions

// ============================================================================
// DSI Timing (from JC1060P470C BSP, HKC 7.0" IPS panel dtsi)
// ============================================================================
// MIPI-DSI: 2-lane, 550 Mbps/lane, 51.2 MHz DPI clock
// Defaults in board_config.h match JC1060P470C — no overrides needed.
// Uncomment only if tuning is required:
// #define JD9165_DSI_DPI_CLK_HZ        51200000L
// #define JD9165_DSI_LANE_BIT_RATE      550
// #define JD9165_DSI_HSYNC_PULSE_WIDTH  24
// #define JD9165_DSI_HSYNC_BACK_PORCH   136
// #define JD9165_DSI_HSYNC_FRONT_PORCH  160
// #define JD9165_DSI_VSYNC_PULSE_WIDTH  2
// #define JD9165_DSI_VSYNC_BACK_PORCH   21
// #define JD9165_DSI_VSYNC_FRONT_PORCH  12

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
#define TOUCH_RST 22            // Touch reset pin (from BSP)
#define TOUCH_INT 21            // Touch interrupt pin (from BSP)

// ============================================================================
// Audio (ES8311 codec over I2S, I2C control on shared bus 0)
// Pin mapping from BSP: https://github.com/cheops/JC1060P470C_I_W
// ============================================================================
#define HAS_AUDIO true
#define HAS_MUSIC_ANALYSIS true
#define AUDIO_OUTPUT_DRIVER AUDIO_OUTPUT_DRIVER_ES8311
#define AUDIO_I2S_MCLK   13    // ES8311 MCLK (master clock)
#define AUDIO_I2S_BCLK   12    // ES8311 SCLK (bit clock)
#define AUDIO_I2S_LRCK   10    // ES8311 LRCK (word select)
#define AUDIO_I2S_DOUT    9    // ESP32 TX → ES8311 DSDIN (codec DAC input)
#define AUDIO_I2S_DIN    20    // ES8311 data out (mic path, if connected)
#define AUDIO_PA_PIN     11    // NS4150B power amplifier enable (active-HIGH, confirmed by community)
#define AUDIO_CODEC_ADDR 0x18  // ES8311 I2C address (shared Wire bus 0)

// ============================================================================
// Hardware Button (onboard SW1 / BOOT)
// ============================================================================
// SW1 is the BOOT strapping button on GPIO35 (active-low, internal pull-up).
// Usable as a runtime action button — only matters at reset (holding it low
// at power-up enters serial download mode).
#define HAS_BUTTON true
// BUTTON_PIN / BUTTON_ACTIVE_LOW drive boot-hold config-mode detection in
// check_config_mode_button(); keep them aligned with HW_BUTTON_DEFS[0] below
// (which drives the runtime tap/hold action dispatcher).
#define BUTTON_PIN 35
#define BUTTON_ACTIVE_LOW true
#define NUM_HW_BUTTONS 1
#ifdef __cplusplus
static constexpr HwButtonDef HW_BUTTON_DEFS[NUM_HW_BUTTONS] = {
    { .pin = 35, .active_low = true, .label = "BOOT" },
};
#endif
// ============================================================================
// Advanced Tuning (MIPI-DSI specific)
// ============================================================================
// Hide PSRAM flicker when screensaver fades (DPI FB lives in PSRAM).
#define DISPLAY_BLANK_ON_SAVE true
// Screensaver sleep strategy for the JD9165 + HKC IPS panel. Holding the panel
// in hardware reset all night (DISPLAY_HARD_RESET_ON_SLEEP) fixed the washed-out
// colors but introduced morning flicker: the panel woke from hours of full
// power-down into a marginal VCOM/MIPI-lock state that took minutes to settle,
// and an active LC de-bias + soft-landing power-down did not resolve it.
// Instead, keep the panel fully powered (Display On, scanning all-black with
// frame inversion, backlight at 0) for the whole sleep. That avoids the DC bias
// of DCS Sleep In *and* the hard-reset power-cycling, so wake is a clean
// backlight fade with no panel re-init. Costs extra idle power (mains-powered
// device, acceptable). Mutually exclusive with DISPLAY_HARD_RESET_ON_SLEEP.
#define DISPLAY_KEEP_PANEL_AWAKE_ON_SLEEP true
#define DISPLAY_HARD_RESET_ON_SLEEP false
// Avoid PSRAM bus contention — disable background task telemetry.
#define DEVICE_TELEMETRY_BACKGROUND_TASKS 0
#define DEVICE_TELEMETRY_CPU_MONITOR 1
#define DEVICE_TELEMETRY_HEALTH_WINDOW 1

#endif // BOARD_OVERRIDES_JC1060P470C_H
