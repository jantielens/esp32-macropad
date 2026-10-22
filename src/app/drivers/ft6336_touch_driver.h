#pragma once

#include "../touch_driver.h"

class FT6336_TouchDriver final : public TouchDriver {
public:
    void init() override;
    bool isTouched() override;
    bool getTouch(uint16_t* x, uint16_t* y, uint16_t* pressure = nullptr) override;
    TouchSample readSample() override;
    void setCalibration(uint16_t x_min, uint16_t x_max,
                        uint16_t y_min, uint16_t y_max) override;
    void setRotation(uint8_t rotation) override;

private:
    bool available = false;
    bool calibrated = false;
    uint16_t xmin = 0;
    uint16_t xmax = 0;
    uint16_t ymin = 0;
    uint16_t ymax = 0;
};