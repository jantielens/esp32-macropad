#include "reterminal_e1003_epaper_driver.h"

#include "../board_config.h"
#include "../log_manager.h"

#include <SPI.h>
#include <esp_heap_caps.h>

#include <cstring>

namespace {

constexpr uint16_t kCommandSystemRun = 0x0001;
constexpr uint16_t kCommandRegisterWrite = 0x0011;
constexpr uint16_t kCommandLoadImageArea = 0x0021;
constexpr uint16_t kCommandLoadImageEnd = 0x0022;
constexpr uint16_t kCommandDisplayArea = 0x0034;
constexpr uint16_t kCommandGetDeviceInfo = 0x0302;
constexpr uint16_t kCommandSetVcom = 0x0039;
constexpr uint16_t kCommandSetTemperature = 0x0040;
constexpr uint16_t kRegisterImageBufferAddress = 0x0208;
constexpr uint8_t kImageMode4Bpp = 2;
constexpr uint8_t kWaveformDu = 1;
constexpr uint8_t kWaveformGc16 = 2;
constexpr uint32_t kHrdyTimeoutMs = 250;
constexpr uint32_t kStartupTimeoutMs = 3000;

constexpr int kPanelWidth = 1872;
constexpr int kPanelHeight = 1404;
constexpr size_t kFramebufferBytes = (size_t)kPanelWidth * kPanelHeight / 2;

constexpr int kPinSck = 7;
constexpr int kPinMiso = 8;
constexpr int kPinMosi = 9;
constexpr int kPinCs = 10;
constexpr int kPinBusy = 13;
constexpr int kPinTftEnable = 11;
constexpr int kPinIteEnable = 21;
constexpr int kPinReset = 12;
constexpr uint16_t kVcomMillivolts = 1400;

struct PanelRegion {
		uint16_t x;
		uint16_t y;
		uint16_t width;
		uint16_t height;
};

SPIClass s_spi(HSPI);
SPISettings s_spiSettings(10000000, MSBFIRST, SPI_MODE0);
uint32_t s_imageBufferAddress = 0;

bool waitHrdy(uint32_t timeoutMs = kHrdyTimeoutMs) {
		const uint32_t startMs = millis();
		while (digitalRead(kPinBusy) == LOW) {
			if (millis() - startMs >= timeoutMs) {
				LOGW("Epaper", "IT8951 HRDY timeout after %lums", (unsigned long)timeoutMs);
				return false;
			}
			delay(1);
		}
		return true;
}

bool writeCommand(uint16_t command) {
		if (!waitHrdy()) return false;
		s_spi.beginTransaction(s_spiSettings);
		digitalWrite(kPinCs, LOW);
		s_spi.transfer16(0x6000);
		if (!waitHrdy()) {
			digitalWrite(kPinCs, HIGH);
			s_spi.endTransaction();
			return false;
		}
		s_spi.transfer16(command);
		digitalWrite(kPinCs, HIGH);
		s_spi.endTransaction();
		return true;
}

bool writeData(uint16_t data) {
		if (!waitHrdy()) return false;
		s_spi.beginTransaction(s_spiSettings);
		digitalWrite(kPinCs, LOW);
		s_spi.transfer16(0x0000);
		if (!waitHrdy()) {
			digitalWrite(kPinCs, HIGH);
			s_spi.endTransaction();
			return false;
		}
		s_spi.transfer16(data);
		digitalWrite(kPinCs, HIGH);
		s_spi.endTransaction();
		return true;
}

bool writeRegister(uint16_t registerAddress, uint16_t value) {
		return writeCommand(kCommandRegisterWrite) && writeData(registerAddress) && writeData(value);
}

bool readDeviceInfo() {
		if (!writeCommand(kCommandGetDeviceInfo) || !waitHrdy()) return false;
		uint16_t info[20] = {};
		s_spi.beginTransaction(s_spiSettings);
		digitalWrite(kPinCs, LOW);
		s_spi.transfer16(0x1000);
		if (!waitHrdy()) {
			digitalWrite(kPinCs, HIGH);
			s_spi.endTransaction();
			return false;
		}
		s_spi.transfer16(0);
		for (uint8_t index = 0; index < 20; ++index) info[index] = s_spi.transfer16(0);
		digitalWrite(kPinCs, HIGH);
		s_spi.endTransaction();

		s_imageBufferAddress = ((uint32_t)info[3] << 16) | info[2];
		if (info[0] != kPanelWidth || info[1] != kPanelHeight || s_imageBufferAddress == 0 || s_imageBufferAddress == UINT32_MAX) {
			LOGE("Epaper", "unexpected IT8951 panel %ux%u, image buffer=0x%08lX", info[0], info[1],
					(unsigned long)s_imageBufferAddress);
			return false;
		}
		return true;
}

bool initializeController() {
		pinMode(kPinCs, OUTPUT);
		pinMode(kPinBusy, INPUT_PULLUP);
		pinMode(kPinTftEnable, OUTPUT);
		pinMode(kPinIteEnable, OUTPUT);
		pinMode(kPinReset, OUTPUT);
		digitalWrite(kPinCs, HIGH);
		digitalWrite(kPinTftEnable, HIGH);
		digitalWrite(kPinIteEnable, HIGH);
		digitalWrite(kPinReset, HIGH);
		delay(10);
		digitalWrite(kPinReset, LOW);
		delay(10);
		digitalWrite(kPinReset, HIGH);
		delay(10);
		s_spi.begin(kPinSck, kPinMiso, kPinMosi, -1);
		if (!waitHrdy(kStartupTimeoutMs) || !writeCommand(kCommandSystemRun) || !readDeviceInfo()) return false;
		// E1003 firmware accepts selector 0x0002 for VCOM writes; 0x0001 is read-only.
		return writeCommand(kCommandSetVcom) && writeData(0x0002) && writeData(kVcomMillivolts);
}

bool uploadFramebuffer(const uint8_t* framebuffer, const PanelRegion& region) {
		if (!writeRegister(kRegisterImageBufferAddress, (uint16_t)(s_imageBufferAddress & 0xFFFF)) ||
				!writeRegister(kRegisterImageBufferAddress + 2, (uint16_t)(s_imageBufferAddress >> 16)) ||
				!writeCommand(kCommandSetTemperature) || !writeData(0x0001) || !writeData(16)) return false;

		const uint16_t controllerX = kPanelWidth - region.x - region.width;
		const uint16_t arguments[] = {
				(uint16_t)(kImageMode4Bpp << 4), controllerX, region.y, region.width, region.height,
		};
		if (!writeCommand(kCommandLoadImageArea)) return false;
		for (uint16_t argument : arguments) {
			if (!writeData(argument)) return false;
		}

		const uint16_t rowBytes = region.width / 2;
		const uint16_t wordsPerRow = rowBytes / 2;
		uint8_t rowBuffer[kPanelWidth / 2];
		s_spi.beginTransaction(s_spiSettings);
		digitalWrite(kPinCs, LOW);
		if (!waitHrdy()) {
			digitalWrite(kPinCs, HIGH);
			s_spi.endTransaction();
			return false;
		}
		s_spi.transfer16(0x0000);
		for (uint16_t row = 0; row < region.height; ++row) {
			const uint8_t* sourceRow = framebuffer + (size_t)(region.y + row) * (kPanelWidth / 2) + region.x / 2;
			for (uint16_t word = 0; word < wordsPerRow; ++word) {
				const uint16_t sourceWord = wordsPerRow - 1 - word;
				rowBuffer[word * 2] = sourceRow[sourceWord * 2];
				rowBuffer[word * 2 + 1] = sourceRow[sourceWord * 2 + 1];
			}
			s_spi.transfer(rowBuffer, rowBytes);
			if ((row & 0x3F) == 0) yield();
		}
		digitalWrite(kPinCs, HIGH);
		s_spi.endTransaction();
		return writeCommand(kCommandLoadImageEnd);
}

bool refreshPanel(const PanelRegion& region, uint8_t waveform) {
		const uint16_t controllerX = kPanelWidth - region.x - region.width;
		if (!writeCommand(kCommandDisplayArea) || !writeData(controllerX) || !writeData(region.y) ||
				!writeData(region.width) || !writeData(region.height) || !writeData(waveform)) return false;
		const uint32_t startMs = millis();
		while (digitalRead(kPinBusy) == LOW) {
			if (millis() - startMs > 15000) {
				LOGW("Epaper", "IT8951 waveform %u timed out", waveform);
				return false;
			}
			delay(25);
		}
		return true;
}

} // namespace

ReTerminalE1003EpaperDriver::ReTerminalE1003EpaperDriver(DeviceConfig* cfg)
		: config(cfg), currentX(0), currentY(0), currentW(0), currentH(0), rotation(0),
			panelMode(cfg ? cfg->epaper_render_mode : EPAPER_DEFAULT_RENDER_MODE), lastRefreshMs(0), lastChangeMs(0),
			bwPartialUpdatesSinceFull(0), grayscalePartialUpdatesSinceFull(0), drawingFramebuffer(nullptr), presentedFramebuffer(nullptr), framebufferMutex(nullptr),
			pendingChanges(false), fullRefreshRequested(false), hasPresentedFrame(false), initialized(false), dirtyRegionValid(false),
			dirtyX(0), dirtyY(0), dirtyX2(0), dirtyY2(0) {}

ReTerminalE1003EpaperDriver::~ReTerminalE1003EpaperDriver() {
		if (framebufferMutex) vSemaphoreDelete(framebufferMutex);
		if (drawingFramebuffer) heap_caps_free(drawingFramebuffer);
		if (presentedFramebuffer) heap_caps_free(presentedFramebuffer);
}

void ReTerminalE1003EpaperDriver::init() {
		if (panelMode != EPAPER_RENDER_MODE_BW && panelMode != EPAPER_RENDER_MODE_GRAYSCALE) {
			panelMode = EPAPER_RENDER_MODE_GRAYSCALE;
		}
		framebufferMutex = xSemaphoreCreateMutex();
		drawingFramebuffer = (uint8_t*)heap_caps_malloc(framebufferBytes(), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
		presentedFramebuffer = (uint8_t*)heap_caps_malloc(framebufferBytes(), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
		if (!framebufferMutex || !drawingFramebuffer || !presentedFramebuffer) {
			LOGE("Epaper", "E1003 framebuffer or mutex allocation failed");
			return;
		}
		memset(drawingFramebuffer, 0xFF, framebufferBytes());
		memset(presentedFramebuffer, 0x00, framebufferBytes());
		if (!initializeController()) {
			LOGE("Epaper", "E1003 IT8951 initialization failed");
			return;
		}
		initialized = true;
		LOGI("Epaper", "E1003 interactive panel initialized (%dx%d, %s)", width(), height(),
				usesBwMode() ? "B/W" : "grayscale");
}

void ReTerminalE1003EpaperDriver::setRotation(uint8_t value) { rotation = value & 0x03; }
int ReTerminalE1003EpaperDriver::width() { return (rotation & 1) ? DISPLAY_HEIGHT : DISPLAY_WIDTH; }
int ReTerminalE1003EpaperDriver::height() { return (rotation & 1) ? DISPLAY_WIDTH : DISPLAY_HEIGHT; }
void ReTerminalE1003EpaperDriver::setBacklight(bool) {}
void ReTerminalE1003EpaperDriver::setBacklightBrightness(uint8_t) {}
uint8_t ReTerminalE1003EpaperDriver::getBacklightBrightness() { return 0; }
bool ReTerminalE1003EpaperDriver::hasBacklightControl() { return false; }
void ReTerminalE1003EpaperDriver::applyDisplayFixes() {}
void ReTerminalE1003EpaperDriver::startWrite() {}
void ReTerminalE1003EpaperDriver::endWrite() {}

void ReTerminalE1003EpaperDriver::setAddrWindow(int16_t x, int16_t y, uint16_t w, uint16_t h) {
		currentX = x;
		currentY = y;
		currentW = w;
		currentH = h;
}

bool ReTerminalE1003EpaperDriver::usesBwMode() const { return panelMode == EPAPER_RENDER_MODE_BW; }
size_t ReTerminalE1003EpaperDriver::framebufferBytes() const { return kFramebufferBytes; }

void ReTerminalE1003EpaperDriver::markDirtyRegion(int16_t x, int16_t y, uint16_t w, uint16_t h) {
		if (!w || !h) return;
		int16_t minX = x;
		int16_t minY = y;
		int16_t maxX = x + w - 1;
		int16_t maxY = y + h - 1;
		switch (rotation) {
			case 1:
				minX = kPanelWidth - (y + h);
				maxX = kPanelWidth - 1 - y;
				minY = x;
				maxY = x + w - 1;
				break;
			case 2:
				minX = kPanelWidth - (x + w);
				maxX = kPanelWidth - 1 - x;
				minY = kPanelHeight - (y + h);
				maxY = kPanelHeight - 1 - y;
				break;
			case 3:
				minX = y;
				maxX = y + h - 1;
				minY = kPanelHeight - (x + w);
				maxY = kPanelHeight - 1 - x;
				break;
		}
		if (!dirtyRegionValid) {
			dirtyX = minX;
			dirtyY = minY;
			dirtyX2 = maxX;
			dirtyY2 = maxY;
			dirtyRegionValid = true;
			return;
		}
		if (minX < dirtyX) dirtyX = minX;
		if (minY < dirtyY) dirtyY = minY;
		if (maxX > dirtyX2) dirtyX2 = maxX;
		if (maxY > dirtyY2) dirtyY2 = maxY;
}

void ReTerminalE1003EpaperDriver::writePixel(int16_t x, int16_t y, uint16_t rgb565) {
		if (x < 0 || y < 0 || x >= width() || y >= height()) return;
		int16_t drawX = x;
		int16_t drawY = y;
		switch (rotation) {
			case 1: { const int16_t oldX = drawX; drawX = kPanelWidth - 1 - drawY; drawY = oldX; break; }
			case 2: drawX = kPanelWidth - 1 - drawX; drawY = kPanelHeight - 1 - drawY; break;
			case 3: { const int16_t oldX = drawX; drawX = drawY; drawY = kPanelHeight - 1 - oldX; break; }
		}
		const uint8_t red = ((rgb565 >> 11) & 0x1F) * 255 / 31;
		const uint8_t green = ((rgb565 >> 5) & 0x3F) * 255 / 63;
		const uint8_t blue = (rgb565 & 0x1F) * 255 / 31;
		const uint8_t luminance = ((uint16_t)red * 77 + (uint16_t)green * 150 + (uint16_t)blue * 29) >> 8;
		uint8_t gray = (uint16_t(luminance) * 15 + 127) / 255;
		if (usesBwMode()) {
			static constexpr uint8_t bayer4x4[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
			gray = luminance >= bayer4x4[y & 3][x & 3] * 16 + 8 ? 15 : 0;
		}
		const size_t offset = (size_t)drawY * (kPanelWidth / 2) + drawX / 2;
		if (drawX & 1) drawingFramebuffer[offset] = (drawingFramebuffer[offset] & 0xF0) | gray;
		else drawingFramebuffer[offset] = (drawingFramebuffer[offset] & 0x0F) | (gray << 4);
}

bool ReTerminalE1003EpaperDriver::fullRefreshPending() {
		portENTER_CRITICAL(&fullRefreshRequestMux);
		const bool requested = fullRefreshRequested;
		portEXIT_CRITICAL(&fullRefreshRequestMux);
		return requested;
}

bool ReTerminalE1003EpaperDriver::consumeFullRefreshRequest() {
		portENTER_CRITICAL(&fullRefreshRequestMux);
		const bool requested = fullRefreshRequested;
		fullRefreshRequested = false;
		portEXIT_CRITICAL(&fullRefreshRequestMux);
		return requested;
}

void ReTerminalE1003EpaperDriver::pushColors(uint16_t* data, uint32_t, bool) {
		if (!initialized || !data || !framebufferMutex) return;
		xSemaphoreTake(framebufferMutex, portMAX_DELAY);
		const uint32_t sourceStride = flushSrcStride ? flushSrcStride / sizeof(uint16_t) : currentW;
		for (uint16_t row = 0; row < currentH; ++row) {
			const uint16_t* source = data + row * sourceStride;
			for (uint16_t column = 0; column < currentW; ++column) writePixel(currentX + column, currentY + row, source[column]);
		}
		markDirtyRegion(currentX, currentY, currentW, currentH);
		pendingChanges = true;
		lastChangeMs = millis();
		xSemaphoreGive(framebufferMutex);
		if ((uint32_t)currentW * currentH >= 1024) vTaskDelay(1);
}

void ReTerminalE1003EpaperDriver::present() {
		if (!initialized || !framebufferMutex) return;
		while (true) {
			xSemaphoreTake(framebufferMutex, portMAX_DELAY);
			if (!pendingChanges && !fullRefreshPending()) {
				xSemaphoreGive(framebufferMutex);
				return;
			}
			const EpaperRefreshSettings settings = config_manager_get_epaper_refresh_settings();
			const uint32_t minRefreshMs = settings.epaper_min_refresh_interval_ms;
			const uint32_t now = millis();
			const uint32_t refreshWait = lastRefreshMs && now - lastRefreshMs < minRefreshMs ? minRefreshMs - (now - lastRefreshMs) : 0;
			const uint32_t settleWait = EPAPER_REFRESH_SETTLE_MS && now - lastChangeMs < EPAPER_REFRESH_SETTLE_MS ? EPAPER_REFRESH_SETTLE_MS - (now - lastChangeMs) : 0;
			const uint32_t waitMs = refreshWait > settleWait ? refreshWait : settleWait;
			if (!waitMs) break;
			xSemaphoreGive(framebufferMutex);
			vTaskDelay(pdMS_TO_TICKS(waitMs) ?: 1);
		}

		const bool forceFull = consumeFullRefreshRequest();
		if (!pendingChanges && !forceFull) {
			xSemaphoreGive(framebufferMutex);
			return;
		}
		if (!forceFull && hasPresentedFrame && memcmp(drawingFramebuffer, presentedFramebuffer, framebufferBytes()) == 0) {
			pendingChanges = false;
			dirtyRegionValid = false;
			xSemaphoreGive(framebufferMutex);
			return;
		}
		const EpaperRefreshSettings settings = config_manager_get_epaper_refresh_settings();
		const bool scheduledFull = settings.epaper_full_refresh_threshold > 0 &&
				(usesBwMode() ? bwPartialUpdatesSinceFull : grayscalePartialUpdatesSinceFull) >= settings.epaper_full_refresh_threshold;
		PanelRegion region = {dirtyX, dirtyY, (uint16_t)(dirtyX2 - dirtyX + 1), (uint16_t)(dirtyY2 - dirtyY + 1)};
		const uint16_t regionEnd = region.x + region.width;
		region.x &= ~0x03;
		const uint16_t alignedRegionEnd = (uint16_t)((regionEnd + 3) & ~0x03);
		region.width = alignedRegionEnd > kPanelWidth ? kPanelWidth - region.x : alignedRegionEnd - region.x;
		const uint32_t regionPixels = (uint32_t)region.width * region.height;
		const uint32_t panelPixels = (uint32_t)kPanelWidth * kPanelHeight;
		const uint32_t dirtyCoveragePercent = (regionPixels * 100 + panelPixels - 1) / panelPixels;
		const bool fullRefresh = forceFull || scheduledFull || !hasPresentedFrame;
		const PanelRegion dirtyRegion = region;
		const uint32_t partialCountBefore = usesBwMode() ? bwPartialUpdatesSinceFull : grayscalePartialUpdatesSinceFull;
		if (fullRefresh) region = {0, 0, kPanelWidth, kPanelHeight};
		memcpy(presentedFramebuffer, drawingFramebuffer, framebufferBytes());
		pendingChanges = false;
		dirtyRegionValid = false;
		xSemaphoreGive(framebufferMutex);

		const uint32_t startMs = millis();
		const bool presented = uploadFramebuffer(presentedFramebuffer, region) && refreshPanel(region, fullRefresh || !usesBwMode() ? kWaveformGc16 : kWaveformDu);
		xSemaphoreTake(framebufferMutex, portMAX_DELAY);
		if (!presented) {
			pendingChanges = true;
			dirtyX = 0;
			dirtyY = 0;
			dirtyX2 = kPanelWidth - 1;
			dirtyY2 = kPanelHeight - 1;
			dirtyRegionValid = true;
			LOGE("Epaper", "E1003 presentation failed");
		} else {
			hasPresentedFrame = true;
			lastRefreshMs = millis();
			if (fullRefresh) {
				bwPartialUpdatesSinceFull = 0;
				grayscalePartialUpdatesSinceFull = 0;
			} else if (usesBwMode()) ++bwPartialUpdatesSinceFull;
			else ++grayscalePartialUpdatesSinceFull;
			LOGI("Epaper", "%s refresh %ux%u completed in %lums (dirty=%ux%u@%u,%u %lu%%; forced=%d scheduled=%d; partials=%lu/%u)",
					fullRefresh ? "full GC16" : (usesBwMode() ? "B/W partial DU" : "grayscale partial GC16"),
					region.width, region.height, (unsigned long)(lastRefreshMs - startMs),
					dirtyRegion.width, dirtyRegion.height, dirtyRegion.x, dirtyRegion.y, (unsigned long)dirtyCoveragePercent,
					forceFull, scheduledFull, (unsigned long)partialCountBefore,
					settings.epaper_full_refresh_threshold);
		}
		xSemaphoreGive(framebufferMutex);
}

bool ReTerminalE1003EpaperDriver::requestFullRefresh() {
		if (!initialized) return false;
		portENTER_CRITICAL(&fullRefreshRequestMux);
		fullRefreshRequested = true;
		portEXIT_CRITICAL(&fullRefreshRequestMux);
		return true;
}