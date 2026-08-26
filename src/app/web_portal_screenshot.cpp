#include "web_portal_screenshot.h"

#if HAS_DISPLAY

#include "display_manager.h"
#include "web_portal_auth.h"
#include "web_portal_cors.h"
#include "log_manager.h"
#if HAS_TOUCH
#include "touch_manager.h"
#include "web_portal_json.h"
#endif

#include <lvgl.h>
#include <esp_heap_caps.h>
#include <memory>
#include <new>

#ifdef CONFIG_IDF_TARGET_ESP32P4
#include "driver/jpeg_encode.h"
#include "dma2d_arbiter.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#endif

#define TAG "Screenshot"

static const size_t BMP_HEADER_SIZE = 54;
static const uint8_t DEFAULT_JPEG_QUALITY = 85;

struct ScreenshotContext {
		uint8_t* pixelBuf = nullptr;
		uint8_t* jpegBuf = nullptr;
		size_t jpegSize = 0;
		uint16_t width = 0;
		uint16_t height = 0;
		uint32_t pixelStride = 0;
		uint32_t rowStride = 0;   // BMP row stride (padded to 4 bytes)
		uint32_t totalSize = 0;   // Total BMP file size
		uint8_t* rowBuf = nullptr; // Reusable BGR888 row conversion buffer (PSRAM)

		void releasePayload() {
				if (pixelBuf) {
						free(pixelBuf);
						pixelBuf = nullptr;
				}
				if (jpegBuf) {
						free(jpegBuf);
						jpegBuf = nullptr;
				}
				if (rowBuf) {
						free(rowBuf);
						rowBuf = nullptr;
				}
		}

		~ScreenshotContext() {
				releasePayload();
		}
};

enum class ScreenshotFormat {
		Bmp,
		Jpeg,
};

struct ScreenshotCaptureResult {
		uint8_t* pixels = nullptr;
		uint16_t width = 0;
		uint16_t height = 0;
		uint32_t stride = 0;
};

struct ScreenshotCaptureRequest {
		ScreenshotCaptureResult* result;
};

#ifdef CONFIG_IDF_TARGET_ESP32P4
static jpeg_encoder_handle_t s_hw_jpeg = nullptr;
static bool s_hw_init_done = false;
static bool s_hw_ready = false;

static SemaphoreHandle_t jpegMutex() {
		static SemaphoreHandle_t mutex = xSemaphoreCreateMutex();
		return mutex;
}

static bool initHwJpeg() {
		if (s_hw_init_done) return s_hw_ready;
		s_hw_init_done = true;

		jpeg_encode_engine_cfg_t cfg = {};
		cfg.intr_priority = 0;
		cfg.timeout_ms = 500;
		if (jpeg_new_encoder_engine(&cfg, &s_hw_jpeg) != ESP_OK) {
				LOGE(TAG, "HW JPEG encoder init failed");
				return false;
		}

		s_hw_ready = true;
		LOGI(TAG, "HW JPEG encoder ready");
		return true;
}

static bool encodeJpeg(const ScreenshotCaptureResult* capture, uint8_t quality,
		uint8_t** jpegBuf, size_t* jpegSize) {
		SemaphoreHandle_t mutex = jpegMutex();
		if (!mutex || xSemaphoreTake(mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
				LOGE(TAG, "HW JPEG encoder busy");
				return false;
		}

		bool success = false;
		uint8_t* input = nullptr;
		uint8_t* output = nullptr;
		do {
				if (!initHwJpeg()) break;

				uint32_t width = capture->width;
				uint32_t height = capture->height;
				size_t rawSize = (size_t)width * height * sizeof(uint16_t);

				jpeg_encode_memory_alloc_cfg_t inputCfg = {};
				inputCfg.buffer_direction = JPEG_ENC_ALLOC_INPUT_BUFFER;
				size_t inputSize = 0;
				input = (uint8_t*)jpeg_alloc_encoder_mem(rawSize, &inputCfg, &inputSize);
				if (!input) {
						LOGE(TAG, "HW JPEG input alloc failed (%u bytes)", (unsigned)rawSize);
						break;
				}

				size_t packedStride = (size_t)width * sizeof(uint16_t);
				for (uint32_t row = 0; row < height; ++row) {
						memcpy(input + row * packedStride,
								capture->pixels + row * capture->stride,
								packedStride);
				}

				jpeg_encode_memory_alloc_cfg_t outputCfg = {};
				outputCfg.buffer_direction = JPEG_ENC_ALLOC_OUTPUT_BUFFER;
				size_t outputSize = 0;
				output = (uint8_t*)jpeg_alloc_encoder_mem(rawSize, &outputCfg, &outputSize);
				if (!output) {
						LOGE(TAG, "HW JPEG output alloc failed (%u bytes)", (unsigned)rawSize);
						break;
				}

				jpeg_encode_cfg_t encodeCfg = {};
				encodeCfg.width = width;
				encodeCfg.height = height;
				encodeCfg.src_type = JPEG_ENCODE_IN_FORMAT_RGB565;
				encodeCfg.sub_sample = JPEG_DOWN_SAMPLING_YUV420;
				encodeCfg.image_quality = quality;

				uint32_t encodedSize = 0;
				if (!dma2d_arbiter_acquire(2000)) {
						LOGW(TAG, "2D-DMA busy, deferring screenshot encode");
						break;
				}
				esp_err_t err = jpeg_encoder_process(
						s_hw_jpeg, &encodeCfg,
						input, (uint32_t)rawSize,
						output, (uint32_t)outputSize,
						&encodedSize);
				dma2d_arbiter_release();
				if (err != ESP_OK || encodedSize == 0) {
						LOGE(TAG, "HW JPEG encode failed (0x%x)", err);
						break;
				}

				*jpegBuf = output;
				*jpegSize = encodedSize;
				output = nullptr;
				success = true;
		} while (false);

		if (input) free(input);
		if (output) free(output);
		xSemaphoreGive(mutex);
		return success;
}
#endif

static void captureScreenshot(const void* opaque, bool* ok, char* msg, size_t msgLen) {
		const ScreenshotCaptureRequest* request = static_cast<const ScreenshotCaptureRequest*>(opaque);
		ScreenshotCaptureResult* result = request ? request->result : nullptr;
		if (!result) {
				snprintf(msg, msgLen, "invalid capture request");
				return;
		}

		lv_draw_buf_t* drawBuf = lv_snapshot_take(lv_screen_active(), LV_COLOR_FORMAT_RGB565);
		if (!drawBuf) {
				snprintf(msg, msgLen, "screenshot capture failed");
				return;
		}

		result->width = drawBuf->header.w;
		result->height = drawBuf->header.h;
		result->stride = (uint32_t)result->width * sizeof(uint16_t);
		const size_t pixelBytes = (size_t)result->stride * result->height;
		result->pixels = static_cast<uint8_t*>(
				heap_caps_malloc(pixelBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
		if (!result->pixels) {
				lv_draw_buf_destroy(drawBuf);
				snprintf(msg, msgLen, "screenshot buffer allocation failed");
				return;
		}

		for (uint16_t row = 0; row < result->height; ++row) {
				memcpy(result->pixels + (size_t)row * result->stride,
						drawBuf->data + (size_t)row * drawBuf->header.stride,
						result->stride);
		}
		lv_draw_buf_destroy(drawBuf);
		*ok = true;
}

static void cleanupAbandonedScreenshot(const void* opaque) {
		const ScreenshotCaptureRequest* request = static_cast<const ScreenshotCaptureRequest*>(opaque);
		ScreenshotCaptureResult* result = request ? request->result : nullptr;
		if (!result) return;
		if (result->pixels) free(result->pixels);
		delete result;
}

static void writeBmpHeader(uint8_t* buf, uint16_t width, uint16_t height, uint32_t fileSize) {
		// BMP file header (14 bytes)
		buf[0] = 'B'; buf[1] = 'M';
		memcpy(&buf[2], &fileSize, 4);
		memset(&buf[6], 0, 4);              // Reserved
		uint32_t dataOffset = BMP_HEADER_SIZE;
		memcpy(&buf[10], &dataOffset, 4);

		// DIB header (BITMAPINFOHEADER, 40 bytes)
		uint32_t dibSize = 40;
		memcpy(&buf[14], &dibSize, 4);
		int32_t w = width;
		int32_t h = height;               // Positive = bottom-up row order
		memcpy(&buf[18], &w, 4);
		memcpy(&buf[22], &h, 4);
		uint16_t planes = 1;
		memcpy(&buf[26], &planes, 2);
		uint16_t bpp = 24;
		memcpy(&buf[28], &bpp, 2);
		memset(&buf[30], 0, 24);           // Compression, sizes, resolution, colors
}

// Convert one row of RGB565 pixels to BGR888 (BMP byte order)
static void convertRow(const uint16_t* src, uint8_t* dst, uint16_t width) {
		for (uint16_t x = 0; x < width; x++) {
				uint16_t px = src[x];
				uint8_t r = (px >> 11) & 0x1F;
				uint8_t g = (px >> 5) & 0x3F;
				uint8_t b = px & 0x1F;
				dst[x * 3 + 0] = (b << 3) | (b >> 2);   // Blue  (5-bit → 8-bit)
				dst[x * 3 + 1] = (g << 2) | (g >> 4);   // Green (6-bit → 8-bit)
				dst[x * 3 + 2] = (r << 3) | (r >> 2);   // Red   (5-bit → 8-bit)
		}
}

static void sendBmp(AsyncWebServerRequest* request, ScreenshotCaptureResult* capture) {
		uint16_t width = capture->width;
		uint16_t height = capture->height;
		uint32_t rowStride = ((width * 3 + 3) / 4) * 4;
		uint32_t totalSize = BMP_HEADER_SIZE + rowStride * height;

		auto ctx = std::make_shared<ScreenshotContext>();
		ctx->pixelBuf = capture->pixels;
		capture->pixels = nullptr;
		ctx->width = width;
		ctx->height = height;
		ctx->pixelStride = capture->stride;
		ctx->rowStride = rowStride;
		ctx->totalSize = totalSize;
		delete capture;

		// Allocate row conversion buffer in PSRAM
		ctx->rowBuf = (uint8_t*)heap_caps_malloc(rowStride, MALLOC_CAP_SPIRAM);
		if (!ctx->rowBuf) {
				LOGE(TAG, "Row buffer alloc failed");
				request->send(500, "text/plain", "Out of memory");
				return;
		}

		LOGI(TAG, "Capture %dx%d, BMP %u bytes", width, height, totalSize);

		AsyncWebServerResponse *response = request->beginChunkedResponse(
				"image/bmp",
				[ctx](uint8_t *buffer, size_t maxLen, size_t index) -> size_t {
						if (index >= ctx->totalSize) return 0;

						size_t written = 0;

						// Write BMP header if still in header range
						if (index < BMP_HEADER_SIZE) {
								uint8_t header[BMP_HEADER_SIZE];
								writeBmpHeader(header, ctx->width, ctx->height, ctx->totalSize);
								size_t count = BMP_HEADER_SIZE - index;
								if (count > maxLen) count = maxLen;
								memcpy(buffer, header + index, count);
								written += count;
						}

						// Stream pixel data (RGB565 → BGR888, bottom-up rows)
						while (written < maxLen && (index + written) < ctx->totalSize) {
								size_t pixelPos = (index + written) - BMP_HEADER_SIZE;
								uint32_t bmpRow = pixelPos / ctx->rowStride;
								uint32_t colOffset = pixelPos % ctx->rowStride;

								// Convert the source image row (top-down) to BGR888
								uint32_t imgRow = (ctx->height - 1) - bmpRow;
								const uint8_t* srcRow = ctx->pixelBuf + (imgRow * ctx->pixelStride);
								convertRow((const uint16_t*)srcRow, ctx->rowBuf, ctx->width);

								// Zero-fill BMP row padding
								for (uint32_t p = ctx->width * 3; p < ctx->rowStride; p++) {
										ctx->rowBuf[p] = 0;
								}

								// Copy as much of this row as fits in the output buffer
								size_t rowRemaining = ctx->rowStride - colOffset;
								size_t canWrite = maxLen - written;
								size_t toCopy = rowRemaining < canWrite ? rowRemaining : canWrite;
								memcpy(buffer + written, ctx->rowBuf + colOffset, toCopy);
								written += toCopy;
						}

						if (index + written >= ctx->totalSize) ctx->releasePayload();
						return written;
				}
		);

		web_portal_add_cors_headers(response);
		request->send(response);
}

#ifdef CONFIG_IDF_TARGET_ESP32P4
static bool sendJpeg(AsyncWebServerRequest* request, ScreenshotCaptureResult* capture, uint8_t quality) {
		uint8_t* jpegBuf = nullptr;
		size_t jpegSize = 0;
		if (!encodeJpeg(capture, quality, &jpegBuf, &jpegSize)) return false;

		auto ctx = std::make_shared<ScreenshotContext>();
		ctx->jpegBuf = jpegBuf;
		ctx->jpegSize = jpegSize;
		ctx->width = capture->width;
		ctx->height = capture->height;
		free(capture->pixels);
		delete capture;

		LOGI(TAG, "Capture %dx%d, JPEG %u bytes, quality %u",
				ctx->width, ctx->height, (unsigned)ctx->jpegSize, quality);

		AsyncWebServerResponse* response = request->beginChunkedResponse(
				"image/jpeg",
				[ctx](uint8_t* buffer, size_t maxLen, size_t index) -> size_t {
						if (index >= ctx->jpegSize) return 0;
						size_t count = ctx->jpegSize - index;
						if (count > maxLen) count = maxLen;
						memcpy(buffer, ctx->jpegBuf + index, count);
						if (index + count >= ctx->jpegSize) ctx->releasePayload();
						return count;
				}
		);
		web_portal_add_cors_headers(response);
		request->send(response);
		return true;
}
#endif

static bool parseQuality(AsyncWebServerRequest* request, uint8_t* quality) {
		*quality = DEFAULT_JPEG_QUALITY;
		if (!request->hasParam("quality")) return true;

		const String& value = request->getParam("quality")->value();
		if (value.length() == 0) return false;
		for (size_t i = 0; i < value.length(); ++i) {
				if (value[i] < '0' || value[i] > '9') return false;
		}
		long parsed = value.toInt();
		if (parsed < 1 || parsed > 100) return false;
		*quality = (uint8_t)parsed;
		return true;
}

// GET /api/screenshot — capture display as JPEG on ESP32-P4, BMP elsewhere
void handleGetScreenshot(AsyncWebServerRequest *request) {
		if (!portal_auth_gate(request)) return;

		#ifdef CONFIG_IDF_TARGET_ESP32P4
		ScreenshotFormat format = ScreenshotFormat::Jpeg;
		#else
		ScreenshotFormat format = ScreenshotFormat::Bmp;
		#endif

		if (request->hasParam("format")) {
				String value = request->getParam("format")->value();
				value.toLowerCase();
				if (value == "bmp") {
						format = ScreenshotFormat::Bmp;
				} else if (value == "jpg") {
						#ifdef CONFIG_IDF_TARGET_ESP32P4
						format = ScreenshotFormat::Jpeg;
						#else
						request->send(400, "text/plain", "JPEG screenshots are not supported on this board");
						return;
						#endif
				} else {
						request->send(400, "text/plain", "Invalid format; expected bmp or jpg");
						return;
				}
		}

		uint8_t quality = DEFAULT_JPEG_QUALITY;
		if (!parseQuality(request, &quality)) {
				request->send(400, "text/plain", "Invalid quality; expected an integer from 1 to 100");
				return;
		}

		ScreenshotCaptureResult* capture = new (std::nothrow) ScreenshotCaptureResult();
		if (!capture) {
				request->send(500, "text/plain", "Out of memory");
				return;
		}
		ScreenshotCaptureRequest captureRequest = { capture };
		bool captureOk = false;
		char captureMessage[160] = {0};
		DisplayTaskDispatchResult dispatchResult = display_manager_dispatch(
				captureScreenshot, cleanupAbandonedScreenshot,
				&captureRequest, sizeof(captureRequest), 3000,
				&captureOk, captureMessage, sizeof(captureMessage));
		if (dispatchResult == DISPLAY_TASK_DISPATCH_BUSY) {
				delete capture;
				request->send(503, "text/plain", "Screenshot service busy");
				return;
		}
		if (dispatchResult == DISPLAY_TASK_DISPATCH_UNAVAILABLE) {
				delete capture;
				request->send(503, "text/plain", "Screenshot service unavailable");
				return;
		}
		if (dispatchResult == DISPLAY_TASK_DISPATCH_INVALID) {
				delete capture;
				request->send(500, "text/plain", "Invalid screenshot capture request");
				return;
		}
		if (dispatchResult == DISPLAY_TASK_DISPATCH_TOO_LARGE) {
				delete capture;
				request->send(500, "text/plain", "Screenshot capture request too large");
				return;
		}
		if (dispatchResult != DISPLAY_TASK_DISPATCH_OK) {
				request->send(504, "text/plain", "Screenshot capture timed out");
				return;
		}
		if (!captureOk) {
				delete capture;
				request->send(500, "text/plain",
						captureMessage[0] ? captureMessage : "Screenshot capture failed");
				return;
		}

		#ifdef CONFIG_IDF_TARGET_ESP32P4
		if (format == ScreenshotFormat::Jpeg) {
				if (sendJpeg(request, capture, quality)) return;
				LOGW(TAG, "Falling back to BMP screenshot");
		}
		#endif

		sendBmp(request, capture);
}

#if HAS_TOUCH
static bool parseTapCoordinate(const String& value, int32_t* coordinate) {
		if (!coordinate || value.length() == 0) return false;
		size_t index = 0;
		bool negative = false;
		if (value[0] == '-') {
			negative = true;
			index = 1;
		}
		if (index == value.length()) return false;

		int64_t parsed = 0;
		for (; index < value.length(); ++index) {
			const char c = value[index];
			if (c < '0' || c > '9') return false;
			const int digit = c - '0';
			if (parsed > (INT64_MAX - digit) / 10) return false;
			parsed = parsed * 10 + digit;
		}
		if (negative) parsed = -parsed;
		if (parsed < INT32_MIN || parsed > INT32_MAX) return false;
		*coordinate = (int32_t)parsed;
		return true;
}

void handlePostScreenTap(AsyncWebServerRequest *request) {
		if (!portal_auth_gate(request)) return;
		if (!request->hasParam("x") || !request->hasParam("y")) {
			web_portal_send_json_error(request, 400, "x and y are required integers");
			return;
		}

		int32_t x = 0;
		int32_t y = 0;
		if (!parseTapCoordinate(request->getParam("x")->value(), &x) ||
				!parseTapCoordinate(request->getParam("y")->value(), &y)) {
			web_portal_send_json_error(request, 400, "x and y must be base-10 integers");
			return;
		}

		switch (touch_manager_enqueue_tap(x, y)) {
			case TOUCH_MANAGER_ENQUEUE_QUEUED:
				request->send(202, "application/json", "{\"success\":true,\"message\":\"Tap queued\"}");
				return;
			case TOUCH_MANAGER_ENQUEUE_INVALID:
				web_portal_send_json_error(request, 400, "Coordinates outside active display");
				return;
			case TOUCH_MANAGER_ENQUEUE_BUSY:
				web_portal_send_json_error(request, 409, "Tap queue busy, retry");
				return;
			case TOUCH_MANAGER_ENQUEUE_UNAVAILABLE:
				web_portal_send_json_error(request, 503, "Touch input unavailable");
				return;
		}
}
#endif // HAS_TOUCH

#endif // HAS_DISPLAY
