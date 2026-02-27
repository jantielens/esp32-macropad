/*
 * MIPI-DSI Display Driver Base Implementation (Direct ESP-IDF)
 * 
 * Shared implementation for all MIPI-DSI panel drivers on ESP32-P4.
 * Calls ESP-IDF MIPI-DSI APIs directly for:
 *   - DMA2D async flush (hardware-accelerated pixel copy)
 *   - disable_lp=true (continuous HS mode during blanking)
 *   - Backlight PWM control
 *   - Panel reset and initialization
 * 
 * Subclasses only need to provide their vendor init commands and DSI timing.
 */

#include "mipi_dsi_driver.h"
#include "../log_manager.h"
#include <string.h>

// MIPI DSI PHY power supply (LDO channel 3 on ESP32-P4)
#define MIPI_DSI_PHY_PWR_LDO_CHAN       3
#define MIPI_DSI_PHY_PWR_LDO_VOLTAGE_MV 2500

// DMA2D completion callback — called from ISR when draw_bitmap finishes
// copying the LVGL buffer into the DPI framebuffer.  Signals LVGL that
// the draw buffer can be recycled.
static bool IRAM_ATTR onColorTransDone(esp_lcd_panel_handle_t panel,
                                       esp_lcd_dpi_panel_event_data_t* edata,
                                       void* user_ctx) {
    lv_display_t* disp = (lv_display_t*)user_ctx;
    lv_display_flush_ready(disp);
    return false;
}

MipiDsiDriver::MipiDsiDriver()
    : framebuffer(nullptr), panel_handle(nullptr), currentBrightness(100),
      displayWidth(DISPLAY_WIDTH), displayHeight(DISPLAY_HEIGHT), displayRotation(DISPLAY_ROTATION),
      backlightOn(false), flushX(0), flushY(0), flushW(0), flushH(0), lvglDisplay(nullptr) {
}

MipiDsiDriver::~MipiDsiDriver() {
    // Display is never torn down during normal operation.
}

void MipiDsiDriver::init() {
    const char* tag = getLogTag();
    MipiDsiTimingConfig timing = getTimingConfig();

    // ----------------------------------------------------------------
    // Attach backlight PWM BEFORE the LCD panel starts.
    // Configuring PWM first avoids GPIO glitches during panel scan start.
    // ----------------------------------------------------------------
    #ifdef LCD_BL_PIN
    #if HAS_BACKLIGHT
    #if ESP_ARDUINO_VERSION_MAJOR >= 3
    ledcAttachChannel(LCD_BL_PIN, TFT_BACKLIGHT_PWM_FREQ, 8, TFT_BACKLIGHT_PWM_CHANNEL);
    ledcWrite(LCD_BL_PIN, 0);
    LOGI(tag, "Backlight PWM: GPIO%d, %dHz, 8-bit, ch%d (OFF)",
         LCD_BL_PIN, TFT_BACKLIGHT_PWM_FREQ, TFT_BACKLIGHT_PWM_CHANNEL);
    #else
    ledcSetup(TFT_BACKLIGHT_PWM_CHANNEL, TFT_BACKLIGHT_PWM_FREQ, 8);
    ledcAttachPin(LCD_BL_PIN, TFT_BACKLIGHT_PWM_CHANNEL);
    ledcWrite(TFT_BACKLIGHT_PWM_CHANNEL, 0);
    LOGI(tag, "Backlight PWM: GPIO%d, %dHz, 8-bit, ch%d (OFF)",
         LCD_BL_PIN, TFT_BACKLIGHT_PWM_FREQ, TFT_BACKLIGHT_PWM_CHANNEL);
    #endif
    #else
    pinMode(LCD_BL_PIN, OUTPUT);
    #ifdef TFT_BACKLIGHT_ON
    digitalWrite(LCD_BL_PIN, !TFT_BACKLIGHT_ON);
    #else
    digitalWrite(LCD_BL_PIN, LOW);
    #endif
    LOGI(tag, "Backlight digital: GPIO%d (OFF)", LCD_BL_PIN);
    #endif
    #endif
    
    // ----------------------------------------------------------------
    // Panel hardware reset
    // ----------------------------------------------------------------
    #ifdef LCD_RST_PIN
    pinMode(LCD_RST_PIN, OUTPUT);
    digitalWrite(LCD_RST_PIN, HIGH);
    delay(5);
    digitalWrite(LCD_RST_PIN, LOW);
    delay(10);
    digitalWrite(LCD_RST_PIN, HIGH);
    delay(120);
    LOGI(tag, "Panel reset: GPIO%d (HIGH-LOW-HIGH, 120ms)", LCD_RST_PIN);
    #endif
    
    // ----------------------------------------------------------------
    // Direct ESP-IDF MIPI-DSI initialization
    // ----------------------------------------------------------------
    
    LOGI(tag, "Initializing MIPI-DSI via direct ESP-IDF calls");
    LOGI(tag, "DSI timing: DPI_CLK=%luHz, lane_rate=%lu Mbps",
         (unsigned long)timing.dpi_clock_hz, (unsigned long)timing.lane_bit_rate_mbps);
    LOGI(tag, "DSI HSYNC: pw=%lu, bp=%lu, fp=%lu",
         (unsigned long)timing.hsync_pulse_width, (unsigned long)timing.hsync_back_porch,
         (unsigned long)timing.hsync_front_porch);
    LOGI(tag, "DSI VSYNC: pw=%lu, bp=%lu, fp=%lu",
         (unsigned long)timing.vsync_pulse_width, (unsigned long)timing.vsync_back_porch,
         (unsigned long)timing.vsync_front_porch);
    LOGI(tag, "DSI flags: disable_lp=%s, use_dma2d=true",
         timing.disable_lp ? "true" : "false");
    
    // 1. Power on MIPI DSI PHY via internal LDO (LDO_VO3 → VDD_MIPI_DPHY)
    esp_ldo_channel_handle_t ldo_mipi_phy = NULL;
    esp_ldo_channel_config_t ldo_config = {
        .chan_id = MIPI_DSI_PHY_PWR_LDO_CHAN,
        .voltage_mv = MIPI_DSI_PHY_PWR_LDO_VOLTAGE_MV,
    };
    ESP_ERROR_CHECK(esp_ldo_acquire_channel(&ldo_config, &ldo_mipi_phy));
    LOGI(tag, "MIPI DSI PHY powered on (LDO ch%d, %dmV)",
         MIPI_DSI_PHY_PWR_LDO_CHAN, MIPI_DSI_PHY_PWR_LDO_VOLTAGE_MV);
    
    // 2. Create DSI bus (2-lane, configured bit rate)
    esp_lcd_dsi_bus_config_t bus_config = {
        .bus_id = 0,
        .num_data_lanes = 2,
        .phy_clk_src = MIPI_DSI_PHY_CLK_SRC_DEFAULT,
        .lane_bit_rate_mbps = timing.lane_bit_rate_mbps,
    };
    esp_lcd_dsi_bus_handle_t mipi_dsi_bus = NULL;
    ESP_ERROR_CHECK(esp_lcd_new_dsi_bus(&bus_config, &mipi_dsi_bus));
    
    // 3. Create DBI IO for DSI command channel (vendor init commands)
    esp_lcd_dbi_io_config_t dbi_config = {
        .virtual_channel = 0,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    esp_lcd_panel_io_handle_t io_handle = NULL;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_dbi(mipi_dsi_bus, &dbi_config, &io_handle));
    
    // 4. Create DPI panel with disable_lp=true and use_dma2d=true
    //    disable_lp: keeps D-PHY in HS mode during blanking (avoids flicker)
    //    use_dma2d: hardware-accelerated async pixel copy (avoids blocking CPU)
    esp_lcd_dpi_panel_config_t dpi_config = {
        .virtual_channel = 0,
        .dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT,
        .dpi_clock_freq_mhz = (uint32_t)(timing.dpi_clock_hz / 1000000),
        .pixel_format = LCD_COLOR_PIXEL_FORMAT_RGB565,
        .num_fbs = 1,
        .video_timing = {
            .h_size = displayWidth,
            .v_size = displayHeight,
            .hsync_pulse_width = timing.hsync_pulse_width,
            .hsync_back_porch = timing.hsync_back_porch,
            .hsync_front_porch = timing.hsync_front_porch,
            .vsync_pulse_width = timing.vsync_pulse_width,
            .vsync_back_porch = timing.vsync_back_porch,
            .vsync_front_porch = timing.vsync_front_porch,
        },
        .flags = {
            .use_dma2d = true,
            .disable_lp = timing.disable_lp,
        },
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_dpi(mipi_dsi_bus, &dpi_config, &panel_handle));
    
    // 5. Send vendor init commands via DSI command mode
    const mipi_dsi_init_cmd_t* cmds = getInitCommands();
    const size_t num_cmds = getInitCommandCount();
    for (size_t i = 0; i < num_cmds; i++) {
        ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(
            io_handle, cmds[i].cmd, cmds[i].data, cmds[i].data_bytes
        ));
        if (cmds[i].delay_ms > 0) {
            delay(cmds[i].delay_ms);
        }
    }
    LOGI(tag, "Sent %d vendor init commands", (int)num_cmds);
    
    // 6. Initialize DPI panel — starts continuous DMA refresh from PSRAM
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
    
    // 7. Get PSRAM framebuffer pointer (allocated by ESP-IDF during panel creation)
    void* fb = nullptr;
    ESP_ERROR_CHECK(esp_lcd_dpi_panel_get_frame_buffer(panel_handle, 1, &fb));
    framebuffer = (uint16_t*)fb;
    
    LOGI(tag, "DSI initialized: %dx%d, disable_lp=%s, use_dma2d=true, single FB @ %p",
         displayWidth, displayHeight, timing.disable_lp ? "true" : "false", framebuffer);
    
    delay(50);
    setBacklightBrightness(currentBrightness);
    
    LOGI(tag, "Display ready: %dx%d @ rotation %d", width(), height(), displayRotation);
}

void MipiDsiDriver::setRotation(uint8_t rotation) {
    displayRotation = rotation;
}

int MipiDsiDriver::width() {
    return (displayRotation == 1 || displayRotation == 3) ? displayHeight : displayWidth;
}

int MipiDsiDriver::height() {
    return (displayRotation == 1 || displayRotation == 3) ? displayWidth : displayHeight;
}

void MipiDsiDriver::setBacklight(bool on) {
    setBacklightBrightness(on ? 100 : 0);
}

void MipiDsiDriver::setBacklightBrightness(uint8_t brightness) {
    if (brightness > 100) brightness = 100;
    currentBrightness = brightness;
    backlightOn = (brightness > 0);

    #ifdef LCD_BL_PIN
    #if HAS_BACKLIGHT
    uint32_t duty = 0;
    if (brightness >= 100) {
        duty = 255;
    } else if (brightness > 0) {
        duty = TFT_BACKLIGHT_DUTY_MIN + ((uint32_t)(brightness - 1) * (TFT_BACKLIGHT_DUTY_MAX - TFT_BACKLIGHT_DUTY_MIN)) / 98;
    }
    #ifdef TFT_BACKLIGHT_ON
    if (!TFT_BACKLIGHT_ON) duty = 255 - duty;
    #endif
    #if ESP_ARDUINO_VERSION_MAJOR >= 3
    ledcWrite(LCD_BL_PIN, duty);
    #else
    ledcWrite(TFT_BACKLIGHT_PWM_CHANNEL, duty);
    #endif
    #else
    #ifdef TFT_BACKLIGHT_ON
    digitalWrite(LCD_BL_PIN, backlightOn ? TFT_BACKLIGHT_ON : !TFT_BACKLIGHT_ON);
    #else
    digitalWrite(LCD_BL_PIN, backlightOn ? HIGH : LOW);
    #endif
    #endif
    #endif
}

uint8_t MipiDsiDriver::getBacklightBrightness() {
    return currentBrightness;
}

bool MipiDsiDriver::hasBacklightControl() {
    #ifdef LCD_BL_PIN
    return true;
    #else
    return false;
    #endif
}

void MipiDsiDriver::applyDisplayFixes() {
    // No specific fixes needed for MIPI-DSI panels
}

void MipiDsiDriver::startWrite() {
    // No-op: esp_lcd_panel_draw_bitmap() in pushColors() handles everything
}

void MipiDsiDriver::endWrite() {
    // No-op: esp_lcd_panel_draw_bitmap() in pushColors() handles everything
}

void MipiDsiDriver::setAddrWindow(int16_t x, int16_t y, uint16_t w, uint16_t h) {
    flushX = x;
    flushY = y;
    flushW = w;
    flushH = h;
}

void MipiDsiDriver::pushColors(uint16_t* data, uint32_t len, bool swap_bytes) {
    if (!panel_handle || !data || flushW == 0 || flushH == 0) {
        return;
    }
    // DMA2D hardware async copy — coordinates: inclusive start, exclusive end
    esp_lcd_panel_draw_bitmap(panel_handle,
                              flushX, flushY,
                              flushX + flushW, flushY + flushH,
                              data);
}

void MipiDsiDriver::configureLVGL(lv_display_t* disp, uint8_t rotation) {
    lvglDisplay = disp;
    // Register DMA2D completion callback so LVGL knows the draw buffer
    // can be recycled once the hardware copy finishes.
    esp_lcd_dpi_panel_event_callbacks_t cbs = {};
    cbs.on_color_trans_done = onColorTransDone;
    ESP_ERROR_CHECK(esp_lcd_dpi_panel_register_event_callbacks(panel_handle, &cbs, disp));
    LOGI(getLogTag(), "DMA2D flush callback registered");
}

bool MipiDsiDriver::asyncFlush() const {
    return true;
}
