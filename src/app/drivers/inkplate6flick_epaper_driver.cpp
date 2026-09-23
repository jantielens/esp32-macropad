#include "inkplate6flick_epaper_driver.h"

#include "../board_config.h"
#include "../log_manager.h"

#include <Inkplate.h>
#include <esp_heap_caps.h>

#include <cstring>

static Inkplate* s_inkplate = nullptr;

static constexpr uint8_t BAYER_4X4[4][4] = {
		{ 0, 8, 2, 10 },
		{ 12, 4, 14, 6 },
		{ 3, 11, 1, 9 },
		{ 15, 7, 13, 5 },
};

Inkplate* inkplate6flick_lvgl_instance() {
		return s_inkplate;
}

Inkplate6FlickEpaperDriver::Inkplate6FlickEpaperDriver(DeviceConfig* cfg)
		: display(nullptr), config(cfg), framebufferMutex(nullptr), currentX(0), currentY(0),
			currentW(0), currentH(0), rotation(0), panelMode(cfg ? cfg->epaper_render_mode : EPAPER_DEFAULT_RENDER_MODE),
			lastRefreshMs(0), lastChangeMs(0), drawingFramebuffer(nullptr), presentedFramebuffer(nullptr), refreshCount(0), partialUpdateCount(0),
			bwPartialUpdatesSinceFull(0), noOpSkipCount(0), backlightBrightness(0), frontlightLevel(0), pendingChanges(false),
			frontlightEnabled(false), fullRefreshRequested(false), hasPresentedFrame(false), initialized(false) {}

Inkplate6FlickEpaperDriver::~Inkplate6FlickEpaperDriver() {
		if (framebufferMutex) vSemaphoreDelete(framebufferMutex);
		if (presentedFramebuffer) heap_caps_free(presentedFramebuffer);
}

void Inkplate6FlickEpaperDriver::init() {
		if (panelMode != EPAPER_RENDER_MODE_BW && panelMode != EPAPER_RENDER_MODE_GRAYSCALE) {
			LOGW("Inkplate", "invalid panel mode=%u; using grayscale", panelMode);
			panelMode = EPAPER_RENDER_MODE_GRAYSCALE;
		}
		const uint8_t inkplateMode = usesBwMode() ? INKPLATE_1BIT : INKPLATE_3BIT;
		if (!s_inkplate) s_inkplate = new Inkplate(inkplateMode);
		display = s_inkplate;
		if (!display) {
			LOGE("Inkplate", "display allocation failed");
			return;
		}
		display->begin();
		display->selectDisplayMode(inkplateMode);
		// The library's threshold setter blocks its next partial update, so the
		// driver owns the cadence and leaves the library's automatic threshold off.
		if (usesBwMode()) display->setFullUpdateThreshold(0);
		display->clearDisplay();
		framebufferMutex = xSemaphoreCreateMutex();
		if (!framebufferMutex) {
			LOGE("Inkplate", "framebuffer mutex allocation failed");
			return;
		}
		drawingFramebuffer = usesBwMode() ? display->_partial : display->DMemory4Bit;
		presentedFramebuffer = (uint8_t*)heap_caps_malloc(framebufferBytes(), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
		if (!presentedFramebuffer) {
			LOGE("Inkplate", "presentation snapshot allocation failed");
			vSemaphoreDelete(framebufferMutex);
			framebufferMutex = nullptr;
			return;
		}
		initialized = true;
		LOGI("Inkplate", "6FLICK LVGL spike initialized (%dx%d, %s)", width(), height(),
				usesBwMode() ? "1-bit B/W" : "3-bit grayscale");
}

void Inkplate6FlickEpaperDriver::setRotation(uint8_t value) {
		rotation = value & 0x03;
		if (display) display->setRotation(rotation);
}

int Inkplate6FlickEpaperDriver::width() { return (rotation & 1) ? DISPLAY_HEIGHT : DISPLAY_WIDTH; }
int Inkplate6FlickEpaperDriver::height() { return (rotation & 1) ? DISPLAY_WIDTH : DISPLAY_HEIGHT; }
void Inkplate6FlickEpaperDriver::setBacklight(bool on) {
		setBacklightBrightness(on ? (backlightBrightness ? backlightBrightness : 100) : 0);
}

void Inkplate6FlickEpaperDriver::setBacklightBrightness(uint8_t brightness) {
		if (!display) return;
		if (brightness > 100) brightness = 100;

		if (framebufferMutex) xSemaphoreTake(framebufferMutex, portMAX_DELAY);
		if (brightness == 0) {
			if (frontlightEnabled) {
				display->frontlight.setState(false);
			}
			backlightBrightness = 0;
			frontlightLevel = 0;
			frontlightEnabled = false;
		} else {
			const uint8_t level = (uint16_t(brightness) * 63 + 50) / 100;
			if (!frontlightEnabled || frontlightLevel != level) {
				display->frontlight.setBrightness(level);
			}
			if (!frontlightEnabled) {
				display->frontlight.setState(true);
			}
			backlightBrightness = brightness;
			frontlightLevel = level;
			frontlightEnabled = true;
		}
		if (framebufferMutex) xSemaphoreGive(framebufferMutex);
}

uint8_t Inkplate6FlickEpaperDriver::getBacklightBrightness() { return backlightBrightness; }
bool Inkplate6FlickEpaperDriver::hasBacklightControl() { return true; }
void Inkplate6FlickEpaperDriver::applyDisplayFixes() {}
void Inkplate6FlickEpaperDriver::startWrite() {}
void Inkplate6FlickEpaperDriver::endWrite() {}

void Inkplate6FlickEpaperDriver::setAddrWindow(int16_t x, int16_t y, uint16_t w, uint16_t h) {
		currentX = x;
		currentY = y;
		currentW = w;
		currentH = h;
}

void Inkplate6FlickEpaperDriver::writePixel(int16_t x, int16_t y, uint16_t rgb565) {
		const uint8_t red5 = (rgb565 >> 11) & 0x1F;
		const uint8_t green6 = (rgb565 >> 5) & 0x3F;
		const uint8_t blue5 = rgb565 & 0x1F;
		const uint8_t red = (red5 << 3) | (red5 >> 2);
		const uint8_t green = (green6 << 2) | (green6 >> 4);
		const uint8_t blue = (blue5 << 3) | (blue5 >> 2);
		const uint16_t luminance = red * 77 + green * 150 + blue * 29;
		int16_t drawX = x;
		int16_t drawY = y;
		if (drawX < 0 || drawY < 0 || drawX >= width() || drawY >= height()) return;
		switch (rotation) {
			case 1:
				std::swap(drawX, drawY);
				drawX = DISPLAY_WIDTH - drawX - 1;
				break;
			case 2:
				drawX = DISPLAY_WIDTH - drawX - 1;
				drawY = DISPLAY_HEIGHT - drawY - 1;
				break;
			case 3:
				std::swap(drawX, drawY);
				drawY = DISPLAY_HEIGHT - drawY - 1;
				break;
		}
		if (usesBwMode()) {
			const uint8_t threshold = BAYER_4X4[y & 3][x & 3] * 16 + 8;
			const size_t offset = (size_t)(DISPLAY_WIDTH / 8) * drawY + drawX / 8;
			const uint8_t mask = 1U << (drawX & 7);
			drawingFramebuffer[offset] = (drawingFramebuffer[offset] & ~mask) |
					((luminance >> 8) < threshold ? mask : 0);
			return;
		}
		const uint8_t gray = (luminance * 7 + (255 * 128)) / (255 * 256);
		const size_t offset = (size_t)(DISPLAY_WIDTH / 2) * drawY + drawX / 2;
		const uint8_t mask = (drawX & 1) ? 0xF0 : 0x0F;
		drawingFramebuffer[offset] = (drawingFramebuffer[offset] & mask) |
				((drawX & 1) ? gray : gray << 4);
}

bool Inkplate6FlickEpaperDriver::usesBwMode() const {
		return panelMode == EPAPER_RENDER_MODE_BW;
}

uint8_t* Inkplate6FlickEpaperDriver::framebuffer() const {
		return drawingFramebuffer;
}

size_t Inkplate6FlickEpaperDriver::framebufferBytes() const {
		return (size_t)DISPLAY_WIDTH * DISPLAY_HEIGHT / (usesBwMode() ? 8 : 2);
}

void Inkplate6FlickEpaperDriver::setPresentationFramebuffer() {
		if (usesBwMode()) {
			display->_partial = presentedFramebuffer;
		} else {
			display->DMemory4Bit = presentedFramebuffer;
		}
}

bool Inkplate6FlickEpaperDriver::fullRefreshPending() {
		portENTER_CRITICAL(&fullRefreshRequestMux);
		const bool pending = fullRefreshRequested;
		portEXIT_CRITICAL(&fullRefreshRequestMux);
		return pending;
}

bool Inkplate6FlickEpaperDriver::consumeFullRefreshRequest() {
		portENTER_CRITICAL(&fullRefreshRequestMux);
		const bool requested = fullRefreshRequested;
		fullRefreshRequested = false;
		portEXIT_CRITICAL(&fullRefreshRequestMux);
		return requested;
}

void Inkplate6FlickEpaperDriver::pushColors(uint16_t* data, uint32_t, bool) {
		if (!initialized || !display || !framebufferMutex || !data) return;
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

void Inkplate6FlickEpaperDriver::present() {
		if (!initialized || !display || !framebufferMutex) return;
		while (true) {
			xSemaphoreTake(framebufferMutex, portMAX_DELAY);
			if (!pendingChanges && !fullRefreshPending()) {
				xSemaphoreGive(framebufferMutex);
				return;
			}
			const uint32_t now = millis();
			const EpaperRefreshSettings settings = config_manager_get_epaper_refresh_settings();
			const uint32_t minRefreshMs = usesBwMode()
					? settings.epaper_bw_min_refresh_interval_ms : settings.epaper_grayscale_min_refresh_interval_ms;
			const uint32_t refreshWait = lastRefreshMs && now - lastRefreshMs < minRefreshMs
					? minRefreshMs - (now - lastRefreshMs) : 0;
			const uint32_t quietWait = EPAPER_REFRESH_SETTLE_MS > 0 && now - lastChangeMs < EPAPER_REFRESH_SETTLE_MS
					? EPAPER_REFRESH_SETTLE_MS - (now - lastChangeMs) : 0;

			const uint32_t waitMs = refreshWait > quietWait ? refreshWait : quietWait;
			if (waitMs == 0) {
				break;
			}
			xSemaphoreGive(framebufferMutex);
			const TickType_t waitTicks = pdMS_TO_TICKS(waitMs);
			vTaskDelay(waitTicks ? waitTicks : 1);
		}

		const bool forceFullRefresh = consumeFullRefreshRequest();
		if (!pendingChanges && !forceFullRefresh) {
			xSemaphoreGive(framebufferMutex);
			return;
		}
		if (!forceFullRefresh && presentedFramebuffer && hasPresentedFrame &&
				memcmp(framebuffer(), presentedFramebuffer, framebufferBytes()) == 0) {
			pendingChanges = false;
			noOpSkipCount++;
			LOGI("Inkplate", "refresh skipped=no-op count=%lu", (unsigned long)noOpSkipCount);
			xSemaphoreGive(framebufferMutex);
			return;
		}

		const EpaperRefreshSettings settings = config_manager_get_epaper_refresh_settings();
		const uint16_t fullUpdateThreshold = settings.epaper_bw_full_refresh_threshold;
		const bool scheduledFullRefresh = usesBwMode() && fullUpdateThreshold > 0 &&
				bwPartialUpdatesSinceFull >= fullUpdateThreshold;
		memcpy(presentedFramebuffer, framebuffer(), framebufferBytes());
		setPresentationFramebuffer();
		pendingChanges = false;
		xSemaphoreGive(framebufferMutex);

		const uint32_t refreshStartMs = millis();
		if (forceFullRefresh || scheduledFullRefresh) {
			display->display();
			refreshCount++;
			bwPartialUpdatesSinceFull = 0;
		} else if (usesBwMode() && hasPresentedFrame) {
			display->partialUpdate(false);
			partialUpdateCount++;
			bwPartialUpdatesSinceFull++;
		} else {
			display->display();
			refreshCount++;
		}
		xSemaphoreTake(framebufferMutex, portMAX_DELAY);
		hasPresentedFrame = true;
		lastRefreshMs = millis();
		LOGI("Inkplate", "%s refresh full=%lu partial_requests=%lu dur=%lums", usesBwMode() ? "B/W" : "grayscale",
				(unsigned long)refreshCount, (unsigned long)partialUpdateCount,
				(unsigned long)(lastRefreshMs - refreshStartMs));
		xSemaphoreGive(framebufferMutex);
}

bool Inkplate6FlickEpaperDriver::requestFullRefresh() {
		if (!initialized || !display) return false;
		portENTER_CRITICAL(&fullRefreshRequestMux);
		fullRefreshRequested = true;
		portEXIT_CRITICAL(&fullRefreshRequestMux);
		return true;
}
