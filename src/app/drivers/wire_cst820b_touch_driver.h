#pragma once
#include "../board_config.h"
#include "../touch_driver.h"

class Wire_CST820B_TouchDriver final : public TouchDriver {
public:
    ~Wire_CST820B_TouchDriver() override;
    void init() override;
    bool isTouched() override;
    bool getTouch(uint16_t* x, uint16_t* y, uint16_t* pressure = nullptr) override;
    TouchSample readSample() override;
    void setCalibration(uint16_t xmin, uint16_t xmax, uint16_t ymin, uint16_t ymax) override;
    void setRotation(uint8_t value) override { rotation = value & 3; }

private:
    bool available = false;
    bool calibration = false;
    uint8_t rotation = 0;
    uint16_t xmin = 0, xmax = 233, ymin = 0, ymax = 233;
    TouchSample last;
};
