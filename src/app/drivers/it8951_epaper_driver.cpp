#include "it8951_epaper_driver.h"
#include "monochrome_render.h"

#include "../board_config.h"
#include "../log_manager.h"
#include "../ota_activity.h"

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

struct LuminanceTables {
		uint16_t red[32] = {};
		uint16_t green[64] = {};
		uint16_t blue[32] = {};

		constexpr LuminanceTables() {
			for (int i = 0; i < 32; ++i) {
				red[i] = (i * 255 / 31) * 77;
				blue[i] = (i * 255 / 31) * 29;
			}
			for (int i = 0; i < 64; ++i) green[i] = (i * 255 / 63) * 150;
		}
};

constexpr LuminanceTables kLuminance;

uint8_t pixelGray(uint16_t rgb565, int16_t x, int16_t y, bool bwMode, uint8_t conversion, uint8_t threshold) {
		const uint8_t luminance = (kLuminance.red[rgb565 >> 11] + kLuminance.green[(rgb565 >> 5) & 0x3F] +
				kLuminance.blue[rgb565 & 0x1F]) >> 8;
		if (bwMode) return monochrome_is_white(luminance, x, y, conversion, threshold) ? 15 : 0;
		return (uint16_t(luminance) * 15 + 127) / 255;
}

constexpr int kPinSck = EPAPER_SPI_SCK_PIN;
constexpr int kPinMiso = EPAPER_SPI_MISO_PIN;
constexpr int kPinMosi = EPAPER_SPI_MOSI_PIN;
constexpr int kPinCs = EPAPER_CS_PIN;
constexpr int kPinBusy = EPAPER_BUSY_PIN;
constexpr int kPinReset = EPAPER_RESET_PIN;
constexpr uint16_t kVcomMillivolts = IT8951_VCOM_MV;
constexpr bool kE1003Protocol = IT8951_E1003_PROTOCOL;

struct PanelRegion {
		uint16_t x;
		uint16_t y;
		uint16_t width;
		uint16_t height;
};

SPIClass s_spi(HSPI);
SPISettings s_spiSettings(kE1003Protocol ? 10000000 : 24000000, MSBFIRST, SPI_MODE0);
SPISettings s_spiReadSettings(kE1003Protocol ? 10000000 : 1000000, MSBFIRST, SPI_MODE0);
uint32_t s_imageBufferAddress = 0;
#if HAS_DISPLAY_DIAGNOSTICS
uint64_t s_hrdyWaitUs = 0;
#endif

bool waitHrdy(uint32_t timeoutMs = kE1003Protocol ? kHrdyTimeoutMs : 10000) {
#if HAS_DISPLAY_DIAGNOSTICS
		const uint32_t startUs = micros();
#endif
		const uint32_t startMs = millis();
		while (digitalRead(kPinBusy) == LOW) {
			if (millis() - startMs >= timeoutMs) {
#if HAS_DISPLAY_DIAGNOSTICS
				s_hrdyWaitUs += uint32_t(micros() - startUs);
#endif
				LOGW("Epaper", "IT8951 HRDY timeout after %lums", (unsigned long)timeoutMs);
				return false;
			}
			delay(1);
		}
#if HAS_DISPLAY_DIAGNOSTICS
		s_hrdyWaitUs += uint32_t(micros() - startUs);
#endif
		return true;
}

uint16_t transferWord(uint16_t value) {
		if (kE1003Protocol) return s_spi.transfer16(value);
		const uint16_t high = s_spi.transfer(value >> 8);
		return (high << 8) | s_spi.transfer(value & 0xFF);
}

bool writeCommand(uint16_t command) {
		if (!waitHrdy()) return false;
		s_spi.beginTransaction(s_spiSettings);
		digitalWrite(kPinCs, LOW);
		transferWord(0x6000);
		if (!waitHrdy()) {
			digitalWrite(kPinCs, HIGH);
			s_spi.endTransaction();
			return false;
		}
		transferWord(command);
		digitalWrite(kPinCs, HIGH);
		s_spi.endTransaction();
		return true;
}

bool writeData(uint16_t data) {
		if (!waitHrdy()) return false;
		s_spi.beginTransaction(s_spiSettings);
		digitalWrite(kPinCs, LOW);
		transferWord(0x0000);
		if (!waitHrdy()) {
			digitalWrite(kPinCs, HIGH);
			s_spi.endTransaction();
			return false;
		}
		transferWord(data);
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
		s_spi.beginTransaction(s_spiReadSettings);
		digitalWrite(kPinCs, LOW);
		transferWord(0x1000);
		if (!waitHrdy()) {
			digitalWrite(kPinCs, HIGH);
			s_spi.endTransaction();
			return false;
		}
		transferWord(0);
		if (!waitHrdy()) {
			digitalWrite(kPinCs, HIGH);
			s_spi.endTransaction();
			return false;
		}
		for (uint8_t index = 0; index < 20; ++index) info[index] = transferWord(0);
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
		pinMode(kPinBusy, kE1003Protocol ? INPUT_PULLUP : INPUT);
		pinMode(kPinReset, OUTPUT);
		digitalWrite(kPinCs, HIGH);
		for (int pin : {IT8951_PANEL_POWER_PIN, IT8951_CONTROLLER_POWER_PIN}) {
			if (pin < 0) continue;
			pinMode(pin, OUTPUT);
			digitalWrite(pin, HIGH);
		}
		digitalWrite(kPinReset, HIGH);
		delay(10);
		digitalWrite(kPinReset, LOW);
		delay(kE1003Protocol ? 10 : 200);
		digitalWrite(kPinReset, HIGH);
		delay(kE1003Protocol ? 10 : 200);
		s_spi.begin(kPinSck, kPinMiso, kPinMosi, -1);
		if (!waitHrdy(kE1003Protocol ? kStartupTimeoutMs : 10000) ||
				!writeCommand(kCommandSystemRun) || !readDeviceInfo()) return false;
		// The Waveshare HAT requires I80 packed mode; E1003 firmware initializes it itself.
		if (!kE1003Protocol && !writeRegister(0x0004, 0x0001)) return false;
		// E1003 firmware uses selector 2; the standard HAT uses selector 1.
		if (!writeCommand(kCommandSetVcom) || !writeData(kE1003Protocol ? 2 : 1) ||
				!writeData(kVcomMillivolts)) return false;
		if (kE1003Protocol) return true;
		if (!writeCommand(kCommandSetVcom) || !writeData(0) || !waitHrdy()) return false;
		s_spi.beginTransaction(s_spiReadSettings);
		digitalWrite(kPinCs, LOW);
		transferWord(0x1000);
		bool ready = waitHrdy();
		uint16_t actualVcom = 0;
		if (ready) {
			transferWord(0);
			ready = waitHrdy();
			if (ready) actualVcom = transferWord(0);
		}
		digitalWrite(kPinCs, HIGH);
		s_spi.endTransaction();
		if (!ready) return false;
		if (actualVcom != kVcomMillivolts) {
			LOGE("Epaper", "IT8951 VCOM mismatch: requested -%umV, read -%umV", kVcomMillivolts, actualVcom);
			return false;
		}
		LOGI("Epaper", "IT8951 VCOM verified: -%umV", actualVcom);
		return true;
}

bool uploadFramebuffer(const uint8_t* framebuffer, const PanelRegion& region
#if HAS_DISPLAY_DIAGNOSTICS
		, uint32_t& uploadedBytes
#endif
) {
		if (!writeRegister(kRegisterImageBufferAddress, (uint16_t)(s_imageBufferAddress & 0xFFFF)) ||
				!writeRegister(kRegisterImageBufferAddress + 2, (uint16_t)(s_imageBufferAddress >> 16))) return false;
		if (kE1003Protocol && (!writeCommand(kCommandSetTemperature) || !writeData(0x0001) || !writeData(16))) return false;

		const uint16_t controllerX = kE1003Protocol ? kPanelWidth - region.x - region.width : region.x;
		const uint16_t arguments[] = {
				(uint16_t)((kE1003Protocol ? 0 : 0x0100) | (kImageMode4Bpp << 4)),
				controllerX, region.y, region.width, region.height,
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
		transferWord(0x0000);
		if (!waitHrdy()) {
			digitalWrite(kPinCs, HIGH);
			s_spi.endTransaction();
			return false;
		}
		for (uint16_t row = 0; row < region.height; ++row) {
			if ((row & 0x3F) == 0 && ota_activity_is_active()) {
				digitalWrite(kPinCs, HIGH);
				s_spi.endTransaction();
				writeCommand(kCommandLoadImageEnd);
				LOGI("Epaper", "IT8951 upload deferred for OTA");
				return false;
			}
			const uint8_t* sourceRow = framebuffer + (size_t)(region.y + row) * (kPanelWidth / 2) + region.x / 2;
			if (kE1003Protocol) {
				for (uint16_t word = 0; word < wordsPerRow; ++word) {
					const uint16_t sourceWord = wordsPerRow - 1 - word;
					rowBuffer[word * 2] = sourceRow[sourceWord * 2];
					rowBuffer[word * 2 + 1] = sourceRow[sourceWord * 2 + 1];
				}
				s_spi.transfer(rowBuffer, rowBytes);
			} else {
				s_spi.writeBytes(sourceRow, rowBytes);
			}
#if HAS_DISPLAY_DIAGNOSTICS
			uploadedBytes += rowBytes;
#endif
			if ((row & 0x3F) == 0) yield();
		}
		digitalWrite(kPinCs, HIGH);
		s_spi.endTransaction();
		return writeCommand(kCommandLoadImageEnd);
}

bool refreshPanel(const PanelRegion& region, uint8_t waveform) {
		const uint16_t controllerX = kE1003Protocol ? kPanelWidth - region.x - region.width : region.x;
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

IT8951EpaperDriver::IT8951EpaperDriver(DeviceConfig* cfg)
		: config(cfg), currentX(0), currentY(0), currentW(0), currentH(0), rotation(0),
				panelMode(cfg ? cfg->epaper_render_mode : EPAPER_DEFAULT_RENDER_MODE),
				bwConversion(cfg ? cfg->epaper_bw_conversion : uint8_t(MONOCHROME_CONVERSION_THRESHOLD)),
				bwThreshold(cfg ? cfg->epaper_bw_threshold : MONOCHROME_THRESHOLD), lastRefreshMs(0), lastChangeMs(0),
			bwPartialUpdatesSinceFull(0), grayscalePartialUpdatesSinceFull(0), drawingFramebuffer(nullptr), presentedFramebuffer(nullptr), framebufferMutex(nullptr),
			pendingChanges(false), fullRefreshRequested(false), hasPresentedFrame(false), initialized(false), dirtyRegionValid(false),
			dirtyX(0), dirtyY(0), dirtyX2(0), dirtyY2(0) {}

IT8951EpaperDriver::~IT8951EpaperDriver() {
		if (framebufferMutex) vSemaphoreDelete(framebufferMutex);
		if (drawingFramebuffer) heap_caps_free(drawingFramebuffer);
		if (presentedFramebuffer) heap_caps_free(presentedFramebuffer);
}

void IT8951EpaperDriver::init() {
		if (panelMode != EPAPER_RENDER_MODE_BW && panelMode != EPAPER_RENDER_MODE_GRAYSCALE) {
			panelMode = EPAPER_RENDER_MODE_GRAYSCALE;
		}
		framebufferMutex = xSemaphoreCreateMutex();
		drawingFramebuffer = (uint8_t*)heap_caps_malloc(framebufferBytes(), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
		presentedFramebuffer = (uint8_t*)heap_caps_malloc(framebufferBytes(), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
		if (!framebufferMutex || !drawingFramebuffer || !presentedFramebuffer) {
			LOGE("Epaper", "IT8951 framebuffer or mutex allocation failed");
			return;
		}
		memset(drawingFramebuffer, 0xFF, framebufferBytes());
		memset(presentedFramebuffer, 0x00, framebufferBytes());
		if (!initializeController()) {
			LOGE("Epaper", "IT8951 initialization failed");
			return;
		}
		initialized = true;
		LOGI("Epaper", "IT8951 interactive panel initialized (%dx%d, %s)", width(), height(),
				usesBwMode() ? "B/W" : "grayscale");
}

void IT8951EpaperDriver::setRotation(uint8_t value) { rotation = value & 0x03; }
int IT8951EpaperDriver::width() { return (rotation & 1) ? DISPLAY_HEIGHT : DISPLAY_WIDTH; }
int IT8951EpaperDriver::height() { return (rotation & 1) ? DISPLAY_WIDTH : DISPLAY_HEIGHT; }
void IT8951EpaperDriver::setBacklight(bool) {}
void IT8951EpaperDriver::setBacklightBrightness(uint8_t) {}
uint8_t IT8951EpaperDriver::getBacklightBrightness() { return 0; }
bool IT8951EpaperDriver::hasBacklightControl() { return false; }
void IT8951EpaperDriver::applyDisplayFixes() {}
void IT8951EpaperDriver::startWrite() {}
void IT8951EpaperDriver::endWrite() {}

void IT8951EpaperDriver::setAddrWindow(int16_t x, int16_t y, uint16_t w, uint16_t h) {
		currentX = x;
		currentY = y;
		currentW = w;
		currentH = h;
}

bool IT8951EpaperDriver::usesBwMode() const { return panelMode == EPAPER_RENDER_MODE_BW; }
size_t IT8951EpaperDriver::framebufferBytes() const { return kFramebufferBytes; }

void IT8951EpaperDriver::markDirtyRegion(int16_t x, int16_t y, uint16_t w, uint16_t h) {
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

void IT8951EpaperDriver::writePixel(int16_t x, int16_t y, uint16_t rgb565) {
		if (x < 0 || y < 0 || x >= width() || y >= height()) return;
		int16_t drawX = x;
		int16_t drawY = y;
		switch (rotation) {
			case 1: { const int16_t oldX = drawX; drawX = kPanelWidth - 1 - drawY; drawY = oldX; break; }
			case 2: drawX = kPanelWidth - 1 - drawX; drawY = kPanelHeight - 1 - drawY; break;
			case 3: { const int16_t oldX = drawX; drawX = drawY; drawY = kPanelHeight - 1 - oldX; break; }
		}
		const uint8_t gray = pixelGray(rgb565, x, y, usesBwMode(), bwConversion, bwThreshold);
		const size_t offset = (size_t)drawY * (kPanelWidth / 2) + drawX / 2;
		if (drawX & 1) drawingFramebuffer[offset] = (drawingFramebuffer[offset] & 0xF0) | gray;
		else drawingFramebuffer[offset] = (drawingFramebuffer[offset] & 0x0F) | (gray << 4);
}

bool IT8951EpaperDriver::fullRefreshPending() {
		portENTER_CRITICAL(&fullRefreshRequestMux);
		const bool requested = fullRefreshRequested;
		portEXIT_CRITICAL(&fullRefreshRequestMux);
		return requested;
}

bool IT8951EpaperDriver::consumeFullRefreshRequest() {
		portENTER_CRITICAL(&fullRefreshRequestMux);
		const bool requested = fullRefreshRequested;
		fullRefreshRequested = false;
		portEXIT_CRITICAL(&fullRefreshRequestMux);
		return requested;
}

void IT8951EpaperDriver::pushColors(uint16_t* data, uint32_t, bool) {
		if (!initialized || !data || !framebufferMutex) return;
#if HAS_DISPLAY_DIAGNOSTICS
		const uint32_t waitStartUs = micros();
#endif
		xSemaphoreTake(framebufferMutex, portMAX_DELAY);
#if HAS_DISPLAY_DIAGNOSTICS
		const uint32_t conversionStartUs = micros();
		const uint32_t waitUs = conversionStartUs - waitStartUs;
#endif
		const uint32_t sourceStride = flushSrcStride ? flushSrcStride / sizeof(uint16_t) : currentW;
		const bool packedRows = !(rotation & 1) && currentX >= 0 && currentY >= 0 &&
				int32_t(currentX) + currentW <= kPanelWidth && int32_t(currentY) + currentH <= kPanelHeight;
		const bool reverse = rotation == 2;
		const int step = reverse ? -1 : 1;
		const bool bwMode = usesBwMode();
		for (uint16_t row = 0; row < currentH; ++row) {
			const uint16_t* source = data + row * sourceStride;
			if (!packedRows) {
				for (uint16_t column = 0; column < currentW; ++column) writePixel(currentX + column, currentY + row, source[column]);
				continue;
			}
			const int16_t logicalY = currentY + row;
			const int nativeY = reverse ? kPanelHeight - 1 - logicalY : logicalY;
			const int nativeX = reverse ? kPanelWidth - currentX - currentW : currentX;
			uint8_t* destination = drawingFramebuffer + size_t(nativeY) * (kPanelWidth / 2) + nativeX / 2;
			int column = reverse ? int(currentW) - 1 : 0;
			uint16_t remaining = currentW;
			auto gray = [&](int index) {
				return pixelGray(source[index], currentX + index, logicalY, bwMode, bwConversion, bwThreshold);
			};
			// Only boundary pixels need to preserve the adjacent framebuffer nibble.
			if ((nativeX & 1) && remaining) {
				*destination = (*destination & 0xF0) | gray(column);
				++destination;
				column += step;
				--remaining;
			}
			while (remaining >= 2) {
				const uint8_t high = gray(column);
				column += step;
				const uint8_t low = gray(column);
				column += step;
				*destination++ = (high << 4) | low;
				remaining -= 2;
			}
			if (remaining) *destination = (*destination & 0x0F) | (gray(column) << 4);
		}
		markDirtyRegion(currentX, currentY, currentW, currentH);
#if HAS_DISPLAY_DIAGNOSTICS
		if (!pendingChanges) firstChangeUs = conversionStartUs;
#endif
		pendingChanges = true;
		lastChangeMs = millis();
#if HAS_DISPLAY_DIAGNOSTICS
		const uint32_t conversionUs = uint32_t(micros() - conversionStartUs);
#endif
		xSemaphoreGive(framebufferMutex);
#if HAS_DISPLAY_DIAGNOSTICS
		uint32_t yieldUs = 0;
#endif
		if ((uint32_t)currentW * currentH >= 1024) {
#if HAS_DISPLAY_DIAGNOSTICS
			const uint32_t yieldStartUs = micros();
#endif
			vTaskDelay(1);
#if HAS_DISPLAY_DIAGNOSTICS
			yieldUs = uint32_t(micros() - yieldStartUs);
#endif
		}
#if HAS_DISPLAY_DIAGNOSTICS
		portENTER_CRITICAL(&perfMux);
		++perf.flush_calls;
		perf.flush_pixels += uint32_t(currentW) * currentH;
		perf.conversion_us += conversionUs;
		perf.yield_us += yieldUs;
		perf.framebuffer_wait_us += waitUs;
		portEXIT_CRITICAL(&perfMux);
#endif
}

void IT8951EpaperDriver::present() {
		if (!initialized || !framebufferMutex) return;
#if HAS_DISPLAY_DIAGNOSTICS
		DisplayPresentationSample sample;
		const uint32_t presentStartUs = micros();
#endif
		while (true) {
			if (ota_activity_is_active()) {
#if HAS_DISPLAY_DIAGNOSTICS
				portENTER_CRITICAL(&perfMux);
				++perf.ota_deferrals;
				portEXIT_CRITICAL(&perfMux);
#endif
				return;
			}
#if HAS_DISPLAY_DIAGNOSTICS
			const uint32_t waitStartUs = micros();
#endif
			xSemaphoreTake(framebufferMutex, portMAX_DELAY);
#if HAS_DISPLAY_DIAGNOSTICS
			sample.framebuffer_wait_us += uint32_t(micros() - waitStartUs);
#endif
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
#if HAS_DISPLAY_DIAGNOSTICS
			const uint32_t coalesceStartUs = micros();
#endif
			vTaskDelay(pdMS_TO_TICKS(waitMs) ?: 1);
#if HAS_DISPLAY_DIAGNOSTICS
			sample.coalesce_us += uint32_t(micros() - coalesceStartUs);
#endif
		}

		const bool forceFull = consumeFullRefreshRequest();
		if (!pendingChanges && !forceFull) {
			xSemaphoreGive(framebufferMutex);
			return;
		}
#if HAS_DISPLAY_DIAGNOSTICS
		const uint32_t compareStartUs = micros();
#endif
		const bool unchanged = !forceFull && hasPresentedFrame &&
				memcmp(drawingFramebuffer, presentedFramebuffer, framebufferBytes()) == 0;
#if HAS_DISPLAY_DIAGNOSTICS
		sample.compare_us = uint32_t(micros() - compareStartUs);
#endif
		if (unchanged) {
			pendingChanges = false;
			dirtyRegionValid = false;
			xSemaphoreGive(framebufferMutex);
#if HAS_DISPLAY_DIAGNOSTICS
			portENTER_CRITICAL(&perfMux);
			++perf.unchanged_skips;
			portEXIT_CRITICAL(&perfMux);
#endif
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
#if HAS_DISPLAY_DIAGNOSTICS
		const uint32_t firstDirtyUs = pendingChanges ? firstChangeUs : presentStartUs;
		const uint32_t copyStartUs = micros();
#endif
		memcpy(presentedFramebuffer, drawingFramebuffer, framebufferBytes());
#if HAS_DISPLAY_DIAGNOSTICS
		sample.copy_us = uint32_t(micros() - copyStartUs);
#endif
		pendingChanges = false;
		dirtyRegionValid = false;
		xSemaphoreGive(framebufferMutex);

		const uint32_t startMs = millis();
#if HAS_DISPLAY_DIAGNOSTICS
		const uint64_t hrdyBeforeUs = s_hrdyWaitUs;
		const uint32_t uploadStartUs = micros();
#endif
		const bool uploaded = !ota_activity_is_active() &&
				uploadFramebuffer(presentedFramebuffer, region
#if HAS_DISPLAY_DIAGNOSTICS
						, sample.uploaded_bytes
#endif
				);
		const uint8_t waveform = fullRefresh || !usesBwMode() ? kWaveformGc16 : kWaveformDu;
#if HAS_DISPLAY_DIAGNOSTICS
		sample.upload_us = uint32_t(micros() - uploadStartUs);
		sample.waveform = waveform;
		const uint32_t controllerStartUs = micros();
#endif
		const bool presented = uploaded && !ota_activity_is_active() && refreshPanel(region, waveform);
#if HAS_DISPLAY_DIAGNOSTICS
		sample.controller_us = uint32_t(micros() - controllerStartUs);
		sample.hrdy_wait_us = static_cast<uint32_t>(s_hrdyWaitUs - hrdyBeforeUs);
		const bool otaDeferred = !presented && ota_activity_is_active();
		const uint32_t finalWaitStartUs = micros();
#endif
		xSemaphoreTake(framebufferMutex, portMAX_DELAY);
#if HAS_DISPLAY_DIAGNOSTICS
		sample.framebuffer_wait_us += uint32_t(micros() - finalWaitStartUs);
#endif
		if (!presented) {
			hasPresentedFrame = false;
#if HAS_DISPLAY_DIAGNOSTICS
			if (!pendingChanges) firstChangeUs = firstDirtyUs;
#endif
			pendingChanges = true;
			dirtyX = 0;
			dirtyY = 0;
			dirtyX2 = kPanelWidth - 1;
			dirtyY2 = kPanelHeight - 1;
			dirtyRegionValid = true;
		} else {
			hasPresentedFrame = true;
			lastRefreshMs = millis();
			if (fullRefresh) {
				bwPartialUpdatesSinceFull = 0;
				grayscalePartialUpdatesSinceFull = 0;
			} else if (usesBwMode()) ++bwPartialUpdatesSinceFull;
			else ++grayscalePartialUpdatesSinceFull;
		}
		const uint32_t completed_ms = millis();
		xSemaphoreGive(framebufferMutex);
#if HAS_DISPLAY_DIAGNOSTICS
		sample.completed_ms = completed_ms;
		sample.total_us = uint32_t(micros() - presentStartUs);
		sample.dirty_to_ready_us = uint32_t(micros() - firstDirtyUs);
		sample.x = region.x;
		sample.y = region.y;
		sample.width = region.width;
		sample.height = region.height;
		sample.full = fullRefresh;
		sample.success = presented;
		portENTER_CRITICAL(&perfMux);
		perf.add(sample);
		if (otaDeferred) ++perf.ota_deferrals;
		portEXIT_CRITICAL(&perfMux);
#endif
		static uint32_t last_report_ms = 0;
		static uint32_t presentations = 0;
		if (!presented) {
			LOGE("Epaper", "IT8951 presentation failed");
		} else {
			++presentations;
			if (forceFull || scheduledFull || completed_ms - last_report_ms >= 60000) {
#if HAS_DISPLAY_DIAGNOSTICS
				LOGI("Epaper", "Refresh: n=%lu %s %ux%u total=%lums dirty=%lu wait=%lu spi=%lu ctrl=%lu (ms) f=%u s=%u",
						(unsigned long)presentations, fullRefresh ? "full" : "partial", region.width, region.height,
						(unsigned long)(sample.total_us / 1000), (unsigned long)(sample.dirty_to_ready_us / 1000),
						(unsigned long)(sample.coalesce_us / 1000),
						(unsigned long)(sample.upload_us / 1000), (unsigned long)(sample.controller_us / 1000),
						unsigned(forceFull), unsigned(scheduledFull));
#else
				LOGI("Epaper", "Refresh: count=%lu mode=%s dur=%lums forced=%u scheduled=%u",
						(unsigned long)presentations, fullRefresh ? "full" : "partial",
						(unsigned long)(completed_ms - startMs), unsigned(forceFull), unsigned(scheduledFull));
#endif
				last_report_ms = completed_ms;
			}
			LOGT("Epaper", "%s refresh %ux%u completed in %lums (dirty=%ux%u@%u,%u %lu%%; forced=%d scheduled=%d; partials=%lu/%u)",
					fullRefresh ? "full GC16" : (usesBwMode() ? "B/W partial DU" : "grayscale partial GC16"),
					region.width, region.height, (unsigned long)(completed_ms - startMs),
					dirtyRegion.width, dirtyRegion.height, dirtyRegion.x, dirtyRegion.y, (unsigned long)dirtyCoveragePercent,
					forceFull, scheduledFull, (unsigned long)partialCountBefore,
					settings.epaper_full_refresh_threshold);
		}
}

#if HAS_DISPLAY_DIAGNOSTICS
bool IT8951EpaperDriver::getPresentationStats(DisplayPresentationStats* out) {
		if (!out || !initialized) return false;
		portENTER_CRITICAL(&perfMux);
		*out = perf;
		portEXIT_CRITICAL(&perfMux);
		return true;
}
#endif

bool IT8951EpaperDriver::requestFullRefresh() {
		if (!initialized) return false;
		portENTER_CRITICAL(&fullRefreshRequestMux);
		fullRefreshRequested = true;
		portEXIT_CRITICAL(&fullRefreshRequestMux);
		return true;
}