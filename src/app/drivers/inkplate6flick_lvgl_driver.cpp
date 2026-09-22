#include "inkplate6flick_lvgl_driver.h"

#include "../board_config.h"
#include "../log_manager.h"

#include <Inkplate.h>
#include <esp_heap_caps.h>

#include <cstring>

static Inkplate* s_inkplate = nullptr;

Inkplate* inkplate6flick_lvgl_instance() {
		return s_inkplate;
}

Inkplate6Flick_LVGL_Driver::Inkplate6Flick_LVGL_Driver()
		: display(nullptr), framebufferMutex(nullptr), currentX(0), currentY(0),
			currentW(0), currentH(0), rotation(0), lastRefreshMs(0),
			lastChangeMs(0), presentedFramebuffer(nullptr), refreshCount(0),
			noOpSkipCount(0), pendingChanges(false), hasPresentedFramebuffer(false) {}

Inkplate6Flick_LVGL_Driver::~Inkplate6Flick_LVGL_Driver() {
		if (framebufferMutex) vSemaphoreDelete(framebufferMutex);
		if (presentedFramebuffer) heap_caps_free(presentedFramebuffer);
}

void Inkplate6Flick_LVGL_Driver::init() {
		if (!s_inkplate) s_inkplate = new Inkplate(INKPLATE_3BIT);
		display = s_inkplate;
		if (!display) {
			LOGE("Inkplate", "display allocation failed");
			return;
		}
		display->begin();
		display->selectDisplayMode(INKPLATE_3BIT);
		display->clearDisplay();
		framebufferMutex = xSemaphoreCreateMutex();
		if (!framebufferMutex) {
			LOGE("Inkplate", "framebuffer mutex allocation failed");
			return;
		}
		presentedFramebuffer = (uint8_t*)heap_caps_malloc(framebufferBytes(), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
		if (!presentedFramebuffer) {
			LOGW("Inkplate", "no-op refresh suppression disabled: snapshot allocation failed");
		}
		LOGI("Inkplate", "6FLICK LVGL spike initialized (%dx%d, 3-bit grayscale)", width(), height());
}

void Inkplate6Flick_LVGL_Driver::setRotation(uint8_t value) {
		rotation = value & 0x03;
		if (display) display->setRotation(rotation);
}

int Inkplate6Flick_LVGL_Driver::width() { return (rotation & 1) ? DISPLAY_HEIGHT : DISPLAY_WIDTH; }
int Inkplate6Flick_LVGL_Driver::height() { return (rotation & 1) ? DISPLAY_WIDTH : DISPLAY_HEIGHT; }
void Inkplate6Flick_LVGL_Driver::setBacklight(bool) {}
void Inkplate6Flick_LVGL_Driver::setBacklightBrightness(uint8_t) {}
uint8_t Inkplate6Flick_LVGL_Driver::getBacklightBrightness() { return 0; }
bool Inkplate6Flick_LVGL_Driver::hasBacklightControl() { return false; }
void Inkplate6Flick_LVGL_Driver::applyDisplayFixes() {}
void Inkplate6Flick_LVGL_Driver::startWrite() {}
void Inkplate6Flick_LVGL_Driver::endWrite() {}

void Inkplate6Flick_LVGL_Driver::setAddrWindow(int16_t x, int16_t y, uint16_t w, uint16_t h) {
		currentX = x;
		currentY = y;
		currentW = w;
		currentH = h;
}

void Inkplate6Flick_LVGL_Driver::writePixel(int16_t x, int16_t y, uint16_t rgb565) {
		const uint8_t red5 = (rgb565 >> 11) & 0x1F;
		const uint8_t green6 = (rgb565 >> 5) & 0x3F;
		const uint8_t blue5 = rgb565 & 0x1F;
		const uint8_t red = (red5 << 3) | (red5 >> 2);
		const uint8_t green = (green6 << 2) | (green6 >> 4);
		const uint8_t blue = (blue5 << 3) | (blue5 >> 2);
		const uint16_t luminance = red * 77 + green * 150 + blue * 29;
		const uint8_t gray = (luminance * 7 + (255 * 128)) / (255 * 256);
		display->drawPixel(x, y, gray);
}

size_t Inkplate6Flick_LVGL_Driver::framebufferBytes() const {
		return (size_t)DISPLAY_WIDTH * DISPLAY_HEIGHT / 2;
}

void Inkplate6Flick_LVGL_Driver::pushColors(uint16_t* data, uint32_t, bool) {
		if (!display || !framebufferMutex || !data) return;
		xSemaphoreTake(framebufferMutex, portMAX_DELAY);
		const uint32_t sourceStride = flushSrcStride ? flushSrcStride / sizeof(uint16_t) : currentW;
		for (uint16_t row = 0; row < currentH; ++row) {
			const uint16_t* source = data + row * sourceStride;
			for (uint16_t column = 0; column < currentW; ++column) {
				writePixel(currentX + column, currentY + row, source[column]);
			}
		}
		pendingChanges = true;
		lastChangeMs = millis();
		xSemaphoreGive(framebufferMutex);
}

void Inkplate6Flick_LVGL_Driver::present() {
		if (!display || !framebufferMutex) return;
		while (true) {
			xSemaphoreTake(framebufferMutex, portMAX_DELAY);
			if (!pendingChanges) {
				xSemaphoreGive(framebufferMutex);
				return;
			}
			const uint32_t now = millis();
			const uint32_t refreshWait = lastRefreshMs && now - lastRefreshMs < INKPLATE_MIN_REFRESH_MS
					? INKPLATE_MIN_REFRESH_MS - (now - lastRefreshMs) : 0;
			const uint32_t quietWait = INKPLATE_REFRESH_SETTLE_MS > 0 && now - lastChangeMs < INKPLATE_REFRESH_SETTLE_MS
					? INKPLATE_REFRESH_SETTLE_MS - (now - lastChangeMs) : 0;

			const uint32_t waitMs = refreshWait > quietWait ? refreshWait : quietWait;
			if (waitMs == 0) {
				break;
			}
			xSemaphoreGive(framebufferMutex);
			const TickType_t waitTicks = pdMS_TO_TICKS(waitMs);
			vTaskDelay(waitTicks ? waitTicks : 1);
		}

		if (!pendingChanges) {
			xSemaphoreGive(framebufferMutex);
			return;
		}
		if (presentedFramebuffer && hasPresentedFramebuffer &&
				memcmp(display->DMemory4Bit, presentedFramebuffer, framebufferBytes()) == 0) {
			pendingChanges = false;
			noOpSkipCount++;
			LOGI("Inkplate", "refresh skipped=no-op count=%lu", (unsigned long)noOpSkipCount);
			xSemaphoreGive(framebufferMutex);
			return;
		}

		const uint32_t refreshStartMs = millis();
		display->display();
		if (presentedFramebuffer) {
			memcpy(presentedFramebuffer, display->DMemory4Bit, framebufferBytes());
			hasPresentedFramebuffer = true;
		}
		pendingChanges = false;
		lastRefreshMs = millis();
		refreshCount++;
		LOGI("Inkplate", "grayscale refresh count=%lu dur=%lums", (unsigned long)refreshCount,
				(unsigned long)(lastRefreshMs - refreshStartMs));
		xSemaphoreGive(framebufferMutex);
}
