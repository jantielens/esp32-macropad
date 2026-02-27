/*
 * Arduino_GFX Display Driver Implementation
 * 
 * Wraps Arduino_GFX library for QSPI displays (AXS15231B).
 * Uses a PSRAM framebuffer to work around QSPI partial-write
 * limitations — see header for full explanation.
 * Panel stays in portrait; driver-level rotation transposes pixels
 * from LVGL's landscape coordinate space to portrait for the panel.
 */

#include "arduino_gfx_driver.h"
#include "../log_manager.h"
#include <esp_heap_caps.h>

// Spinlock protecting dirtyMaxRow / hasDirtyRows shared between
// pushColors() (LVGL task) and present() (async present task).
static portMUX_TYPE s_dirty_mux = portMUX_INITIALIZER_UNLOCKED;

Arduino_GFX_Driver::Arduino_GFX_Driver() 
		: bus(nullptr), gfx(nullptr), currentBrightness(100), backlightPwmAttached(false),
			displayWidth(DISPLAY_WIDTH), displayHeight(DISPLAY_HEIGHT), displayRotation(DISPLAY_ROTATION),
			currentX(0), currentY(0), currentW(0), currentH(0),
			framebuffer(nullptr), hasDirtyRows(false), dirtyMaxRow(0) {
}

Arduino_GFX_Driver::~Arduino_GFX_Driver() {
		if (framebuffer) { heap_caps_free(framebuffer); framebuffer = nullptr; }
		if (gfx) delete gfx;
		if (bus) delete bus;
}

void Arduino_GFX_Driver::init() {
		LOGI("GFX", "Initializing QSPI display driver");
		
		// Initialize backlight pin first
		#ifdef LCD_BL_PIN
		pinMode(LCD_BL_PIN, OUTPUT);

		#if HAS_BACKLIGHT
		// Configure PWM for smooth brightness control
		#if ESP_ARDUINO_VERSION_MAJOR >= 3
		double actualFreq = ledcAttach(LCD_BL_PIN, TFT_BACKLIGHT_PWM_FREQ, 8);  // pin, freq, resolution (8-bit)
		LOGI("GFX", "PWM attached on GPIO%d, actual freq: %.1f Hz", LCD_BL_PIN, actualFreq);
		#else
		ledcSetup(TFT_BACKLIGHT_PWM_CHANNEL, TFT_BACKLIGHT_PWM_FREQ, 8);
		ledcAttachPin(LCD_BL_PIN, TFT_BACKLIGHT_PWM_CHANNEL);
		LOGI("GFX", "PWM setup complete on GPIO%d (channel %d)", LCD_BL_PIN, TFT_BACKLIGHT_PWM_CHANNEL);
		#endif
		backlightPwmAttached = true;

		setBacklightBrightness(currentBrightness);
		#else
		// Simple on/off
		#ifdef TFT_BACKLIGHT_ON
		digitalWrite(LCD_BL_PIN, TFT_BACKLIGHT_ON);
		#else
		digitalWrite(LCD_BL_PIN, HIGH);
		#endif
		LOGI("GFX", "Backlight enabled on GPIO%d", LCD_BL_PIN);
		#endif
		#endif
		
		// Create QSPI bus (from sample: Arduino_ESP32QSPI)
		#ifdef LCD_QSPI_CS
		bus = new Arduino_ESP32QSPI(
				LCD_QSPI_CS,    // CS
				LCD_QSPI_PCLK,  // SCK
				LCD_QSPI_D0,    // D0
				LCD_QSPI_D1,    // D1
				LCD_QSPI_D2,    // D2
				LCD_QSPI_D3     // D3
		);
		LOGI("GFX", "QSPI bus created");
		#else
		LOGE("GFX", "QSPI pins not defined in board_config.h");
		return;
		#endif
		
		// Create AXS15231B display (physical panel, portrait orientation)
		// GFX_NOT_DEFINED = -1 for RST (no reset pin on this board)
		// 0 = initial rotation (portrait)
		// false = IPS mode
		gfx = new Arduino_AXS15231B(bus, LCD_QSPI_RST, 0, false, displayWidth, displayHeight);
		LOGI("GFX", "AXS15231B panel object created");
		
		// Initialize display directly (no canvas layer)
		if (!gfx->begin(40000000UL)) {  // 40MHz QSPI frequency
				LOGE("GFX", "Failed to initialize display");
				return;
		}
		LOGI("GFX", "Display initialized (direct QSPI)");
		
		// Clear screen
		gfx->fillScreen(RGB565_BLACK);
		LOGI("GFX", "Screen cleared");
		
		// Allocate portrait-orientation framebuffer in PSRAM.
		// QSPI partial writes don't work (address window lost on CS toggle),
		// so we accumulate LVGL strips here and send the full frame in present().
		size_t fbBytes = (size_t)displayWidth * displayHeight * sizeof(uint16_t);
		framebuffer = (uint16_t*)heap_caps_malloc(fbBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
		if (!framebuffer) {
				framebuffer = (uint16_t*)heap_caps_malloc(fbBytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
		}
		if (framebuffer) {
				memset(framebuffer, 0, fbBytes);
				LOGI("GFX", "Framebuffer allocated: %u bytes (%ux%u)", fbBytes, displayWidth, displayHeight);
		} else {
				LOGE("GFX", "Failed to allocate framebuffer! (%u bytes)", fbBytes);
		}
		
		LOGI("GFX", "Display ready: %dx%d (physical), rotation %d", displayWidth, displayHeight, displayRotation);
}

void Arduino_GFX_Driver::setRotation(uint8_t rotation) {
		// Panel stays in portrait mode (rotation 0).
		// Driver-level rotation transposes pixels in pushColors().
		// MADCTL rotation is unreliable on AXS15231B over QSPI.
		displayRotation = rotation;
		LOGI("GFX", "Rotation %d (driver-level transpose in pushColors)", rotation);
}

int Arduino_GFX_Driver::width() {
		// Return LOGICAL width (what LVGL uses for layout).
		// For landscape rotations (1, 3), the logical width is the physical height.
		if (displayRotation == 1 || displayRotation == 3) {
				return (int)displayHeight;
		}
		return (int)displayWidth;
}

int Arduino_GFX_Driver::height() {
		// Return LOGICAL height (what LVGL uses for layout).
		// For landscape rotations (1, 3), the logical height is the physical width.
		if (displayRotation == 1 || displayRotation == 3) {
				return (int)displayWidth;
		}
		return (int)displayHeight;
}

void Arduino_GFX_Driver::setBacklight(bool on) {
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

void Arduino_GFX_Driver::setBacklightBrightness(uint8_t brightness) {
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

		// Handle active low vs active high backlight
		#ifdef TFT_BACKLIGHT_ON
		if (!TFT_BACKLIGHT_ON) {
				duty = 255 - duty;
		}
		#endif

		#if ESP_ARDUINO_VERSION_MAJOR >= 3
		ledcWrite(LCD_BL_PIN, duty);  // New API: write to pin directly
		#else
		ledcWrite(TFT_BACKLIGHT_PWM_CHANNEL, duty);  // Old API: write to channel
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

uint8_t Arduino_GFX_Driver::getBacklightBrightness() {
		return currentBrightness;
}

bool Arduino_GFX_Driver::hasBacklightControl() {
		#ifdef LCD_BL_PIN
		return true;
		#else
		return false;
		#endif
}

void Arduino_GFX_Driver::applyDisplayFixes() {
		// AXS15231B doesn't need gamma correction or inversion fixes
		// Panel is configured correctly by Arduino_GFX library
}

void Arduino_GFX_Driver::startWrite() {
		// pushColors() writes to the framebuffer; present() sends it to the
		// panel via draw16bitRGBBitmap() which handles bus transactions internally.
}

void Arduino_GFX_Driver::endWrite() {
		// See startWrite() comment.
}

void Arduino_GFX_Driver::setAddrWindow(int16_t x, int16_t y, uint16_t w, uint16_t h) {
		// Store the current drawing area for pushColors
		// draw16bitRGBBitmap takes coordinates + dimensions together
		currentX = x;
		currentY = y;
		currentW = w;
		currentH = h;
}

void Arduino_GFX_Driver::pushColors(uint16_t* data, uint32_t len, bool swap_bytes) {
		if (!framebuffer) return;
		
		const uint16_t w = currentW;
		const uint16_t h = currentH;
		const int16_t x = currentX;   // logical x (from LVGL)
		const int16_t y = currentY;   // logical y (from LVGL)
		
		// Copy LVGL flush strip into the portrait framebuffer.
		// For rotation 0 the coordinates map 1:1; for landscape rotations
		// each pixel is transposed from logical to physical orientation.
		// Track the portrait row range touched for dirty-row optimisation.
		
		// Compute the furthest dirty portrait row for this strip.
		uint16_t maxDirtyRow = 0;
		
		switch (displayRotation) {
				case 0: {
						// Portrait — direct row-by-row memcpy.
						maxDirtyRow = (uint16_t)(y + h - 1);
						for (uint16_t r = 0; r < h; r++) {
								memcpy(&framebuffer[(y + r) * displayWidth + x],
											 &data[r * w],
											 w * sizeof(uint16_t));
						}
						break;
				}
				case 1: {
						// 90° CW — logical (lx, ly) → physical (ly, H-1-lx)
						maxDirtyRow = displayHeight - 1 - x;
						for (uint16_t r = 0; r < h; r++) {
								const uint16_t py_base = y + r;
								for (uint16_t c = 0; c < w; c++) {
										const uint16_t px = py_base;
										const uint16_t py = displayHeight - 1 - (x + c);
										framebuffer[py * displayWidth + px] = data[r * w + c];
								}
						}
						break;
				}
				case 2: {
						// 180° — logical (lx, ly) → physical (W-1-lx, H-1-ly)
						maxDirtyRow = displayHeight - 1 - y;
						for (uint16_t r = 0; r < h; r++) {
								const uint16_t py = displayHeight - 1 - (y + r);
								for (uint16_t c = 0; c < w; c++) {
										const uint16_t px = displayWidth - 1 - (x + c);
										framebuffer[py * displayWidth + px] = data[r * w + c];
								}
						}
						break;
				}
				case 3: {
						// 270° CW — logical (lx, ly) → physical (W-1-ly, lx)
						maxDirtyRow = (uint16_t)(x + w - 1);
						for (uint16_t r = 0; r < h; r++) {
								for (uint16_t c = 0; c < w; c++) {
										const uint16_t px = displayWidth - 1 - (y + r);
										const uint16_t py = x + c;
										framebuffer[py * displayWidth + px] = data[r * w + c];
								}
						}
						break;
				}
		}
		
		// Atomic dirty-row update — safe for concurrent present() in the async
		// present task (only the tracking variables are shared, not the framebuffer
		// writes above which are harmless to overlap with the QSPI read path).
		portENTER_CRITICAL(&s_dirty_mux);
		hasDirtyRows = true;
		if (maxDirtyRow > dirtyMaxRow) dirtyMaxRow = maxDirtyRow;
		portEXIT_CRITICAL(&s_dirty_mux);
}

void Arduino_GFX_Driver::present() {
		if (!gfx || !framebuffer) return;
		
		// Atomically capture and reset dirty-row state.
		// This allows pushColors() in the LVGL task to safely update
		// dirty tracking while present() transfers pixel data to the panel.
		portENTER_CRITICAL(&s_dirty_mux);
		if (!hasDirtyRows) {
				portEXIT_CRITICAL(&s_dirty_mux);
				return;
		}
		uint16_t rowCount = dirtyMaxRow + 1;
		hasDirtyRows = false;
		dirtyMaxRow = 0;
		portEXIT_CRITICAL(&s_dirty_mux);
		
		// Clamp to valid range.
		if (rowCount > displayHeight) rowCount = displayHeight;
		
		// Send only the dirty portrait rows to the panel.
		// draw16bitRGBBitmap at (0,0) works reliably on QSPI (see header).
		// We always start at row 0 because the panel ignores address windows,
		// but we limit the height to rowCount to reduce transfer size.
		gfx->draw16bitRGBBitmap(0, 0, framebuffer, displayWidth, rowCount);
}
