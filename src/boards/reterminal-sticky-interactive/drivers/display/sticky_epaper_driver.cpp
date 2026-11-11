#include "sticky_epaper_driver.h"
#include "app/drivers/monochrome_render.h"
#include "app/presented_frame.h"
#include "app/config_manager.h"
#include "app/board_hardware.h"
#include "app/log_manager.h"
#include "app/ota_activity.h"
#include <esp_heap_caps.h>
#include <cstring>

StickyEpaperDriver::~StickyEpaperDriver() { releaseResources(); }

void StickyEpaperDriver::releaseResources() {
    available.store(false);
    panel.end();
    if (mutex) vSemaphoreDelete(mutex);
    if (drawing) heap_caps_free(drawing);
    if (sending) heap_caps_free(sending);
    if (presented) heap_caps_free(presented);
    mutex = nullptr;
    drawing = sending = presented = nullptr;
    pending = forceFull = baselineValid = false;
    partialsSinceFull = 0;
}

void StickyEpaperDriver::init() {
    releaseResources();
    if (!BOARD_DISPLAY_PREPARE()) {
        LOGE("Sticky", "Display unavailable: board initialization failed");
        return;
    }
    const EpaperRefreshSettings settings = config_manager_get_epaper_refresh_settings();
    grayscale = settings.epaper_render_mode == EPAPER_RENDER_MODE_GRAYSCALE;
    bwConversion = settings.epaper_bw_conversion;
    bwThreshold = settings.epaper_bw_threshold;
    frameBytes = presented_frame_bytes(800, 480, grayscale ? 4 : 1);
    drawing = static_cast<uint8_t*>(heap_caps_malloc(frameBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    sending = static_cast<uint8_t*>(heap_caps_malloc(frameBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    presented = static_cast<uint8_t*>(heap_caps_malloc(frameBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    mutex = xSemaphoreCreateMutex();
    if (!drawing || !sending || !presented || !mutex) {
        LOGE("Sticky", "Unable to allocate display buffers or mutex");
        releaseResources();
        return;
    }
    memset(drawing, 0xff, frameBytes);
    memset(sending, 0xff, frameBytes);
    memset(presented, 0xff, frameBytes);
    if (!panel.begin()) {
        LOGE("Sticky", "Panel initialization failed");
        releaseResources();
        return;
    }
    available.store(true);
}

void StickyEpaperDriver::setAddrWindow(int16_t x, int16_t y, uint16_t w, uint16_t h) {
    currentX = x; currentY = y; currentW = w; currentH = h;
}

void StickyEpaperDriver::pushColors(uint16_t* data, uint32_t len, bool swapBytes) {
    if (!drawing || !data || !currentW || !currentH) return;
    if (flushSrcStride % sizeof(uint16_t)) {
        LOGE("Sticky", "Invalid RGB565 source stride: %lu", static_cast<unsigned long>(flushSrcStride));
        return;
    }
    const size_t stride = flushSrcStride ? flushSrcStride / sizeof(uint16_t) : currentW;
    if (stride < currentW || len < uint32_t(currentW) * currentH) {
        LOGE("Sticky", "Incomplete RGB565 flush");
        return;
    }
    xSemaphoreTake(mutex, portMAX_DELAY);
    bool changed = false;
    for (uint16_t row = 0; row < currentH; ++row) {
        for (uint16_t column = 0; column < currentW; ++column) {
            const int x = int(currentX) + column, y = int(currentY) + row;
            if (x < 0 || y < 0 || x >= width() || y >= height()) continue;
            uint16_t nativeX = 0, nativeY = 0;
            presented_frame_native_coords(800, 480, rotation, x, y, &nativeX, &nativeY);
            uint16_t color = data[size_t(row) * stride + column];
            if (swapBytes) color = uint16_t((color << 8) | (color >> 8));
            const uint32_t red = ((color >> 11) & 31) * 255 / 31;
            const uint32_t green = ((color >> 5) & 63) * 255 / 63;
            const uint32_t blue = (color & 31) * 255 / 31;
            const uint8_t luminance = (red * 77 + green * 150 + blue * 29) >> 8;
            const uint8_t gray = grayscale ? uint8_t(((uint16_t(luminance) * 3 + 127) / 255) * 5) :
                monochrome_is_white(luminance, x, y, bwConversion, bwThreshold) ? 1 : 0;
            const size_t index = size_t(nativeY) * (grayscale ? 400 : 100) + nativeX / (grayscale ? 2 : 8);
            const uint8_t before = drawing[index];
            presented_frame_write_gray(drawing, grayscale ? 400 : 100, nativeX, nativeY, grayscale ? 4 : 1, gray);
            changed |= before != drawing[index];
        }
    }
    if (changed || !baselineValid) {
        if (!pending) firstPending = millis();
        pending = true;
        if (changed) lastChange = millis();
    }
    xSemaphoreGive(mutex);
}

void StickyEpaperDriver::present() {
    if (!drawing) return;
    for (;;) {
        if (ota_activity_is_active()) return;
        xSemaphoreTake(mutex, portMAX_DELAY);
        if (!pending && !forceFull) { xSemaphoreGive(mutex); return; }
        const EpaperRefreshSettings settings = config_manager_get_epaper_refresh_settings();
        const uint32_t now = millis();
        const uint32_t refreshWait = baselineValid && now - lastRefresh < settings.epaper_min_refresh_interval_ms
            ? settings.epaper_min_refresh_interval_ms - (now - lastRefresh) : 0;
        const uint32_t quietWait = !forceFull && now - lastChange < EPAPER_REFRESH_SETTLE_MS
            ? EPAPER_REFRESH_SETTLE_MS - (now - lastChange) : 0;
        const uint32_t deadlineWait = now - firstPending < 500 ? 500 - (now - firstPending) : 0;
        const uint32_t settleWait = quietWait < deadlineWait ? quietWait : deadlineWait;
        const uint32_t wait = refreshWait > settleWait ? refreshWait : settleWait;
        if (wait) {
            xSemaphoreGive(mutex);
            const TickType_t ticks = pdMS_TO_TICKS(wait < 20 ? wait : 20);
            vTaskDelay(ticks ? ticks : 1);
            continue;
        }
        if (!forceFull && baselineValid && memcmp(drawing, presented, frameBytes) == 0) {
            pending = false;
            xSemaphoreGive(mutex);
            return;
        }
        const bool full = grayscale || forceFull || !baselineValid ||
            (settings.epaper_full_refresh_threshold && partialsSinceFull >= settings.epaper_full_refresh_threshold);
        memcpy(sending, drawing, frameBytes);
        // Snapshot readers must not report a frame while glass may be changing.
        baselineValid = false;
        pending = forceFull = false;
        xSemaphoreGive(mutex);
        const auto result = panel.refresh(sending, presented, grayscale, full);
        xSemaphoreTake(mutex, portMAX_DELAY);
        if (result == sticky_epaper::Result::Success) {
            memcpy(presented, sending, frameBytes);
            baselineValid = true;
            lastRefresh = millis();
            partialsSinceFull = full ? 0 : partialsSinceFull < UINT16_MAX ? partialsSinceFull + 1 : UINT16_MAX;
            available.store(true);
        } else {
            pending = true;
            forceFull = true;
            if (result == sticky_epaper::Result::Error) {
                LOGE("Sticky", "Presentation failed; next request will retry a full refresh");
            }
        }
        xSemaphoreGive(mutex);
        if (result != sticky_epaper::Result::Success) return;
    }
}

bool StickyEpaperDriver::requestFullRefresh() {
    if (!drawing) return false;
    xSemaphoreTake(mutex, portMAX_DELAY);
    forceFull = true;
    xSemaphoreGive(mutex);
    return true;
}

bool StickyEpaperDriver::getPresentedFrameInfo(PresentedFrameInfo& info) const {
    if (!mutex) return false;
    xSemaphoreTake(mutex, portMAX_DELAY);
    const bool valid = baselineValid;
    if (valid) {
        info.width = rotation & 1 ? 480 : 800;
        info.height = rotation & 1 ? 800 : 480;
        info.bitDepth = grayscale ? 4 : 1;
    }
    xSemaphoreGive(mutex);
    return valid;
}

bool StickyEpaperDriver::copyPresentedFrame(uint8_t* pixels, size_t length) {
    if (!mutex || !pixels || length < frameBytes) return false;
    xSemaphoreTake(mutex, portMAX_DELAY);
    if (!baselineValid) { xSemaphoreGive(mutex); return false; }
    const size_t rowBytes = presented_frame_row_bytes(width(), grayscale ? 4 : 1);
    memset(pixels, 0, frameBytes);
    for (int y = 0; y < height(); ++y) {
        for (int x = 0; x < width(); ++x) {
            uint16_t nativeX = 0, nativeY = 0;
            presented_frame_native_coords(800, 480, rotation, x, y, &nativeX, &nativeY);
            const uint8_t packed = presented[size_t(nativeY) * (grayscale ? 400 : 100) + nativeX / (grayscale ? 2 : 8)];
            const uint8_t gray = grayscale ? (nativeX & 1 ? packed & 15 : packed >> 4) :
                (packed & (0x80 >> (nativeX & 7)) ? 1 : 0);
            presented_frame_write_gray(pixels, rowBytes, x, y, grayscale ? 4 : 1, gray);
        }
        if ((y & 15) == 15) vTaskDelay(1);
    }
    xSemaphoreGive(mutex);
    return true;
}
