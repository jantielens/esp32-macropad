/*
 * ST7701 RGB Display Driver Implementation
 * 
 * Delegates panel management entirely to Arduino_GFX library:
 *   Arduino_SWSPI → Arduino_ESP32RGBPanel → Arduino_RGB_Display
 * 
 * This matches the exact code path used by proven working samples
 * (e.g., esp32-4848s040-st7701). Key design decisions:
 * 
 * - Arduino_GFX creates and manages the ESP-IDF RGB panel internally
 * - Single PSRAM framebuffer (Arduino_GFX default: num_fbs=1)
 * - Optional bounce buffer in internal SRAM (see ST7701_BOUNCE_BUFFER_LINES)
 *   Currently disabled (0) — stable without it at 8 MHz PCLK.
 *   Can be re-enabled (e.g. 40 lines) if PSRAM contention causes flicker.
 * - auto_flush=true: Cache_WriteBack_Addr called after every draw
 * - bb_invalidate_cache=false (Arduino_GFX default): no ISR cache sync
 *   — not needed because auto_flush writes back before next bounce fill
 * - No VSYNC restart callback (Arduino_GFX doesn't use one)
 * - No custom esp_lcd_panel configuration — Arduino_GFX handles it all
 * 
 * LVGL flush: setAddrWindow(x,y,w,h) → pushColors(data,len)
 *   → gfx->draw16bitRGBBitmap(x, y, data, w, h)
 *   → pixel copy to framebuffer + Cache_WriteBack_Addr
 * 
 * Critical timing parameters (from verified hardware testing):
 * - PCLK: 6 MHz default (board-overridable via ST7701_PCLK_HZ)
 * - HSYNC: polarity=1, front=10, width=8, back=50
 * - VSYNC: polarity=1, front=10, width=8, back=20
 * 
 * Init sequence: Adapted from verified GUITION ESP32-4848S040 sample
 */

#include "st7701_rgb_driver.h"
#include "../log_manager.h"

// PCLK: Pixel clock frequency in Hz.
// Lower values reduce PSRAM bandwidth pressure but lower the refresh rate.
// Tested stable at 6-8 MHz with WiFi active on ESP32-S3-4848S040.
// Override in board_overrides.h if a specific panel needs a different speed.
#ifndef ST7701_PCLK_HZ
#define ST7701_PCLK_HZ 6000000L
#endif

// Bounce buffer: N lines of pixels in internal SRAM, used by LCD DMA
// instead of reading directly from PSRAM.  This shields scan-out from
// PSRAM bus contention caused by WiFi, flash cache, and CPU traffic.
//
// Set to 0 to disable (DMA reads directly from PSRAM framebuffer).
// Tested stable at 0 with 8 MHz PCLK + WiFi active (Feb 2026).
// If flickering reappears (e.g. with heavier WiFi traffic or higher
// PCLK), re-enable by setting to 40 (~75 KB internal SRAM cost).
//
// Sizing guide (480px wide, 2 bytes/pixel, ESP-IDF allocates 2 buffers):
//   10 lines → 2×9600  = ~19 KB SRAM
//   20 lines → 2×19200 = ~37 KB SRAM
//   40 lines → 2×38400 = ~75 KB SRAM
#ifndef ST7701_BOUNCE_BUFFER_LINES
#define ST7701_BOUNCE_BUFFER_LINES 0
#endif

// ST7701 initialization sequence for GUITION ESP32-4848S040 panel
// This sequence is critical - verified from working hardware
static const uint8_t st7701_type4848C080_init_operations[] = {
		BEGIN_WRITE,
		WRITE_COMMAND_8, 0xFF,
		WRITE_BYTES, 5, 0x77, 0x01, 0x00, 0x00, 0x10,

		WRITE_C8_D16, 0xC0, 0x3B, 0x00,
		WRITE_C8_D16, 0xC1, 0x0D, 0x02,
		WRITE_C8_D16, 0xC2, 0x31, 0x05,
		WRITE_C8_D8, 0xCD, 0x00,

		WRITE_COMMAND_8, 0xB0, // Positive Voltage Gamma Control
		WRITE_BYTES, 16,
		0x00, 0x11, 0x18, 0x0E,
		0x11, 0x06, 0x07, 0x08,
		0x07, 0x22, 0x04, 0x12,
		0x0F, 0xAA, 0x31, 0x18,

		WRITE_COMMAND_8, 0xB1, // Negative Voltage Gamma Control
		WRITE_BYTES, 16,
		0x00, 0x11, 0x19, 0x0E,
		0x12, 0x07, 0x08, 0x08,
		0x08, 0x22, 0x04, 0x11,
		0x11, 0xA9, 0x32, 0x18,

		// PAGE1
		WRITE_COMMAND_8, 0xFF,
		WRITE_BYTES, 5, 0x77, 0x01, 0x00, 0x00, 0x11,

		WRITE_C8_D8, 0xB0, 0x60, // Vop=4.7375v
		WRITE_C8_D8, 0xB1, 0x32, // VCOM=32
		WRITE_C8_D8, 0xB2, 0x07, // VGH=15v
		WRITE_C8_D8, 0xB3, 0x80,
		WRITE_C8_D8, 0xB5, 0x49, // VGL=-10.17v
		WRITE_C8_D8, 0xB7, 0x85,
		WRITE_C8_D8, 0xB8, 0x21, // AVDD=6.6 & AVCL=-4.6
		WRITE_C8_D8, 0xC1, 0x78,
		WRITE_C8_D8, 0xC2, 0x78,

		WRITE_COMMAND_8, 0xE0,
		WRITE_BYTES, 3, 0x00, 0x1B, 0x02,

		WRITE_COMMAND_8, 0xE1,
		WRITE_BYTES, 11,
		0x08, 0xA0, 0x00, 0x00,
		0x07, 0xA0, 0x00, 0x00,
		0x00, 0x44, 0x44,

		WRITE_COMMAND_8, 0xE2,
		WRITE_BYTES, 12,
		0x11, 0x11, 0x44, 0x44,
		0xED, 0xA0, 0x00, 0x00,
		0xEC, 0xA0, 0x00, 0x00,

		WRITE_COMMAND_8, 0xE3,
		WRITE_BYTES, 4, 0x00, 0x00, 0x11, 0x11,

		WRITE_C8_D16, 0xE4, 0x44, 0x44,

		WRITE_COMMAND_8, 0xE5,
		WRITE_BYTES, 16,
		0x0A, 0xE9, 0xD8, 0xA0,
		0x0C, 0xEB, 0xD8, 0xA0,
		0x0E, 0xED, 0xD8, 0xA0,
		0x10, 0xEF, 0xD8, 0xA0,

		WRITE_COMMAND_8, 0xE6,
		WRITE_BYTES, 4, 0x00, 0x00, 0x11, 0x11,

		WRITE_C8_D16, 0xE7, 0x44, 0x44,

		WRITE_COMMAND_8, 0xE8,
		WRITE_BYTES, 16,
		0x09, 0xE8, 0xD8, 0xA0,
		0x0B, 0xEA, 0xD8, 0xA0,
		0x0D, 0xEC, 0xD8, 0xA0,
		0x0F, 0xEE, 0xD8, 0xA0,

		WRITE_COMMAND_8, 0xEB,
		WRITE_BYTES, 7,
		0x02, 0x00, 0xE4, 0xE4,
		0x88, 0x00, 0x40,

		WRITE_C8_D16, 0xEC, 0x3C, 0x00,

		WRITE_COMMAND_8, 0xED,
		WRITE_BYTES, 16,
		0xAB, 0x89, 0x76, 0x54,
		0x02, 0xFF, 0xFF, 0xFF,
		0xFF, 0xFF, 0xFF, 0x20,
		0x45, 0x67, 0x98, 0xBA,

		//-----------VAP & VAN---------------
		WRITE_COMMAND_8, 0xFF,
		WRITE_BYTES, 5, 0x77, 0x01, 0x00, 0x00, 0x13,

		WRITE_C8_D8, 0xE5, 0xE4,

		WRITE_COMMAND_8, 0xFF,
		WRITE_BYTES, 5, 0x77, 0x01, 0x00, 0x00, 0x00,

		WRITE_C8_D8, 0x3A, 0x60, // 0x70 RGB888, 0x60 RGB666, 0x50 RGB565

		DELAY, 10,
		WRITE_COMMAND_8, 0x11, // Sleep Out
		END_WRITE,

		DELAY, 120,

		BEGIN_WRITE,
		WRITE_COMMAND_8, 0x29, // Display On
		END_WRITE
};

// ============================================================================
// ST7701_RGB_Driver HAL Implementation
// ============================================================================

ST7701_RGB_Driver::ST7701_RGB_Driver()
		: bus(nullptr), rgbpanel(nullptr), gfx(nullptr), currentBrightness(100),
			displayWidth(DISPLAY_WIDTH), displayHeight(DISPLAY_HEIGHT), displayRotation(DISPLAY_ROTATION),
			backlightOn(false), flushX(0), flushY(0), flushW(0), flushH(0) {
}

ST7701_RGB_Driver::~ST7701_RGB_Driver() {
	// Arduino_GFX objects are allocated with new but not deleted in typical
	// Arduino lifecycle.  Cleaning up in case of dynamic driver management.
	if (gfx) { delete gfx; gfx = nullptr; }
	if (rgbpanel) { delete rgbpanel; rgbpanel = nullptr; }
	if (bus) { delete bus; bus = nullptr; }
}

void ST7701_RGB_Driver::init() {
	// ----------------------------------------------------------------
	// Attach backlight PWM BEFORE the LCD panel starts.
	// Why: ledcAttach reconfigures the GPIO matrix.  If done while the
	// RGB LCD DMA is already scanning, the momentary pin glitch causes
	// the panel's timing controller to lose sync (visible as a brief
	// horizontal/vertical shift).  Configuring PWM first means the pin
	// is already in its final state when the LCD peripheral starts.
	//
	// PWM parameters are board-configurable via board_overrides.h:
	//   - TFT_BACKLIGHT_PWM_FREQ: frequency in Hz (trade-off between
	//     coil whine at low freq vs narrow dimming range at high freq)
	//   - TFT_BACKLIGHT_PWM_CHANNEL: LEDC channel (use a high channel
	//     to avoid collision with LCD-internal timers)
	//   - TFT_BACKLIGHT_DUTY_MIN/MAX: usable duty range for dimming
	//   - Duty starts at 0 (backlight OFF during LCD init)
	//   - At runtime only ledcWrite() is called (single register write,
	//     no GPIO matrix reconfiguration)
	// ----------------------------------------------------------------
	#ifdef LCD_BL_PIN
	#if HAS_BACKLIGHT
	// PWM brightness control
	#if ESP_ARDUINO_VERSION_MAJOR >= 3
	ledcAttachChannel(LCD_BL_PIN, TFT_BACKLIGHT_PWM_FREQ, 8, TFT_BACKLIGHT_PWM_CHANNEL);
	ledcWrite(LCD_BL_PIN, 0);  // Start OFF
	LOGI("ST7701", "Backlight PWM: GPIO%d, %dHz, 8-bit, ch%d (OFF)",
			LCD_BL_PIN, TFT_BACKLIGHT_PWM_FREQ, TFT_BACKLIGHT_PWM_CHANNEL);
	#else
	ledcSetup(TFT_BACKLIGHT_PWM_CHANNEL, TFT_BACKLIGHT_PWM_FREQ, 8);
	ledcAttachPin(LCD_BL_PIN, TFT_BACKLIGHT_PWM_CHANNEL);
	ledcWrite(TFT_BACKLIGHT_PWM_CHANNEL, 0);  // Start OFF
	LOGI("ST7701", "Backlight PWM: GPIO%d, %dHz, 8-bit, ch%d (OFF)",
			LCD_BL_PIN, TFT_BACKLIGHT_PWM_FREQ, TFT_BACKLIGHT_PWM_CHANNEL);
	#endif
	#else
	// Simple on/off — no PWM
	pinMode(LCD_BL_PIN, OUTPUT);
	#ifdef TFT_BACKLIGHT_ON
	digitalWrite(LCD_BL_PIN, !TFT_BACKLIGHT_ON);  // Start OFF
	#else
	digitalWrite(LCD_BL_PIN, LOW);  // Start OFF
	#endif
	LOGI("ST7701", "Backlight digital: GPIO%d (OFF)", LCD_BL_PIN);
	#endif
	#endif
	
	// ----------------------------------------------------------------
	// Create Arduino_GFX display stack (same approach as working sample)
	// ----------------------------------------------------------------
	
	LOGI("ST7701", "Creating Arduino_GFX display stack");

	// 1. 9-bit SPI bus for ST7701 initialization commands
	#if defined(LCD_CS_PIN) && defined(LCD_SCK_PIN) && defined(LCD_MOSI_PIN)
	bus = new Arduino_SWSPI(
			GFX_NOT_DEFINED,  // DC (not used for 9-bit SPI)
			LCD_CS_PIN,       // CS
			LCD_SCK_PIN,      // SCK
			LCD_MOSI_PIN,     // MOSI
			GFX_NOT_DEFINED   // MISO (not used)
	);
	LOGI("ST7701", "SPI bus: CS=%d, SCK=%d, MOSI=%d", LCD_CS_PIN, LCD_SCK_PIN, LCD_MOSI_PIN);
	#else
	LOGE("ST7701", "SPI pins not defined in board_config.h");
	return;
	#endif

	// 2. RGB parallel panel bus
	//    Bounce buffer (last param) shields LCD DMA from PSRAM contention.
	//    Without it, WiFi PSRAM access causes horizontal shift artifacts.
	const size_t bounce_px = (size_t)displayWidth * ST7701_BOUNCE_BUFFER_LINES;
	rgbpanel = new Arduino_ESP32RGBPanel(
			LCD_DE_PIN, LCD_VSYNC_PIN, LCD_HSYNC_PIN, LCD_PCLK_PIN,
			LCD_R0_PIN, LCD_R1_PIN, LCD_R2_PIN, LCD_R3_PIN, LCD_R4_PIN,
			LCD_G0_PIN, LCD_G1_PIN, LCD_G2_PIN, LCD_G3_PIN, LCD_G4_PIN, LCD_G5_PIN,
			LCD_B0_PIN, LCD_B1_PIN, LCD_B2_PIN, LCD_B3_PIN, LCD_B4_PIN,
			LCD_HSYNC_POLARITY, LCD_HSYNC_FRONT_PORCH, LCD_HSYNC_PULSE_WIDTH, LCD_HSYNC_BACK_PORCH,
			LCD_VSYNC_POLARITY, LCD_VSYNC_FRONT_PORCH, LCD_VSYNC_PULSE_WIDTH, LCD_VSYNC_BACK_PORCH,
			0,                 // pclk_active_neg (normal polarity)
			ST7701_PCLK_HZ,    // prefer_speed (board-overridable)
			false,             // useBigEndian
			0,                 // de_idle_high
			0,                 // pclk_idle_high
			bounce_px          // bounce_buffer_size_px
	);
	LOGI("ST7701", "RGB panel: HSYNC=%d/%d/%d, VSYNC=%d/%d/%d, bounce=%d lines (%d px)",
			LCD_HSYNC_FRONT_PORCH, LCD_HSYNC_PULSE_WIDTH, LCD_HSYNC_BACK_PORCH,
			LCD_VSYNC_FRONT_PORCH, LCD_VSYNC_PULSE_WIDTH, LCD_VSYNC_BACK_PORCH,
			ST7701_BOUNCE_BUFFER_LINES, (int)bounce_px);

	// 3. Display with ST7701 init sequence + auto_flush
	//    auto_flush=true: Cache_WriteBack_Addr called after every draw16bitRGBBitmap
	gfx = new Arduino_RGB_Display(
			displayWidth,      // width
			displayHeight,     // height
			rgbpanel,          // RGB panel bus
			0,                 // rotation
			true,              // auto_flush (cache writeback after each draw)
			bus,               // SPI bus for ST7701 init commands
			GFX_NOT_DEFINED,   // RST pin (use software reset)
			st7701_type4848C080_init_operations,
			sizeof(st7701_type4848C080_init_operations)
	);

	if (!gfx->begin()) {
		LOGE("ST7701", "Arduino_GFX begin() failed");
		return;
	}

	LOGI("ST7701", "Arduino_GFX initialized: %dx%d, auto_flush=true, PCLK=%ldHz, bounce=%d lines",
			displayWidth, displayHeight, (long)ST7701_PCLK_HZ, ST7701_BOUNCE_BUFFER_LINES);
	
	delay(50);  // Brief delay before enabling backlight
	setBacklightBrightness(currentBrightness);
	
	LOGI("ST7701", "Display ready: %dx%d @ rotation %d", 
			width(), height(), displayRotation);
}

void ST7701_RGB_Driver::setRotation(uint8_t rotation) {
		displayRotation = rotation;
}

int ST7701_RGB_Driver::width() {
		if (displayRotation == 1 || displayRotation == 3) {
				return displayHeight;
		}
		return displayWidth;
}

int ST7701_RGB_Driver::height() {
		if (displayRotation == 1 || displayRotation == 3) {
				return displayWidth;
		}
		return displayHeight;
}

void ST7701_RGB_Driver::setBacklight(bool on) {
		setBacklightBrightness(on ? 100 : 0);
}

void ST7701_RGB_Driver::setBacklightBrightness(uint8_t brightness) {
		if (brightness > 100) brightness = 100;
		currentBrightness = brightness;
		backlightOn = (brightness > 0);

		#ifdef LCD_BL_PIN
		#if HAS_BACKLIGHT
		// Remap brightness to usable hardware duty range.
		// MOSFET dimming range varies by board/frequency — configured via
		// TFT_BACKLIGHT_DUTY_MIN/MAX in board_overrides.h.
		// Map: 0% → 0 (off), 1-99% → DUTY_MIN..DUTY_MAX (visible dimming),
		//       100% → 255 (constant DC, maximum brightness)
		uint32_t duty = 0;
		if (brightness >= 100) {
			duty = 255;
		} else if (brightness > 0) {
			duty = TFT_BACKLIGHT_DUTY_MIN + ((uint32_t)(brightness - 1) * (TFT_BACKLIGHT_DUTY_MAX - TFT_BACKLIGHT_DUTY_MIN)) / 98;  // DUTY_MIN..DUTY_MAX
		}
		#ifdef TFT_BACKLIGHT_ON
		if (!TFT_BACKLIGHT_ON) duty = 255 - duty;  // Invert for active-low
		#endif
		#if ESP_ARDUINO_VERSION_MAJOR >= 3
		ledcWrite(LCD_BL_PIN, duty);
		#else
		ledcWrite(TFT_BACKLIGHT_PWM_CHANNEL, duty);
		#endif
		#else
		// Digital on/off fallback
		#ifdef TFT_BACKLIGHT_ON
		digitalWrite(LCD_BL_PIN, backlightOn ? TFT_BACKLIGHT_ON : !TFT_BACKLIGHT_ON);
		#else
		digitalWrite(LCD_BL_PIN, backlightOn ? HIGH : LOW);
		#endif
		#endif
		#endif
}

uint8_t ST7701_RGB_Driver::getBacklightBrightness() {
		return currentBrightness;
}

bool ST7701_RGB_Driver::hasBacklightControl() {
		#ifdef LCD_BL_PIN
		return true;
		#else
		return false;
		#endif
}

void ST7701_RGB_Driver::applyDisplayFixes() {
		// No specific fixes needed for ST7701 RGB
}

void ST7701_RGB_Driver::startWrite() {
		// Not needed — draw16bitRGBBitmap handles everything
}

void ST7701_RGB_Driver::endWrite() {
		// Not needed — draw16bitRGBBitmap handles everything
}

void ST7701_RGB_Driver::setAddrWindow(int16_t x, int16_t y, uint16_t w, uint16_t h) {
		// Save the area for pushColors()
		flushX = x;
		flushY = y;
		flushW = w;
		flushH = h;
}

void ST7701_RGB_Driver::pushColors(uint16_t* data, uint32_t len, bool swap_bytes) {
	// Delegate to Arduino_GFX's draw16bitRGBBitmap — the proven working path.
	// Internally this:
	//   1. Copies pixels from LVGL buffer → PSRAM framebuffer (via gfx_draw_bitmap_to_framebuffer)
	//   2. Calls Cache_WriteBack_Addr to flush dirty cache lines (auto_flush=true)
	//   3. Bounce buffer DMA reads clean data from PSRAM → SRAM → LCD
	if (!gfx || !data || flushW == 0 || flushH == 0) {
		return;
	}

	gfx->draw16bitRGBBitmap(flushX, flushY, data, flushW, flushH);
}

