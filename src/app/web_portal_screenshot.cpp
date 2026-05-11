#include "web_portal_screenshot.h"

#if HAS_DISPLAY

#include "display_manager.h"
#include "web_portal_auth.h"
#include "web_portal_cors.h"
#include "log_manager.h"

#include <lvgl.h>
#include <esp_heap_caps.h>
#include <memory>

#define TAG "Screenshot"

static const size_t BMP_HEADER_SIZE = 54;

struct ScreenshotContext {
		lv_draw_buf_t* drawBuf;
		uint16_t width;
		uint16_t height;
		uint32_t rowStride;   // BMP row stride (padded to 4 bytes)
		uint32_t totalSize;   // Total BMP file size
		uint8_t* rowBuf;      // Reusable BGR888 row conversion buffer (PSRAM)

		~ScreenshotContext() {
				if (drawBuf) lv_draw_buf_destroy(drawBuf);
				if (rowBuf) free(rowBuf);
		}
};

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

// GET /api/screenshot — capture display as 24-bit BMP
void handleGetScreenshot(AsyncWebServerRequest *request) {
		if (!portal_auth_gate(request)) return;

		// Acquire LVGL mutex with timeout
		if (!display_manager_try_lock(1000)) {
				request->send(503, "text/plain", "Display busy");
				return;
		}

		// Capture the active screen in RGB565 (native display format)
		lv_draw_buf_t* drawBuf = lv_snapshot_take(lv_screen_active(), LV_COLOR_FORMAT_RGB565);

		display_manager_unlock();

		if (!drawBuf) {
				LOGE(TAG, "lv_snapshot_take failed");
				request->send(500, "text/plain", "Screenshot capture failed");
				return;
		}

		uint16_t width = drawBuf->header.w;
		uint16_t height = drawBuf->header.h;
		uint32_t rowStride = ((width * 3 + 3) / 4) * 4;
		uint32_t totalSize = BMP_HEADER_SIZE + rowStride * height;

		auto ctx = std::make_shared<ScreenshotContext>();
		ctx->drawBuf = drawBuf;
		ctx->width = width;
		ctx->height = height;
		ctx->rowStride = rowStride;
		ctx->totalSize = totalSize;

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
								const uint8_t* srcRow = ctx->drawBuf->data + (imgRow * ctx->drawBuf->header.stride);
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

						return written;
				}
		);

		web_portal_add_cors_headers(response);
		request->send(response);
}

#endif // HAS_DISPLAY
