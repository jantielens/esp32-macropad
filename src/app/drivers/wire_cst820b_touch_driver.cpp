#include "wire_cst820b_touch_driver.h"
#include "../m5stack_stopwatch.h"
#include <atomic>

#if !HAS_M5STACK_STOPWATCH
#error "StopWatch CST820B requires HAS_M5STACK_STOPWATCH"
#endif

namespace {
std::atomic<bool> cst820b_irq_pending{false};
void IRAM_ATTR cst820b_interrupt() {
    cst820b_irq_pending.store(true, std::memory_order_relaxed);
}
}

Wire_CST820B_TouchDriver::~Wire_CST820B_TouchDriver() {
    if (available) detachInterrupt(digitalPinToInterrupt(13));
}

void Wire_CST820B_TouchDriver::init() {
    if (available) return;
    available = m5stack_stopwatch_ready();
    pinMode(13, INPUT_PULLUP);
    if (available) {
        cst820b_irq_pending.store(false, std::memory_order_relaxed);
        attachInterrupt(digitalPinToInterrupt(13), cst820b_interrupt, FALLING);
    }
    // No CST816S-only 0xFE write: CST820B uses 0xE5=3 for explicit sleep.
}

bool Wire_CST820B_TouchDriver::isTouched() {
    const TouchSample sample = readSample();
    return sample.status != TouchReadStatus::Error && sample.pressed;
}

bool Wire_CST820B_TouchDriver::getTouch(uint16_t* x, uint16_t* y, uint16_t* pressure) {
    if (pressure) *pressure = 0;
    if (!x || !y) return false;
    const TouchSample sample = readSample();
    if (sample.status == TouchReadStatus::Error || !sample.pressed) return false;
    *x = sample.horizontal; *y = sample.vertical;
    return true;
}

TouchSample Wire_CST820B_TouchDriver::readSample() {
    TouchSample sample;
    sample.status = TouchReadStatus::Error;
    if (!available) return sample;
    // Poll through a held contact to capture release, even after IRQ goes high.
    const bool pending = cst820b_irq_pending.exchange(false, std::memory_order_relaxed);
    if (!pending && digitalRead(13) != LOW && !last.pressed) {
        sample.status = TouchReadStatus::Unchanged;
        return sample;
    }
    uint8_t data[5];
    if (!m5stack_stopwatch_touch_read(data, sizeof(data))) return sample;
    const uint8_t event = data[1] >> 6;
    if (data[0] > 1 || (data[0] && event == 3)) return sample;
    sample.status = TouchReadStatus::Fresh;
    sample.pressed = data[0] == 1 && event != 1;
    if (sample.pressed) {
        uint16_t x = ((data[1] & 15) << 8) | data[2];
        uint16_t y = ((data[3] & 15) << 8) | data[4];
        if (x < xmin) x = xmin;
        if (x > xmax) x = xmax;
        if (y < ymin) y = ymin;
        if (y > ymax) y = ymax;
        // Factory native X=468, Y=466; crop one X pixel from each side.
        x = uint32_t(x - xmin) * (calibration ? DISPLAY_WIDTH - 1 : 467) / (xmax - xmin);
        y = uint32_t(y - ymin) * (DISPLAY_HEIGHT - 1) / (ymax - ymin);
        if (!calibration) x = x == 0 ? 0 : x - 1;
        if (x >= DISPLAY_WIDTH) x = DISPLAY_WIDTH - 1;
        uint16_t tx = x, ty = y;
        if (rotation == 1) { tx = y; ty = DISPLAY_WIDTH - 1 - x; }
        if (rotation == 2) { tx = DISPLAY_WIDTH - 1 - x; ty = DISPLAY_HEIGHT - 1 - y; }
        if (rotation == 3) { tx = DISPLAY_HEIGHT - 1 - y; ty = x; }
        sample.horizontal = tx; sample.vertical = ty;
    }
    last = sample;
    return sample;
}

void Wire_CST820B_TouchDriver::setCalibration(uint16_t x0, uint16_t x1, uint16_t y0, uint16_t y1) {
    if (x1 <= x0 || y1 <= y0) return;
    xmin = x0; xmax = x1; ymin = y0; ymax = y1;
    calibration = true;
}
