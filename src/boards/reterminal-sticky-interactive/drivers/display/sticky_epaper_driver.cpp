#include "sticky_epaper_driver.h"
#include "app/drivers/monochrome_render.h"
#include "app/presented_frame.h"
#include "app/config_manager.h"
#include "app/board_hardware.h"
#include "app/log_manager.h"
#include "app/ota_activity.h"
#include <esp_heap_caps.h>
#include <cstring>
#include <algorithm>
#if HAS_DISPLAY_DIAGNOSTICS
#include <esp_timer.h>
#endif

StickyEpaperDriver::~StickyEpaperDriver() { releaseResources(); }

void StickyEpaperDriver::releaseResources() {
    available.store(false);
    panel.end();
    if (mutex) vSemaphoreDelete(mutex);
    if (drawing) heap_caps_free(drawing);
    if (sending) heap_caps_free(sending);
    if (presented) heap_caps_free(presented);
    mutex = nullptr;
    wake = nullptr;
    schedule = {};
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
    wake = xSemaphoreCreateBinaryStatic(&wakeStorage);
    stopping.store(false);
    panel.setStopFlag(&stopping);
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
    const int left = std::max(0, int(currentX)), top = std::max(0, int(currentY));
    const int right = std::min(width(), int(currentX) + currentW);
    const int bottom = std::min(height(), int(currentY) + currentH);
    if (left >= right || top >= bottom) return;
    uint16_t x0, y0, x1, y1;
    presented_frame_native_coords(800, 480, rotation, left, top, &x0, &y0);
    presented_frame_native_coords(800, 480, rotation, right - 1, bottom - 1, &x1, &y1);
    const int nativeLeft = std::min(x0, x1), nativeRight = std::max(x0, x1) + 1;
    const int nativeTop = std::min(y0, y1), nativeBottom = std::max(y0, y1) + 1;
    const int pixelsPerByte = grayscale ? 2 : 8;
    const int rowBytes = grayscale ? 400 : 100;
    const int bits = grayscale ? 4 : 1;
    const int xStep = rotation == 0 ? 1 : rotation == 2 ? -1 : 0;
    const int yStep = rotation == 3 ? 1 : rotation == 1 ? -1 : 0;
    const ptrdiff_t sourceStep = xStep + yStep * ptrdiff_t(stride);
#if HAS_DISPLAY_DIAGNOSTICS
    const uint64_t waitStart = esp_timer_get_time();
#endif
    xSemaphoreTake(mutex, portMAX_DELAY);
#if HAS_DISPLAY_DIAGNOSTICS
    const uint64_t conversionStart = esp_timer_get_time();
    stats.framebuffer_wait_us += conversionStart - waitStart;
#endif
    bool changed = false;
    // Visit packed native bytes, not rotated pixels scattered through PSRAM.
    // Each destination byte is read/merged/written once, including clipped edges.
    for (int nativeY = nativeTop; nativeY < nativeBottom; ++nativeY) {
        int x = rotation == 0 ? nativeLeft : rotation == 1 ? nativeY :
            rotation == 2 ? 799 - nativeLeft : 479 - nativeY;
        int y = rotation == 0 ? nativeY : rotation == 1 ? 799 - nativeLeft :
            rotation == 2 ? 479 - nativeY : nativeLeft;
        ptrdiff_t source = ptrdiff_t(y - currentY) * stride + x - currentX;
        int nativeX = nativeLeft;
        while (nativeX < nativeRight) {
            const size_t index = size_t(nativeY) * rowBytes + nativeX / pixelsPerByte;
            const uint8_t before = drawing[index];
            uint8_t packed = before;
            const int end = std::min(nativeRight, (nativeX / pixelsPerByte + 1) * pixelsPerByte);
            for (; nativeX < end; ++nativeX) {
                uint16_t color = data[source];
                if (swapBytes) color = uint16_t((color << 8) | (color >> 8));
                const uint8_t luminance = monochrome_rgb565_luminance(color);
                const uint8_t gray = grayscale ? uint8_t(((uint16_t(luminance) * 3 + 127) / 255) * 5) :
                    monochrome_is_white(luminance, x, y, bwConversion, bwThreshold) ? 1 : 0;
                const int shift = (pixelsPerByte - 1 - nativeX % pixelsPerByte) * bits;
                const uint8_t mask = uint8_t((grayscale ? 15 : 1) << shift);
                packed = uint8_t((packed & ~mask) | (gray << shift));
                source += sourceStep;
                x += xStep;
                y += yStep;
            }
            if (before != packed) {
                drawing[index] = packed;
                changed = true;
            }
        }
    }
    if (changed || !baselineValid) {
        if (!pending) firstPending = millis();
        pending = true;
        if (changed) lastChange = millis();
    }
    schedule.changed |= changed;
#if HAS_DISPLAY_DIAGNOSTICS
    ++stats.flush_calls;
    stats.flush_pixels += uint32_t(right - left) * (bottom - top);
    stats.conversion_us += esp_timer_get_time() - conversionStart;
#endif
    xSemaphoreGive(mutex);
}

void StickyEpaperDriver::beginFrame() {
    if (!mutex) return;
    xSemaphoreTake(mutex, portMAX_DELAY);
    schedule.begin();
#if HAS_DISPLAY_DIAGNOSTICS
    frameConversionStart = stats.conversion_us;
    frameWaitStart = stats.framebuffer_wait_us;
    framePixelsStart = stats.flush_pixels;
    frameFlushesStart = stats.flush_calls;
#endif
    xSemaphoreGive(mutex);
}

void StickyEpaperDriver::commitFrame(const DisplayFrame& frame) {
    if (!mutex) return;
    xSemaphoreTake(mutex, portMAX_DELAY);
    schedule.commit(frame);
#if HAS_DISPLAY_DIAGNOSTICS
    const uint64_t conversion = stats.conversion_us - frameConversionStart;
    const uint64_t wait = stats.framebuffer_wait_us - frameWaitStart;
    const uint64_t pixels = stats.flush_pixels - framePixelsStart;
    const uint64_t flushes = stats.flush_calls - frameFlushesStart;
#endif
    xSemaphoreGive(mutex);
    xSemaphoreGive(wake);
#if HAS_DISPLAY_DIAGNOSTICS
    if (flushes) LOGD("Sticky", "frame=%lu convert=%llu fb_wait=%llu pixels=%llu flushes=%llu",
        static_cast<unsigned long>(frame.sequence), static_cast<unsigned long long>(conversion),
        static_cast<unsigned long long>(wait), static_cast<unsigned long long>(pixels),
        static_cast<unsigned long long>(flushes));
#endif
}

void StickyEpaperDriver::stopPresentation() {
    stopping.store(true);
    if (wake) xSemaphoreGive(wake);
}

void StickyEpaperDriver::present() {
    if (!drawing) return;
    for (;;) {
        if (ota_activity_is_active() || stopping.load()) return;
        xSemaphoreTake(mutex, portMAX_DELAY);
        if (schedule.rendering) { xSemaphoreGive(mutex); return; }
        if (!pending && !forceFull) { xSemaphoreGive(mutex); return; }
        const EpaperRefreshSettings settings = config_manager_get_epaper_refresh_settings();
        const uint32_t now = millis();
        const uint32_t wait = schedule.wait(now, baselineValid, forceFull, lastRefresh,
            settings.epaper_min_refresh_interval_ms, lastChange, firstPending, EPAPER_REFRESH_SETTLE_MS);
        if (wait) {
            xSemaphoreGive(mutex);
            const TickType_t ticks = pdMS_TO_TICKS(wait < 20 ? wait : 20);
            // Bounded timeout also observes OTA, which does not own our wake signal.
            xSemaphoreTake(wake, ticks ? ticks : 1);
            continue;
        }
#if HAS_DISPLAY_DIAGNOSTICS
        const uint64_t compareStart = esp_timer_get_time();
#endif
        if (!forceFull && baselineValid && memcmp(drawing, presented, frameBytes) == 0) {
            pending = false;
            schedule.consume();
#if HAS_DISPLAY_DIAGNOSTICS
            ++stats.unchanged_skips;
#endif
            xSemaphoreGive(mutex);
            return;
        }
        const bool full = grayscale || forceFull || !baselineValid ||
            (settings.epaper_full_refresh_threshold && partialsSinceFull >= settings.epaper_full_refresh_threshold);
#if HAS_DISPLAY_DIAGNOSTICS
        DisplayPresentationSample sample;
        sample.frame_sequence = schedule.frame.sequence;
        sample.request_sequence = schedule.frame.request_sequence;
        sample.requested_us = schedule.frame.requested_us;
        sample.ready_us = schedule.frame.ready_us;
        sample.snapshot_us = esp_timer_get_time();
        const uint64_t copyStart = sample.snapshot_us;
        sample.eligible_us = sample.ready_us > hardwareCompletedUs ? sample.ready_us : hardwareCompletedUs;
        sample.coalesce_us = sample.eligible_us ? sample.snapshot_us - sample.eligible_us : 0;
        sample.full = full;
        sample.compare_us = sample.snapshot_us - compareStart;
        sample.width = 800;
        sample.height = 480;
#endif
        memcpy(sending, drawing, frameBytes);
#if HAS_DISPLAY_DIAGNOSTICS
        sample.copy_us = esp_timer_get_time() - copyStart;
#endif
        // Snapshot readers must not report a frame while glass may be changing.
        baselineValid = false;
        pending = forceFull = false;
        schedule.consume();
        xSemaphoreGive(mutex);
        const auto result = panel.refresh(sending, presented, grayscale, full);
#if HAS_DISPLAY_DIAGNOSTICS
        hardwareCompletedUs = esp_timer_get_time();
#endif
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
#if HAS_DISPLAY_DIAGNOSTICS
        sample.success = result == sticky_epaper::Result::Success;
        sample.completed_ms = millis();
        sample.published_us = sample.success ? esp_timer_get_time() : 0;
        sample.trigger_us = panel.timing.trigger_us;
        sample.waveform_done_us = panel.timing.waveform_done_us;
        sample.controller_us = panel.timing.preparation_us;
        sample.upload_us = panel.timing.upload_us;
        sample.uploaded_bytes = panel.timing.uploaded_bytes;
        sample.total_us = esp_timer_get_time() - sample.snapshot_us;
        stats.add(sample);
#endif
        xSemaphoreGive(mutex);
#if HAS_DISPLAY_DIAGNOSTICS
        LOGD("Sticky", "frame=%lu request_seq=%lu request=%llu ready=%llu eligible=%llu snapshot=%llu",
            static_cast<unsigned long>(sample.frame_sequence),
            static_cast<unsigned long>(sample.request_sequence),
            static_cast<unsigned long long>(sample.requested_us), static_cast<unsigned long long>(sample.ready_us),
            static_cast<unsigned long long>(sample.eligible_us),
            static_cast<unsigned long long>(sample.snapshot_us));
        LOGD("Sticky", "frame=%lu prep=%lu upload=%lu trigger=%llu busy_done=%llu published=%llu ok=%d",
            static_cast<unsigned long>(sample.frame_sequence), static_cast<unsigned long>(sample.controller_us),
            static_cast<unsigned long>(sample.upload_us), static_cast<unsigned long long>(sample.trigger_us),
            static_cast<unsigned long long>(sample.waveform_done_us), static_cast<unsigned long long>(sample.published_us),
            sample.success);
        LOGD("Sticky", "frame=%lu pack=%lu spi=%lu yield=%lu bytes=%lu packed_bytes=%lu",
            static_cast<unsigned long>(sample.frame_sequence),
            static_cast<unsigned long>(panel.timing.pack_us), static_cast<unsigned long>(panel.timing.spi_us),
            static_cast<unsigned long>(panel.timing.yield_us), static_cast<unsigned long>(sample.uploaded_bytes),
            static_cast<unsigned long>(panel.timing.packed_bytes));
#endif
        if (result != sticky_epaper::Result::Success) return;
    }
}

#if HAS_DISPLAY_DIAGNOSTICS
bool StickyEpaperDriver::getPresentationStats(DisplayPresentationStats* out) {
    if (!mutex || !out) return false;
    xSemaphoreTake(mutex, portMAX_DELAY);
    *out = stats;
    xSemaphoreGive(mutex);
    return true;
}
#endif

bool StickyEpaperDriver::requestFullRefresh() {
    if (!drawing) return false;
    xSemaphoreTake(mutex, portMAX_DELAY);
    forceFull = true;
    xSemaphoreGive(mutex);
    xSemaphoreGive(wake);
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
