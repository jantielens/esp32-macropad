/*
 * Arduino_GFX NV3041A QSPI Display Driver Implementation
 */

#include "arduino_gfx_nv3041a_driver.h"
#include "arduino_gfx_panel_sleep.h"
#include "../log_manager.h"

#ifndef TFT_SPI_FREQ_HZ
#define TFT_SPI_FREQ_HZ (32 * 1000 * 1000)
#endif

Arduino_GFX_NV3041A_Driver::Arduino_GFX_NV3041A_Driver()
		: bus(nullptr), gfx(nullptr), currentBrightness(100), currentX(0), currentY(0),
			currentW(0), currentH(0) {
}

Arduino_GFX_NV3041A_Driver::~Arduino_GFX_NV3041A_Driver() {
		if (gfx) delete gfx;
		if (bus) delete bus;
}

void Arduino_GFX_NV3041A_Driver::init() {
		LOGI("GFX_NV3041A", "Initializing NV3041A QSPI display driver");

		#ifdef LCD_BL_PIN
		pinMode(LCD_BL_PIN, OUTPUT);
		#if HAS_BACKLIGHT
		#if ESP_ARDUINO_VERSION_MAJOR >= 3
		ledcAttach(LCD_BL_PIN, TFT_BACKLIGHT_PWM_FREQ, 8);
		#else
		ledcSetup(TFT_BACKLIGHT_PWM_CHANNEL, TFT_BACKLIGHT_PWM_FREQ, 8);
		ledcAttachPin(LCD_BL_PIN, TFT_BACKLIGHT_PWM_CHANNEL);
		#endif
		setBacklightBrightness(currentBrightness);
		#else
		digitalWrite(LCD_BL_PIN, HIGH);
		#endif
		#endif

		bus = new Arduino_ESP32QSPI(LCD_QSPI_CS, LCD_QSPI_PCLK, LCD_QSPI_D0,
				LCD_QSPI_D1, LCD_QSPI_D2, LCD_QSPI_D3);
		gfx = new Arduino_NV3041A(bus, GFX_NOT_DEFINED, 0, true, DISPLAY_WIDTH, DISPLAY_HEIGHT);
		if (!gfx->begin(TFT_SPI_FREQ_HZ)) {
				LOGE("GFX_NV3041A", "Failed to initialize display");
				return;
		}

		gfx->fillScreen(RGB565_BLACK);
		LOGI("GFX_NV3041A", "Display ready: %dx%d at %d MHz", DISPLAY_WIDTH,
				DISPLAY_HEIGHT, TFT_SPI_FREQ_HZ / 1000000);
}

void Arduino_GFX_NV3041A_Driver::setRotation(uint8_t rotation) {
		if (gfx) gfx->setRotation(rotation);
}

int Arduino_GFX_NV3041A_Driver::width() { return DISPLAY_WIDTH; }
int Arduino_GFX_NV3041A_Driver::height() { return DISPLAY_HEIGHT; }

void Arduino_GFX_NV3041A_Driver::setBacklight(bool on) {
		setBacklightBrightness(on ? 100 : 0);
}

void Arduino_GFX_NV3041A_Driver::setBacklightBrightness(uint8_t brightness) {
		if (brightness > 100) brightness = 100;
		currentBrightness = brightness;
		#ifdef LCD_BL_PIN
		#if HAS_BACKLIGHT
		uint32_t duty = brightness >= 100 ? 255 : (uint32_t)brightness * 255 / 100;
		#if ESP_ARDUINO_VERSION_MAJOR >= 3
		ledcWrite(LCD_BL_PIN, duty);
		#else
		ledcWrite(TFT_BACKLIGHT_PWM_CHANNEL, duty);
		#endif
		#else
		digitalWrite(LCD_BL_PIN, brightness > 0 ? HIGH : LOW);
		#endif
		#endif
}

uint8_t Arduino_GFX_NV3041A_Driver::getBacklightBrightness() { return currentBrightness; }
bool Arduino_GFX_NV3041A_Driver::hasBacklightControl() { return HAS_BACKLIGHT; }
void Arduino_GFX_NV3041A_Driver::applyDisplayFixes() {}
void Arduino_GFX_NV3041A_Driver::startWrite() {}
void Arduino_GFX_NV3041A_Driver::endWrite() {}

void Arduino_GFX_NV3041A_Driver::setAddrWindow(int16_t x, int16_t y, uint16_t w, uint16_t h) {
		currentX = x;
		currentY = y;
		currentW = w;
		currentH = h;
}

void Arduino_GFX_NV3041A_Driver::pushColors(uint16_t* data, uint32_t len, bool swap_bytes) {
		if (!gfx || !data || !currentW || !currentH || len != (uint32_t)currentW * currentH) return;
		(void)swap_bytes;
		gfx->draw16bitRGBBitmap(currentX, currentY, data, currentW, currentH);
}

void Arduino_GFX_NV3041A_Driver::displaySleep() { arduino_gfx_panel_sleep(bus); }
void Arduino_GFX_NV3041A_Driver::displayWake() { arduino_gfx_panel_wake(bus); }
void Arduino_GFX_NV3041A_Driver::displayWakeSleepOut() { arduino_gfx_panel_sleep_out(bus); }
void Arduino_GFX_NV3041A_Driver::displayWakeDisplayOn() { arduino_gfx_panel_display_on(bus); }