#pragma once
#include "../board_config.h"
#include "../touch_driver.h"

struct CST820BConfig {
    uint8_t address = 0x15;
    int interruptPin = TOUCH_INT;
    uint16_t nativeWidth = DISPLAY_WIDTH, nativeHeight = DISPLAY_HEIGHT;
    uint16_t cropX = 0, cropY = 0;
    bool (*prepare)() = nullptr;
};

CST820BConfig board_cst820b_config();

class Wire_CST820B_TouchDriver final : public TouchDriver {
public:
    explicit Wire_CST820B_TouchDriver(const CST820BConfig& value)
        : config(value), xmax(value.nativeWidth - 1), ymax(value.nativeHeight - 1) {}
    ~Wire_CST820B_TouchDriver() override;
    void init() override;
    bool isTouched() override;
    bool getTouch(uint16_t* x, uint16_t* y, uint16_t* pressure = nullptr) override;
    TouchSample readSample() override;
    void setCalibration(uint16_t xmin, uint16_t xmax, uint16_t ymin, uint16_t ymax) override;
    void setRotation(uint8_t value) override { rotation = value & 3; }

private:
    CST820BConfig config;
    bool available = false;
    bool calibration = false;
    bool recovering = false;
    uint32_t retryAfter = 0;
    uint8_t rotation = 0;
    uint16_t xmin = 0, xmax, ymin = 0, ymax;
    TouchSample last;
};
