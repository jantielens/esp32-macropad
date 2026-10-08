#pragma once
#include "../board_config.h"
#include "../display_driver.h"
#include <Arduino_GFX_Library.h>

class Arduino_GFX_CO5300_Driver final : public DisplayDriver {
public:
    ~Arduino_GFX_CO5300_Driver() override;
    void init() override;
    bool isAvailable() const override { return available; }
    void setRotation(uint8_t value) override { rotation = value & 3; }
    int width() override { return (rotation & 1) ? DISPLAY_HEIGHT : DISPLAY_WIDTH; }
    int height() override { return (rotation & 1) ? DISPLAY_WIDTH : DISPLAY_HEIGHT; }
    void setBacklight(bool on) override;
    void setBacklightBrightness(uint8_t value) override;
    uint8_t getBacklightBrightness() override { return brightness; }
    bool hasBacklightControl() override { return true; }
    void applyDisplayFixes() override {}
    void startWrite() override {}
    void endWrite() override {}
    void setAddrWindow(int16_t x, int16_t y, uint16_t w, uint16_t h) override;
    void pushColors(uint16_t* data, uint32_t len, bool swap_bytes = true) override;
    void displaySleep() override;
    void displayWake() override;
    void displayWakeSleepOut() override;
    void displayWakeDisplayOn() override;

private:
    Arduino_DataBus* bus = nullptr;
    Arduino_CO5300* gfx = nullptr;
    uint16_t* scratch = nullptr;
    bool available = false;
    bool backlightOn = true;
    uint8_t rotation = 0;
    uint8_t brightness = 100;
    int16_t x = 0, y = 0;
    uint16_t w = 0, h = 0;
};
