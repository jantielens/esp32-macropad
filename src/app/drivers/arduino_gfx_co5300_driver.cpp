#include "arduino_gfx_co5300_driver.h"
#include "../m5stack_stopwatch.h"
#include "../log_manager.h"
#include <esp_heap_caps.h>
#include <new>
#include <cstring>

#if !HAS_M5STACK_STOPWATCH
#error "StopWatch CO5300 requires HAS_M5STACK_STOPWATCH"
#endif

namespace {
constexpr uint16_t panelWidth = DISPLAY_WIDTH + 2;
constexpr uint32_t scratchPixels = LVGL_BUFFER_SIZE + 2 * DISPLAY_HEIGHT + 2 * panelWidth + 4;
// The factory's 468x466 viewport at (6,0) is cropped by one column each
// side to expose the documented 466-square screen. CO5300 has no 90° MADCTL.
class StopWatchPanel final : public Arduino_CO5300 {
public:
    explicit StopWatchPanel(Arduino_DataBus* bus)
        : Arduino_CO5300(bus, GFX_NOT_DEFINED, 0, panelWidth, DISPLAY_HEIGHT,
                6, 0, 6, 0) {}
protected:
    void tftInit() override {
        _bus->sendCommand(0x11);
        delay(150);
        _bus->beginWrite();
        _bus->writeC8D8(0xc4, 0x80);
        _bus->writeC8D8(0x35, 0x80);
        _bus->writeC8D16(0x44, 0x01d2);
        _bus->writeC8D8(0x3a, 0x55);
        _bus->writeC8D8(0x53, 0x20);
        _bus->writeCommand(0x20);
        _bus->writeC8D8(0x36, 0);
        _bus->writeC8D8(0x51, 0);
        _bus->writeCommand(0x29);
        _bus->endWrite();
    }
};
}

Arduino_GFX_CO5300_Driver::~Arduino_GFX_CO5300_Driver() {
    delete gfx;
    delete bus;
    if (scratch) heap_caps_free(scratch);
    if (framebuffer) heap_caps_free(framebuffer);
}

void Arduino_GFX_CO5300_Driver::init() {
    if (available) return;
    if (!m5stack_stopwatch_ready()) return;
    scratch = static_cast<uint16_t*>(heap_caps_malloc(
        scratchPixels * sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    framebuffer = static_cast<uint16_t*>(heap_caps_malloc(
        uint32_t(panelWidth) * DISPLAY_HEIGHT * sizeof(uint16_t),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    bus = new (std::nothrow) Arduino_ESP32QSPI(
        LCD_QSPI_CS, LCD_QSPI_PCLK, LCD_QSPI_D0, LCD_QSPI_D1, LCD_QSPI_D2, LCD_QSPI_D3);
    if (bus) gfx = new (std::nothrow) StopWatchPanel(bus);
    if (!scratch || !framebuffer || !gfx || !gfx->begin(TFT_SPI_FREQ_HZ)) {
        LOGE("CO5300", "Display initialization failed");
        delete gfx; gfx = nullptr;
        delete bus; bus = nullptr;
        if (scratch) heap_caps_free(scratch);
        scratch = nullptr;
        if (framebuffer) heap_caps_free(framebuffer);
        framebuffer = nullptr;
        return;
    }
    memset(framebuffer, 0, uint32_t(panelWidth) * DISPLAY_HEIGHT * sizeof(uint16_t));
    pinMode(38, INPUT_PULLUP);
    available = true;
    gfx->fillScreen(RGB565_BLACK);
    setBacklightBrightness(brightness);
}

void Arduino_GFX_CO5300_Driver::setBacklight(bool on) {
    backlightOn = on;
    if (available) gfx->setBrightness(on ? uint16_t(brightness) * 255 / 100 : 0);
}

void Arduino_GFX_CO5300_Driver::setBacklightBrightness(uint8_t value) {
    brightness = value > 100 ? 100 : value;
    setBacklight(backlightOn);
}

void Arduino_GFX_CO5300_Driver::setAddrWindow(int16_t px, int16_t py, uint16_t pw, uint16_t ph) {
    x = px; y = py; w = pw; h = ph;
}

void Arduino_GFX_CO5300_Driver::pushColors(uint16_t* data, uint32_t len, bool swap_bytes) {
    (void)swap_bytes; // Arduino_GFX bitmap API consumes native RGB565 words.
    if (!available || !data || !w || !h || x < 0 || y < 0 ||
        x + w > width() || y + h > height() || len < uint32_t(w) * h ||
        uint32_t(w) * h > LVGL_BUFFER_SIZE) return;
    const uint32_t stride = flushSrcStride ? flushSrcStride / sizeof(uint16_t) : w;
    if (stride < w) return;
    int16_t px = x, py = y;
    const uint16_t pw = (rotation & 1) ? h : w;
    const uint16_t ph = (rotation & 1) ? w : h;
    if (rotation == 1) { px = DISPLAY_WIDTH - y - h; py = x; }
    if (rotation == 2) { px = DISPLAY_WIDTH - x - w; py = DISPLAY_HEIGHT - y - h; }
    if (rotation == 3) { px = y; py = DISPLAY_HEIGHT - x - w; }
    for (uint16_t sy = 0; sy < h; ++sy) {
        for (uint16_t sx = 0; sx < w; ++sx) {
            uint16_t dx = sx, dy = sy;
            if (rotation == 1) { dx = h - 1 - sy; dy = sx; }
            if (rotation == 2) { dx = w - 1 - sx; dy = h - 1 - sy; }
            if (rotation == 3) { dx = sy; dy = w - 1 - sx; }
            framebuffer[uint32_t(py + dy) * panelWidth + px + dx + 1] =
                data[uint32_t(sy) * stride + sx];
        }
    }
    const uint16_t left = (px + 1) & ~1;
    const uint16_t right = (px + pw) | 1;
    const uint16_t alignedWidth = right - left + 1;
    const uint16_t top = py & ~1;
    const uint16_t bottom = (py + ph - 1) | 1;
    const uint16_t alignedHeight = bottom - top + 1;
    for (uint16_t row = 0; row < alignedHeight; ++row) {
        memcpy(scratch + uint32_t(row) * alignedWidth,
               framebuffer + uint32_t(top + row) * panelWidth + left,
               alignedWidth * sizeof(uint16_t));
    }
    gfx->draw16bitRGBBitmap(left, top, scratch, alignedWidth, alignedHeight);
}

void Arduino_GFX_CO5300_Driver::displaySleep() {
    if (!available) return;
    bus->sendCommand(0x28);
    bus->sendCommand(0x10);
}

void Arduino_GFX_CO5300_Driver::displayWakeSleepOut() {
    if (available) bus->sendCommand(0x11);
}

void Arduino_GFX_CO5300_Driver::displayWakeDisplayOn() {
    if (!available) return;
    bus->sendCommand(0x29);
    setBacklight(backlightOn);
}

void Arduino_GFX_CO5300_Driver::displayWake() {
    displayWakeSleepOut();
    delay(120);
    displayWakeDisplayOn();
}
