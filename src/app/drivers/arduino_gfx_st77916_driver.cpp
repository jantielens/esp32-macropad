/*
 * Arduino_GFX ST77916 QSPI Display Driver Implementation
 *
 * Wraps Arduino_GFX for the ST77916 QSPI 360x360 round display.
 * Direct rendering: each LVGL flush strip is sent to the panel via
 * draw16bitRGBBitmap().  No PSRAM framebuffer needed because the
 * ST77916 retains its address window across QSPI CS toggles.
 */

#include "arduino_gfx_st77916_driver.h"
#include "../log_manager.h"

#ifndef TFT_SPI_FREQ_HZ
#define TFT_SPI_FREQ_HZ (50 * 1000 * 1000)
#endif

Arduino_GFX_ST77916_Driver::Arduino_GFX_ST77916_Driver()
		: bus(nullptr), gfx(nullptr), currentBrightness(100), backlightPwmAttached(false),
			currentX(0), currentY(0), currentW(0), currentH(0) {
}

Arduino_GFX_ST77916_Driver::~Arduino_GFX_ST77916_Driver() {
		if (gfx) delete gfx;
		if (bus) delete bus;
}

void Arduino_GFX_ST77916_Driver::init() {
		LOGI("GFX_ST77916", "Initializing QSPI display driver");

		// Backlight PWM
		#ifdef LCD_BL_PIN
		pinMode(LCD_BL_PIN, OUTPUT);

		#if HAS_BACKLIGHT
		#if ESP_ARDUINO_VERSION_MAJOR >= 3
		double actualFreq = ledcAttach(LCD_BL_PIN, TFT_BACKLIGHT_PWM_FREQ, 8);
		LOGI("GFX_ST77916", "PWM attached on GPIO%d, actual freq: %.1f Hz", LCD_BL_PIN, actualFreq);
		#else
		ledcSetup(TFT_BACKLIGHT_PWM_CHANNEL, TFT_BACKLIGHT_PWM_FREQ, 8);
		ledcAttachPin(LCD_BL_PIN, TFT_BACKLIGHT_PWM_CHANNEL);
		LOGI("GFX_ST77916", "PWM setup complete on GPIO%d (channel %d)", LCD_BL_PIN, TFT_BACKLIGHT_PWM_CHANNEL);
		#endif
		backlightPwmAttached = true;
		setBacklightBrightness(currentBrightness);
		#else
		#ifdef TFT_BACKLIGHT_ON
		digitalWrite(LCD_BL_PIN, TFT_BACKLIGHT_ON);
		#else
		digitalWrite(LCD_BL_PIN, HIGH);
		#endif
		LOGI("GFX_ST77916", "Backlight enabled on GPIO%d", LCD_BL_PIN);
		#endif
		#endif

		// Create QSPI bus
		#ifdef LCD_QSPI_CS
		bus = new Arduino_ESP32QSPI(
				LCD_QSPI_CS,
				LCD_QSPI_PCLK,
				LCD_QSPI_D0,
				LCD_QSPI_D1,
				LCD_QSPI_D2,
				LCD_QSPI_D3
		);
		LOGI("GFX_ST77916", "QSPI bus created");
		#else
		LOGE("GFX_ST77916", "QSPI pins not defined in board_config.h");
		return;
		#endif

		// Create ST77916 panel.
		// Use st77916_150_init_operations which best matches the known-good
		// vendor init sequence from the JC3636W518-macropad sample.
		// IPS=true enables color inversion (required for this panel).
		gfx = new Arduino_ST77916(
				bus,
				#ifdef LCD_QSPI_RST
				LCD_QSPI_RST,
				#else
				GFX_NOT_DEFINED,
				#endif
				0,     // rotation (managed by LVGL / setRotation override)
				true,  // IPS
				DISPLAY_WIDTH,
				DISPLAY_HEIGHT,
				0, 0, 0, 0,
				st77916_150_init_operations,
				sizeof(st77916_150_init_operations)
		);
		LOGI("GFX_ST77916", "ST77916 panel object created (%dx%d)", DISPLAY_WIDTH, DISPLAY_HEIGHT);

		if (!gfx->begin(TFT_SPI_FREQ_HZ)) {
				LOGE("GFX_ST77916", "Failed to initialize display");
				return;
		}
		LOGI("GFX_ST77916", "Display initialized (direct QSPI, %d MHz)", TFT_SPI_FREQ_HZ / 1000000);

		gfx->fillScreen(RGB565_BLACK);
		LOGI("GFX_ST77916", "Display ready: %dx%d", DISPLAY_WIDTH, DISPLAY_HEIGHT);
}

void Arduino_GFX_ST77916_Driver::setRotation(uint8_t rotation) {
		// Delegate to Arduino_GFX which uses MADCTL for hardware rotation.
		// ST77916 supports MADCTL rotation, unlike AXS15231B.
		if (gfx) {
				gfx->setRotation(rotation);
		}
		LOGI("GFX_ST77916", "Rotation set to %d (hardware MADCTL)", rotation);
}

int Arduino_GFX_ST77916_Driver::width() {
		return (int)DISPLAY_WIDTH;
}

int Arduino_GFX_ST77916_Driver::height() {
		return (int)DISPLAY_HEIGHT;
}

void Arduino_GFX_ST77916_Driver::setBacklight(bool on) {
		#ifdef LCD_BL_PIN
		#if HAS_BACKLIGHT
		setBacklightBrightness(on ? 100 : 0);
		#else
		#ifdef TFT_BACKLIGHT_ON
		digitalWrite(LCD_BL_PIN, on ? TFT_BACKLIGHT_ON : !TFT_BACKLIGHT_ON);
		#else
		digitalWrite(LCD_BL_PIN, on ? HIGH : LOW);
		#endif
		#endif
		#endif
}

void Arduino_GFX_ST77916_Driver::setBacklightBrightness(uint8_t brightness) {
		#ifdef LCD_BL_PIN
		if (brightness > 100) brightness = 100;
		currentBrightness = brightness;

		#if HAS_BACKLIGHT
		uint32_t duty = 0;
		if (brightness >= 100) {
				duty = 255;
		} else if (brightness > 0) {
				duty = TFT_BACKLIGHT_DUTY_MIN + ((uint32_t)(brightness - 1) * (TFT_BACKLIGHT_DUTY_MAX - TFT_BACKLIGHT_DUTY_MIN)) / 98;
		}

		#ifdef TFT_BACKLIGHT_ON
		if (!TFT_BACKLIGHT_ON) {
				duty = 255 - duty;
		}
		#endif

		#if ESP_ARDUINO_VERSION_MAJOR >= 3
		ledcWrite(LCD_BL_PIN, duty);
		#else
		ledcWrite(TFT_BACKLIGHT_PWM_CHANNEL, duty);
		#endif
		#else
		#ifdef TFT_BACKLIGHT_ON
		digitalWrite(LCD_BL_PIN, brightness > 0 ? TFT_BACKLIGHT_ON : !TFT_BACKLIGHT_ON);
		#else
		digitalWrite(LCD_BL_PIN, brightness > 0 ? HIGH : LOW);
		#endif
		#endif
		#endif
}

uint8_t Arduino_GFX_ST77916_Driver::getBacklightBrightness() {
		return currentBrightness;
}

bool Arduino_GFX_ST77916_Driver::hasBacklightControl() {
		#ifdef LCD_BL_PIN
		return true;
		#else
		return false;
		#endif
}

void Arduino_GFX_ST77916_Driver::applyDisplayFixes() {
		// IPS=true in the constructor already handles color inversion.
		// No additional fixes needed for ST77916.
}

void Arduino_GFX_ST77916_Driver::startWrite() {
		// draw16bitRGBBitmap() handles bus transactions internally.
}

void Arduino_GFX_ST77916_Driver::endWrite() {
		// See startWrite() comment.
}

void Arduino_GFX_ST77916_Driver::setAddrWindow(int16_t x, int16_t y, uint16_t w, uint16_t h) {
		currentX = x;
		currentY = y;
		currentW = w;
		currentH = h;
}

void Arduino_GFX_ST77916_Driver::pushColors(uint16_t* data, uint32_t len, bool swap_bytes) {
		if (!gfx || !data || currentW == 0 || currentH == 0) return;

		// draw16bitRGBBitmap() sets the address window and writes pixels
		// in a single bus transaction sequence.  Arduino_GFX handles
		// byte order internally, so swap_bytes is not needed here.
		(void)swap_bytes;
		gfx->draw16bitRGBBitmap(currentX, currentY, data, currentW, currentH);
}
