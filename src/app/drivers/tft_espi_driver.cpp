#include "tft_espi_driver.h"
#include "../log_manager.h"

TFT_eSPI_Driver::TFT_eSPI_Driver() : currentBrightness(100) {
		// TFT_eSPI constructor already called
		// Initialize brightness to 100% (full brightness)
}

void TFT_eSPI_Driver::init() {
		LOGI("TFT_eSPI", "Initializing");
		tft.init();
		
		#if HAS_BACKLIGHT
		// Initialize PWM for backlight control
		LOGI("TFT_eSPI", "Configuring PWM backlight control on pin %d", TFT_BL);
		
		// ESP32 Arduino Core 3.x uses new LEDC API
		#if ESP_ARDUINO_VERSION_MAJOR >= 3
		double actualFreq = ledcAttach(TFT_BL, TFT_BACKLIGHT_PWM_FREQ, 8);  // pin, freq, resolution (8-bit)
		LOGI("TFT_eSPI", "PWM attached, actual freq: %.1f Hz", actualFreq);
		#else
		// ESP32 Arduino Core 2.x uses old LEDC API
		ledcSetup(TFT_BACKLIGHT_PWM_CHANNEL, TFT_BACKLIGHT_PWM_FREQ, 8);  // channel, freq, resolution
		ledcAttachPin(TFT_BL, TFT_BACKLIGHT_PWM_CHANNEL);
		LOGI("TFT_eSPI", "PWM setup complete (channel %d)", TFT_BACKLIGHT_PWM_CHANNEL);
		#endif
		
		LOGI("TFT_eSPI", "Applying initial brightness: %d%%", currentBrightness);
		setBacklightBrightness(currentBrightness);  // Apply initial brightness
		#endif
}

void TFT_eSPI_Driver::setRotation(uint8_t rotation) {
		tft.setRotation(rotation);
}

int TFT_eSPI_Driver::width() {
		return (int)tft.width();
}

int TFT_eSPI_Driver::height() {
		return (int)tft.height();
}

void TFT_eSPI_Driver::setBacklight(bool on) {
		#if HAS_BACKLIGHT
		// When backlight control available, use brightness (on=100%, off=0%)
		setBacklightBrightness(on ? 100 : 0);
		#elif defined(TFT_BL)
		// Fallback to digital control if no PWM backlight
		pinMode(TFT_BL, OUTPUT);
		digitalWrite(TFT_BL, on ? TFT_BACKLIGHT_ON : !TFT_BACKLIGHT_ON);
		#endif
}

void TFT_eSPI_Driver::setBacklightBrightness(uint8_t brightness) {
		#if HAS_BACKLIGHT
		// Clamp brightness to 0-100 range
		if (brightness > 100) brightness = 100;
		currentBrightness = brightness;
		
		// Map 0-100% to usable duty range (board-configurable)
		uint32_t dutyCycle = 0;
		if (brightness >= 100) {
			dutyCycle = 255;
		} else if (brightness > 0) {
			dutyCycle = TFT_BACKLIGHT_DUTY_MIN + ((uint32_t)(brightness - 1) * (TFT_BACKLIGHT_DUTY_MAX - TFT_BACKLIGHT_DUTY_MIN)) / 98;
		}
		
		// Handle active low vs active high backlight
		#ifdef TFT_BACKLIGHT_ON
		if (!TFT_BACKLIGHT_ON) {
				dutyCycle = 255 - dutyCycle;  // Invert for active-low
		}
		#endif
		
		// ESP32 Arduino Core 3.x uses new LEDC API
		#if ESP_ARDUINO_VERSION_MAJOR >= 3
		ledcWrite(TFT_BL, dutyCycle);  // New API: write to pin directly
		#else
		ledcWrite(TFT_BACKLIGHT_PWM_CHANNEL, dutyCycle);  // Old API: write to channel
		#endif
		#endif
}

uint8_t TFT_eSPI_Driver::getBacklightBrightness() {
		#if HAS_BACKLIGHT
		return currentBrightness;
		#else
		return 100;  // Default to full brightness if no control
		#endif
}

bool TFT_eSPI_Driver::hasBacklightControl() {
		#if HAS_BACKLIGHT
		return true;
		#else
		return false;
		#endif
}

void TFT_eSPI_Driver::applyDisplayFixes() {
		// Apply display-specific settings (inversion, gamma, etc.)
		#ifdef DISPLAY_INVERSION_ON
		tft.invertDisplay(true);
		LOGI("TFT_eSPI", "Inversion ON");
		#endif
		
		#ifdef DISPLAY_INVERSION_OFF
		tft.invertDisplay(false);
		LOGI("TFT_eSPI", "Inversion OFF");
		#endif
		
		// Apply gamma fix (both v2 and v3 CYD variants need this)
		#ifdef DISPLAY_NEEDS_GAMMA_FIX
		LOGI("TFT_eSPI", "Applying gamma correction fix");
		tft.writecommand(0x26);
		tft.writedata(2);
		delay(120);
		tft.writecommand(0x26);
		tft.writedata(1);
		LOGI("TFT_eSPI", "Gamma fix applied");
		#endif
}

void TFT_eSPI_Driver::startWrite() {
		tft.startWrite();
}

void TFT_eSPI_Driver::endWrite() {
		tft.endWrite();
}

void TFT_eSPI_Driver::setAddrWindow(int16_t x, int16_t y, uint16_t w, uint16_t h) {
		tft.setAddrWindow(x, y, w, h);
}

void TFT_eSPI_Driver::pushColors(uint16_t* data, uint32_t len, bool swap_bytes) {
		tft.pushColors(data, len, swap_bytes);
}
