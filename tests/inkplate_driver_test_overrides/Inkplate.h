#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

#define INKPLATE_1BIT 0
#define INKPLATE_3BIT 1

class Inkplate;
bool inkplate_test_power_on();
void inkplate_test_power_off();
void inkplate_test_refresh(Inkplate& display, bool partial);

class Inkplate {
public:
    struct Frontlight {
        bool enabled = false;
        uint8_t brightness = 0;
        unsigned stateChanges = 0, brightnessChanges = 0;
        void setState(bool state) { enabled = state; ++stateChanges; }
        void setBrightness(uint8_t value) { brightness = value; ++brightnessChanges; }
    } frontlight;

    explicit Inkplate(uint8_t selected)
        : mono(1024u * 758 / 8), gray(1024u * 758 / 2), previous(mono.size()),
          _partial(mono.data()), DMemory4Bit(gray.data()), mode(selected) {}
    void begin() { begun = true; }
    void selectDisplayMode(uint8_t selected) { mode = selected; }
    void setRotation(uint8_t value) { rotation = value; }
    void setFullUpdateThreshold(uint16_t value) { limit = value; }
    void clearDisplay() {
        if (mode == INKPLATE_1BIT) std::fill(mono.begin(), mono.end(), 0);
        else std::fill(gray.begin(), gray.end(), 255);
    }
    bool einkOn() { return inkplate_test_power_on(); }
    void einkOff() { inkplate_test_power_off(); }
    void display() {
        inkplate_test_refresh(*this, false);
        if (mode == INKPLATE_1BIT) std::copy(mono.begin(), mono.end(), previous.begin());
        einkOff();
    }
    uint32_t partialUpdate(bool) {
        inkplate_test_refresh(*this, true);
        std::copy(mono.begin(), mono.end(), previous.begin());
        einkOff();
        return 0;
    }

    std::vector<uint8_t> mono, gray, previous;
    uint8_t* _partial;
    uint8_t* DMemory4Bit;
    uint8_t mode, rotation = 0;
    uint16_t limit = 10;
    bool begun = false;
};
