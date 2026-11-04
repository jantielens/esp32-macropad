#pragma once

#include "../board_config.h"
#include "../touch_driver.h"

class ST7123TouchDriver final : public TouchDriver {
public:
    void init() override;
    bool isTouched() override;
    bool getTouch(uint16_t* x, uint16_t* y, uint16_t* pressure = nullptr) override;
    TouchSample readSample() override;
    TouchSnapshot readSnapshot() override;
    uint8_t contactCapacity() const override { return TOUCH_CONTACT_CAPACITY; }
    void setCalibration(uint16_t xmin, uint16_t xmax, uint16_t ymin, uint16_t ymax) override;
    void setRotation(uint8_t value) override { rotation = value & 3; }

private:
    bool available = false;
    uint8_t rotation = 0;
    uint16_t xmin = 0, xmax = DISPLAY_WIDTH - 1;
    uint16_t ymin = 0, ymax = DISPLAY_HEIGHT - 1;
};