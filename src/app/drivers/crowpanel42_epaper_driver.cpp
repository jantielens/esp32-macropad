#include "crowpanel42_epaper_driver.h"
#include "monochrome_render.h"
#include "../config_manager.h"
#include "../log_manager.h"
#include "../ota_activity.h"
#include <esp_heap_caps.h>
#include <cstring>

namespace {
constexpr int crowpanelClock = EPAPER_SPI_SCK_PIN;
constexpr int crowpanelMosi = EPAPER_SPI_MOSI_PIN;
constexpr int crowpanelReset = EPAPER_RESET_PIN;
constexpr int crowpanelDc = EPAPER_DC_PIN;
constexpr int crowpanelCs = EPAPER_CS_PIN;
constexpr int crowpanelBusy = EPAPER_BUSY_PIN;
constexpr int crowpanelPower = EPAPER_POWER_PIN;
constexpr uint32_t crowpanelBusyTimeoutMs = 15000;
constexpr uint32_t crowpanelMaxSettleMs = 500;
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
    if (mutex) vSemaphoreDelete(mutex);
    if (drawing) heap_caps_free(drawing);
    if (snapshot) heap_caps_free(snapshot);
    mutex = nullptr;
    drawing = nullptr;
    snapshot = nullptr;
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

bool CrowPanel42EpaperDriver::waitReady(const char* stage) {
    const uint32_t started = millis();
    while (digitalRead(crowpanelBusy) != LOW) {
        if (millis() - started >= crowpanelBusyTimeoutMs) {
            available.store(false);
            LOGE("CrowPanel", "V1.2A BUSY timeout at %s: busy=%d reset=%d power=%d; reboot to reinitialize display",
                  stage, digitalRead(crowpanelBusy), digitalRead(crowpanelReset), digitalRead(crowpanelPower));
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(1) ? pdMS_TO_TICKS(1) : 1);
    }
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
    drawing = static_cast<uint8_t*>(heap_caps_malloc(framebufferBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    snapshot = static_cast<uint8_t*>(heap_caps_malloc(framebufferBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    mutex = xSemaphoreCreateMutex();
    if (!drawing || !snapshot || !mutex) {
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
    xSemaphoreTake(mutex, portMAX_DELAY);
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
            const bool white = monochrome_is_white(luminance);
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
    xSemaphoreGive(mutex);
}

bool CrowPanel42EpaperDriver::update(uint8_t mode) {
    const auto& waveform = mode == 0xf7 ? crowpanelGc : crowpanelDu;
    for (size_t index = 0; index < 5; ++index) {
        uint8_t table[42] = {};
        memcpy(table, waveform[index], 7);
        send(uint8_t(0x20 + index), table, sizeof(table));
    }
    const uint8_t activate = 0xa5;
    send(0x17, &activate, 1);
    return waitReady(mode == 0xf7 ? "full refresh" : "partial refresh");
}

bool CrowPanel42EpaperDriver::refresh(bool full) {
    if (full) {
        if (!initializePanel()) return false;
        fillRam(0x10);
        fillRam(0x13);
        if (!update(0xf7)) return false;
        delay(500);
        if (!initializePanel(100)) return false;
        delay(300);
    }
    const uint8_t border = 0x97;
    send(0x50, &border, 1);
    send(0x13, snapshot, framebufferBytes);
    const bool success = update(full ? 0xf7 : 0xff);
    if (full && success) delay(500);
    return success;
}

void CrowPanel42EpaperDriver::present() {
    if (!isAvailable()) return;
    for (;;) {
        if (ota_activity_is_active()) return;
        xSemaphoreTake(mutex, portMAX_DELAY);
        if (!pending && !forceFull) { xSemaphoreGive(mutex); return; }
        const EpaperRefreshSettings settings = config_manager_get_epaper_refresh_settings();
        const uint32_t now = millis();
        const uint32_t refreshWait = hasPresented && now - lastRefresh < settings.epaper_min_refresh_interval_ms
            ? settings.epaper_min_refresh_interval_ms - (now - lastRefresh) : 0;
        const uint32_t quietWait = !forceFull && now - lastChange < EPAPER_REFRESH_SETTLE_MS
            ? EPAPER_REFRESH_SETTLE_MS - (now - lastChange) : 0;
        const uint32_t deadlineWait = now - firstPendingChange < crowpanelMaxSettleMs
            ? crowpanelMaxSettleMs - (now - firstPendingChange) : 0;
        const uint32_t settleWait = quietWait < deadlineWait ? quietWait : deadlineWait;
        const uint32_t wait = refreshWait > settleWait ? refreshWait : settleWait;
        if (wait) {
            xSemaphoreGive(mutex);
            vTaskDelay(pdMS_TO_TICKS(wait) ? pdMS_TO_TICKS(wait) : 1);
            continue;
        }
        if (!forceFull && hasPresented && memcmp(drawing, snapshot, framebufferBytes) == 0) {
            pending = false;
            xSemaphoreGive(mutex);
            return;
        }
        const bool full = forceFull || !hasPresented ||
            (settings.epaper_full_refresh_threshold && partialsSinceFull >= settings.epaper_full_refresh_threshold);
        memcpy(snapshot, drawing, framebufferBytes);
        pending = false;
        forceFull = false;
        xSemaphoreGive(mutex);
          const uint32_t refreshStarted = millis();
          LOGD("CrowPanel", "Presenting %s frame", full ? "full" : "partial");
        const bool success = refresh(full);
          LOGD("CrowPanel", "Presentation %s after %lu ms", success ? "completed" : "failed",
                 static_cast<unsigned long>(millis() - refreshStarted));
        xSemaphoreTake(mutex, portMAX_DELAY);
        hasPresented = success;
        lastRefresh = millis();
        partialsSinceFull = full ? 0 : partialsSinceFull < UINT16_MAX ? partialsSinceFull + 1 : UINT16_MAX;
        xSemaphoreGive(mutex);
        if (!success) return;
    }
}

bool CrowPanel42EpaperDriver::requestFullRefresh() {
    if (!isAvailable()) return false;
    xSemaphoreTake(mutex, portMAX_DELAY);
    forceFull = true;
    xSemaphoreGive(mutex);
    return true;
}