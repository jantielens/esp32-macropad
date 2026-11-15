#include "crowpanel42_epaper_driver.h"
#include "app/drivers/monochrome_render.h"
#include "app/presented_frame.h"
#include "app/config_manager.h"
#include "app/log_manager.h"
#include "app/ota_activity.h"
#include <esp_heap_caps.h>
#include <cstring>
#if HAS_DISPLAY_DIAGNOSTICS
#include <esp_timer.h>
#endif

namespace {
constexpr int crowpanelClock = EPAPER_SPI_SCK_PIN;
constexpr int crowpanelMosi = EPAPER_SPI_MOSI_PIN;
constexpr int crowpanelReset = EPAPER_RESET_PIN;
constexpr int crowpanelDc = EPAPER_DC_PIN;
constexpr int crowpanelCs = EPAPER_CS_PIN;
constexpr int crowpanelBusy = EPAPER_BUSY_PIN;
constexpr int crowpanelPower = EPAPER_POWER_PIN;
constexpr uint32_t crowpanelBusyTimeoutMs = 15000;
constexpr uint8_t crowpanelGc[5][7] = {
    {0x01, 0x14, 0x0a, 0x14, 0x00, 0x01, 0x01},
    {0x01, 0x54, 0x0a, 0x94, 0x00, 0x01, 0x01},
    {0x01, 0x54, 0x0a, 0x94, 0x00, 0x01, 0x01},
    {0x01, 0x94, 0x0a, 0x54, 0x00, 0x01, 0x01},
    {0x01, 0x94, 0x0a, 0x54, 0x00, 0x01, 0x01},
};
constexpr uint8_t crowpanelDu[5][7] = {
    {0x01, 0x14, 0x00, 0x00, 0x00, 0x01, 0x00},
    {0x01, 0x14, 0x00, 0x00, 0x00, 0x01, 0x00},
    {0x01, 0x94, 0x00, 0x00, 0x00, 0x01, 0x00},
    {0x01, 0x54, 0x00, 0x00, 0x00, 0x01, 0x00},
    {0x01, 0x14, 0x00, 0x00, 0x00, 0x01, 0x00},
};
}

CrowPanel42EpaperDriver::~CrowPanel42EpaperDriver() {
    releaseResources();
}

void CrowPanel42EpaperDriver::releaseResources() {
    available.store(false);
    if (spiStarted) spi.end();
    spiStarted = false;
    if (mutex) vSemaphoreDelete(mutex);
    if (drawing) heap_caps_free(drawing);
    if (snapshot) heap_caps_free(snapshot);
    mutex = nullptr;
    drawing = nullptr;
    snapshot = nullptr;
    wake = nullptr;
    schedule = {};
    pending = forceFull = hasPresented = false;
    lastChange = firstPendingChange = lastRefresh = 0;
    partialsSinceFull = 0;
}

void CrowPanel42EpaperDriver::send(uint8_t command, const uint8_t* data, size_t length) {
    spi.beginTransaction(SPISettings(10000000, MSBFIRST, SPI_MODE0));
    digitalWrite(crowpanelDc, LOW);
    digitalWrite(crowpanelCs, LOW);
    spi.transfer(command);
    digitalWrite(crowpanelCs, HIGH);
    digitalWrite(crowpanelDc, HIGH);
    if (length) {
        digitalWrite(crowpanelCs, LOW);
        spi.writeBytes(data, length);
        digitalWrite(crowpanelCs, HIGH);
    }
    spi.endTransaction();
}

void CrowPanel42EpaperDriver::fillRam(uint8_t command) {
    uint8_t white[256];
    memset(white, 0xff, sizeof(white));
    send(command);
    spi.beginTransaction(SPISettings(10000000, MSBFIRST, SPI_MODE0));
    digitalWrite(crowpanelCs, LOW);
    for (size_t remaining = framebufferBytes; remaining;) {
        const size_t length = remaining < sizeof(white) ? remaining : sizeof(white);
        spi.writeBytes(white, length);
        remaining -= length;
    }
    digitalWrite(crowpanelCs, HIGH);
    spi.endTransaction();
}

bool CrowPanel42EpaperDriver::cancelled() const {
    return stopping.load() || ota_activity_is_active();
}

bool CrowPanel42EpaperDriver::waitReady(const char* stage, bool activated) {
    const uint32_t started = millis();
    bool observedBusy = !activated;
    LOGD("CrowPanel", "%s wait: BUSY=%d (LOW=active)", stage, digitalRead(crowpanelBusy));
    for (;;) {
        if (!activated && cancelled()) return false;
        const bool busy = digitalRead(crowpanelBusy) == LOW;
        observedBusy |= busy;
        if (!busy && observedBusy) break;
        if (millis() - started >= crowpanelBusyTimeoutMs) {
            available.store(false);
            LOGE("CrowPanel", "V1.2A BUSY timeout at %s: busy=%d reset=%d power=%d; reboot to reinitialize display",
                  stage, digitalRead(crowpanelBusy), digitalRead(crowpanelReset), digitalRead(crowpanelPower));
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(1) ? pdMS_TO_TICKS(1) : 1);
    }
    LOGD("CrowPanel", "%s ready after %lu ms: BUSY=%d", stage,
         static_cast<unsigned long>(millis() - started), digitalRead(crowpanelBusy));
    return true;
}

bool CrowPanel42EpaperDriver::initializePanel(uint32_t resetSettleMs) {
    digitalWrite(crowpanelReset, HIGH);
    delay(10);
    digitalWrite(crowpanelReset, LOW);
    delay(100);
    digitalWrite(crowpanelReset, HIGH);
    delay(100);
    if (resetSettleMs) delay(resetSettleMs);
        LOGI("CrowPanel", "Hardware reset: busy=%d reset=%d power=%d",
            digitalRead(crowpanelBusy), digitalRead(crowpanelReset), digitalRead(crowpanelPower));
    const uint8_t panel[] = {0x3f, 0x4d};
    const uint8_t power[] = {0x03, 0x10, 0x3f, 0x3f, 0x03};
    const uint8_t booster[] = {0x96, 0x96, 0x29};
    const uint8_t resolution[] = {0x01, 0x90, 0x01, 0x2c};
    send(0x00, panel, sizeof(panel));
    send(0x01, power, sizeof(power));
    send(0x06, booster, sizeof(booster));
    const uint8_t clock = 0x09, vcom = 0x05, border = 0x97, timing = 0x22, saving = 0x88;
    send(0x30, &clock, 1);
    send(0x61, resolution, sizeof(resolution));
    send(0x82, &vcom, 1);
    send(0x50, &border, 1);
    send(0x60, &timing, 1);
    send(0xe3, &saving, 1);
    return true;
}

void CrowPanel42EpaperDriver::init() {
    releaseResources();
    stopping.store(false);
#if HAS_DISPLAY_DIAGNOSTICS
    stats = {};
    hardwareSample = {};
    hardwareCompletedUs = 0;
#endif
    const EpaperRefreshSettings settings = config_manager_get_epaper_refresh_settings();
    bwConversion = settings.epaper_bw_conversion;
    bwThreshold = settings.epaper_bw_threshold;
    drawing = static_cast<uint8_t*>(heap_caps_malloc(framebufferBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    snapshot = static_cast<uint8_t*>(heap_caps_malloc(framebufferBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    mutex = xSemaphoreCreateMutex();
    wake = xSemaphoreCreateBinaryStatic(&wakeStorage);
    if (!drawing || !snapshot || !mutex || !wake) {
        LOGE("CrowPanel", "Unable to allocate display framebuffers or mutex");
        releaseResources();
        return;
    }
    memset(drawing, 0xff, framebufferBytes);
    memset(snapshot, 0xff, framebufferBytes);
    pinMode(crowpanelPower, OUTPUT);
    digitalWrite(crowpanelPower, HIGH);
    pinMode(crowpanelCs, OUTPUT);
    digitalWrite(crowpanelCs, HIGH);
    pinMode(crowpanelDc, OUTPUT);
    pinMode(crowpanelReset, OUTPUT);
    pinMode(crowpanelBusy, INPUT);
    spi.begin(crowpanelClock, -1, crowpanelMosi, crowpanelCs);
    spiStarted = true;
    available.store(initializePanel());
    LOGI("CrowPanel", "V1.2A panel initialized: available=%u", unsigned(isAvailable()));
}

void CrowPanel42EpaperDriver::setAddrWindow(int16_t x, int16_t y, uint16_t w, uint16_t h) {
    currentX = x;
    currentY = y;
    currentW = w;
    currentH = h;
}

void CrowPanel42EpaperDriver::pushColors(uint16_t* data, uint32_t, bool) {
    if (!isAvailable() || !data) return;
#if HAS_DISPLAY_DIAGNOSTICS
    const uint64_t lockStart = esp_timer_get_time();
#endif
    xSemaphoreTake(mutex, portMAX_DELAY);
#if HAS_DISPLAY_DIAGNOSTICS
    const uint64_t conversionStart = esp_timer_get_time();
    stats.framebuffer_wait_us += conversionStart - lockStart;
    ++stats.flush_calls;
    stats.flush_pixels += uint32_t(currentW) * currentH;
#endif
    const uint32_t stride = flushSrcStride ? flushSrcStride / sizeof(uint16_t) : currentW;
    bool changed = false;
    for (uint16_t row = 0; row < currentH; ++row) {
        for (uint16_t column = 0; column < currentW; ++column) {
            int nativeX = currentX + column;
            int nativeY = currentY + row;
            if (nativeX < 0 || nativeY < 0 || nativeX >= width() || nativeY >= height()) continue;
            const int logicalX = nativeX;
            const int logicalY = nativeY;
            if (rotation == 1) { nativeX = 399 - logicalY; nativeY = logicalX; }
            else if (rotation == 2) { nativeX = 399 - logicalX; nativeY = 299 - logicalY; }
            else if (rotation == 3) { nativeX = logicalY; nativeY = 299 - logicalX; }
            const uint16_t color = data[row * stride + column];
            const uint32_t red = ((color >> 11) & 31) * 255 / 31;
            const uint32_t green = ((color >> 5) & 63) * 255 / 63;
            const uint32_t blue = (color & 31) * 255 / 31;
            const uint32_t luminance = (red * 77 + green * 150 + blue * 29) >> 8;
            const bool white = monochrome_is_white(luminance, logicalX, logicalY, bwConversion, bwThreshold);
            const size_t index = size_t(nativeY) * 50 + size_t(nativeX) / 8;
            const uint8_t mask = uint8_t(0x80 >> (nativeX & 7));
            const uint8_t value = white ? drawing[index] | mask : drawing[index] & ~mask;
            changed |= drawing[index] != value;
            drawing[index] = value;
        }
    }
    if (changed || !hasPresented) {
        if (!pending) firstPendingChange = millis();
        pending = true;
        if (changed) lastChange = millis();
    }
    schedule.changed |= changed;
#if HAS_DISPLAY_DIAGNOSTICS
    stats.conversion_us += esp_timer_get_time() - conversionStart;
#endif
    xSemaphoreGive(mutex);
}

void CrowPanel42EpaperDriver::beginFrame() {
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

void CrowPanel42EpaperDriver::commitFrame(const DisplayFrame& frame) {
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
    if (flushes) LOGD("CrowPanel", "frame=%lu convert=%llu fb_wait=%llu pixels=%llu flushes=%llu",
        static_cast<unsigned long>(frame.sequence), static_cast<unsigned long long>(conversion),
        static_cast<unsigned long long>(wait), static_cast<unsigned long long>(pixels),
        static_cast<unsigned long long>(flushes));
#endif
}

void CrowPanel42EpaperDriver::stopPresentation() {
    stopping.store(true);
    if (wake) xSemaphoreGive(wake);
}

bool CrowPanel42EpaperDriver::update(uint8_t mode) {
    if (!waitReady("before activation")) return false;
#if HAS_DISPLAY_DIAGNOSTICS
    const uint64_t preparationStart = esp_timer_get_time();
#endif
    const auto& waveform = mode == 0xf7 ? crowpanelGc : crowpanelDu;
    for (size_t index = 0; index < 5; ++index) {
        if (cancelled()) return false;
        uint8_t table[42] = {};
        memcpy(table, waveform[index], 7);
        send(uint8_t(0x20 + index), table, sizeof(table));
    }
    if (cancelled()) return false;
    const uint8_t activate = 0xa5;
#if HAS_DISPLAY_DIAGNOSTICS
    hardwareSample.controller_us += esp_timer_get_time() - preparationStart;
    hardwareSample.trigger_us = esp_timer_get_time();
#endif
    send(0x17, &activate, 1);
    // Activation owns the waveform: stop/OTA must not interrupt its BUSY completion.
    const bool success = waitReady(mode == 0xf7 ? "full refresh" : "partial refresh", true);
#if HAS_DISPLAY_DIAGNOSTICS
    hardwareSample.waveform_done_us = esp_timer_get_time();
#endif
    return success;
}

bool CrowPanel42EpaperDriver::refresh(bool full) {
    if (!waitReady("before presentation")) return false;
    if (full) {
#if HAS_DISPLAY_DIAGNOSTICS
        const uint64_t preparationStart = esp_timer_get_time();
#endif
        if (!initializePanel()) return false;
        if (cancelled()) return false;
        fillRam(0x10);
        if (cancelled()) return false;
        fillRam(0x13);
#if HAS_DISPLAY_DIAGNOSTICS
        hardwareSample.controller_us += esp_timer_get_time() - preparationStart;
#endif
        if (!update(0xf7)) return false;
        delay(500);
        if (cancelled()) return false;
#if HAS_DISPLAY_DIAGNOSTICS
        const uint64_t resetStart = esp_timer_get_time();
#endif
        if (!initializePanel(100)) return false;
        delay(300);
#if HAS_DISPLAY_DIAGNOSTICS
        hardwareSample.controller_us += esp_timer_get_time() - resetStart;
#endif
    }
    if (cancelled()) return false;
    const uint8_t border = 0x97;
    send(0x50, &border, 1);
#if HAS_DISPLAY_DIAGNOSTICS
    const uint64_t uploadStart = esp_timer_get_time();
#endif
    send(0x13, snapshot, framebufferBytes);
#if HAS_DISPLAY_DIAGNOSTICS
    hardwareSample.upload_us += esp_timer_get_time() - uploadStart;
    hardwareSample.uploaded_bytes += framebufferBytes;
#endif
    const bool success = update(full ? 0xf7 : 0xff);
    if (full && success) delay(500);
    return success && !cancelled();
}

void CrowPanel42EpaperDriver::present() {
    if (!isAvailable()) return;
    for (;;) {
        if (cancelled()) return;
        xSemaphoreTake(mutex, portMAX_DELAY);
        if (schedule.rendering) { xSemaphoreGive(mutex); return; }
        if (!pending && !forceFull) { xSemaphoreGive(mutex); return; }
        const EpaperRefreshSettings settings = config_manager_get_epaper_refresh_settings();
        const uint32_t now = millis();
        const uint32_t wait = schedule.wait(now, hasPresented, forceFull, lastRefresh,
            settings.epaper_min_refresh_interval_ms, lastChange, firstPendingChange, EPAPER_REFRESH_SETTLE_MS);
        if (wait) {
            xSemaphoreGive(mutex);
            const TickType_t ticks = pdMS_TO_TICKS(wait < 20 ? wait : 20);
            // OTA does not signal this semaphore; bound its observation delay.
            xSemaphoreTake(wake, ticks ? ticks : 1);
            continue;
        }
        if (!forceFull && hasPresented && memcmp(drawing, snapshot, framebufferBytes) == 0) {
            pending = false;
            schedule.consume();
#if HAS_DISPLAY_DIAGNOSTICS
            ++stats.unchanged_skips;
#endif
            xSemaphoreGive(mutex);
            return;
        }
        const bool full = forceFull || !hasPresented ||
            (settings.epaper_full_refresh_threshold && partialsSinceFull >= settings.epaper_full_refresh_threshold);
#if HAS_DISPLAY_DIAGNOSTICS
        DisplayPresentationSample sample;
        sample.frame_sequence = schedule.frame.sequence;
        sample.request_sequence = schedule.frame.request_sequence;
        sample.requested_us = schedule.frame.requested_us;
        sample.ready_us = schedule.frame.ready_us;
        sample.snapshot_us = esp_timer_get_time();
        sample.eligible_us = sample.ready_us > hardwareCompletedUs ? sample.ready_us : hardwareCompletedUs;
        sample.coalesce_us = sample.eligible_us ? sample.snapshot_us - sample.eligible_us : 0;
        sample.full = full;
        sample.width = 400;
        sample.height = 300;
        const uint64_t copyStart = esp_timer_get_time();
        hardwareSample = {};
#endif
        memcpy(snapshot, drawing, framebufferBytes);
#if HAS_DISPLAY_DIAGNOSTICS
        sample.copy_us = esp_timer_get_time() - copyStart;
#endif
        hasPresented = false;
        pending = false;
        forceFull = false;
        schedule.consume();
        xSemaphoreGive(mutex);
          const uint32_t refreshStarted = millis();
          LOGD("CrowPanel", "Presenting %s frame", full ? "full" : "partial");
        const bool success = refresh(full);
#if HAS_DISPLAY_DIAGNOSTICS
        hardwareCompletedUs = esp_timer_get_time();
#endif
          LOGD("CrowPanel", "Presentation %s after %lu ms", success ? "completed" : "failed",
                 static_cast<unsigned long>(millis() - refreshStarted));
        xSemaphoreTake(mutex, portMAX_DELAY);
        hasPresented = success;
        if (success) {
            lastRefresh = millis();
            partialsSinceFull = full ? 0 : partialsSinceFull < UINT16_MAX ? partialsSinceFull + 1 : UINT16_MAX;
        } else {
            pending = forceFull = true;
            LOGW("CrowPanel", "Presentation incomplete; full recovery required");
        }
#if HAS_DISPLAY_DIAGNOSTICS
        sample.success = success;
        sample.completed_ms = millis();
        sample.published_us = success ? esp_timer_get_time() : 0;
        sample.trigger_us = hardwareSample.trigger_us;
        sample.waveform_done_us = hardwareSample.waveform_done_us;
        sample.controller_us = hardwareSample.controller_us;
        sample.upload_us = hardwareSample.upload_us;
        sample.uploaded_bytes = hardwareSample.uploaded_bytes;
        sample.total_us = esp_timer_get_time() - sample.snapshot_us;
        stats.add(sample);
#endif
        xSemaphoreGive(mutex);
#if HAS_DISPLAY_DIAGNOSTICS
        LOGD("CrowPanel", "frame=%lu request_seq=%lu request=%llu ready=%llu eligible=%llu snapshot=%llu",
            static_cast<unsigned long>(sample.frame_sequence), static_cast<unsigned long>(sample.request_sequence),
            static_cast<unsigned long long>(sample.requested_us), static_cast<unsigned long long>(sample.ready_us),
            static_cast<unsigned long long>(sample.eligible_us), static_cast<unsigned long long>(sample.snapshot_us));
        LOGD("CrowPanel", "frame=%lu prep=%lu upload=%lu bytes=%lu trigger=%llu busy_done=%llu published=%llu ok=%d",
            static_cast<unsigned long>(sample.frame_sequence), static_cast<unsigned long>(sample.controller_us),
            static_cast<unsigned long>(sample.upload_us), static_cast<unsigned long>(sample.uploaded_bytes),
            static_cast<unsigned long long>(sample.trigger_us), static_cast<unsigned long long>(sample.waveform_done_us),
            static_cast<unsigned long long>(sample.published_us), sample.success);
#endif
        if (!success) return;
    }
}

#if HAS_DISPLAY_DIAGNOSTICS
bool CrowPanel42EpaperDriver::getPresentationStats(DisplayPresentationStats* out) {
    if (!mutex || !out) return false;
    xSemaphoreTake(mutex, portMAX_DELAY);
    *out = stats;
    xSemaphoreGive(mutex);
    return true;
}
#endif

bool CrowPanel42EpaperDriver::getPresentedFrameInfo(PresentedFrameInfo& info) const {
    if (!isAvailable() || !mutex) return false;
    xSemaphoreTake(mutex, portMAX_DELAY);
    const bool available = hasPresented;
    if (available) {
        info.width = rotation & 1 ? 300 : 400;
        info.height = rotation & 1 ? 400 : 300;
        info.bitDepth = 1;
    }
    xSemaphoreGive(mutex);
    return available;
}

bool CrowPanel42EpaperDriver::copyPresentedFrame(uint8_t* pixels, size_t length) {
    PresentedFrameInfo info;
    if (!getPresentedFrameInfo(info) || !pixels ||
            length < presented_frame_bytes(info.width, info.height, info.bitDepth)) return false;
    xSemaphoreTake(mutex, portMAX_DELAY);
    if (!hasPresented) { xSemaphoreGive(mutex); return false; }
    const size_t rowBytes = presented_frame_row_bytes(info.width, info.bitDepth);
    memset(pixels, 0, rowBytes * info.height);
    for (uint16_t row = 0; row < info.height; ++row) {
        for (uint16_t column = 0; column < info.width; ++column) {
            uint16_t nativeX = 0, nativeY = 0;
            if (!presented_frame_native_coords(400, 300, rotation, column, row, &nativeX, &nativeY)) {
                xSemaphoreGive(mutex);
                return false;
            }
            const size_t sourceIndex = size_t(nativeY) * 50 + nativeX / 8;
            const bool white = snapshot[sourceIndex] & (0x80 >> (nativeX & 7));
            presented_frame_write_gray(pixels, rowBytes, column, row, info.bitDepth, white ? 1 : 0);
        }
    }
    xSemaphoreGive(mutex);
    return true;
}

bool CrowPanel42EpaperDriver::requestFullRefresh() {
    if (!isAvailable() || stopping.load()) return false;
    xSemaphoreTake(mutex, portMAX_DELAY);
    forceFull = true;
    xSemaphoreGive(mutex);
    xSemaphoreGive(wake);
    return true;
}