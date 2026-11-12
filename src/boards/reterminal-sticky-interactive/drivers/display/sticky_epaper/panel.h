#pragma once

#include <SPI.h>
#include <atomic>

namespace sticky_epaper {

enum class Result { Success, Deferred, Error };

// Protocol subset adapted from Seeed-Studio/Seeed_GFX2, MIT, revision
// 0e8bac1188cb915d5ea26ea16d538aed13b3cf48:
// Driver_Sticky_Auto.cpp, Driver_SSD1677.cpp, Driver_SSD2677.cpp.
// Local changes: no graphics framework or private framebuffers, strict presence
// detection, checked BUSY results, cooperative OTA cancellation, chunked transfers.
class Panel {
public:
    bool begin();
    void end();
    Result refresh(const uint8_t* current, const uint8_t* previous, bool grayscale, bool full);
    void setStopFlag(const std::atomic<bool>* flag) { stopFlag = flag; }
#if HAS_DISPLAY_DIAGNOSTICS
    struct Timing {
        uint32_t preparation_us = 0;
        uint32_t upload_us = 0;
        uint32_t pack_us = 0;
        uint32_t spi_us = 0;
        uint32_t yield_us = 0;
        uint32_t uploaded_bytes = 0;
        uint64_t trigger_us = 0;
        uint64_t waveform_done_us = 0;
    } timing;
#endif

private:
    enum class Controller { Unknown, SSD1677, SSD2677 };
    SPIClass spi{HSPI};
    Controller controller = Controller::Unknown;
    bool started = false;
    bool powerCut = false;
    bool temperatureWarning = false;
    const std::atomic<bool>* stopFlag = nullptr;
    bool cancelled() const;
    void command(uint8_t code, const uint8_t* data = nullptr, size_t length = 0);
    uint8_t read();
    void reset(uint32_t lowMs, uint32_t highMs);
    Result waitReady(const char* stage, bool deferForOta = true);
    void restorePower();
    Result finishRefresh(Result result);
    bool detectByBusy();
    Result initialize(bool grayscale);
    Result latchWaveform(bool grayscale, bool full);
    Result stream(uint8_t code, const uint8_t* current, const uint8_t* previous,
                  bool grayscale, bool full, bool highPlane = false);
};
}
