#pragma once

#include "app/display_driver.h"
#include "app/drivers/epaper_schedule.h"
#include "sticky_epaper/panel.h"
#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

class StickyEpaperDriver : public DisplayDriver {
public:
    ~StickyEpaperDriver() override;
    void init() override;
    void setRotation(uint8_t value) override { rotation = value & 3; }
    int width() override { return rotation & 1 ? 480 : 800; }
    int height() override { return rotation & 1 ? 800 : 480; }
    void setBacklight(bool) override {}
    void setBacklightBrightness(uint8_t) override {}
    uint8_t getBacklightBrightness() override { return 0; }
    bool hasBacklightControl() override { return false; }
    void applyDisplayFixes() override {}
    void startWrite() override {}
    void endWrite() override {}
    void setAddrWindow(int16_t x, int16_t y, uint16_t w, uint16_t h) override;
    void pushColors(uint16_t* data, uint32_t len, bool swap_bytes) override;
    RenderMode renderMode() const override { return RenderMode::Buffered; }
    void present() override;
    bool coherentFrames() const override { return true; }
    void beginFrame() override;
    void commitFrame(const DisplayFrame& frame) override;
    void stopPresentation() override;
#if HAS_AUTO_SHUTDOWN
    bool hasPresentedInteraction(uint32_t request_sequence) override;
#endif
    bool requestFullRefresh() override;
    bool isAvailable() const override { return available.load(); }
    int presentationMode() const override { return grayscale ? 1 : 0; }
    bool getPresentedFrameInfo(PresentedFrameInfo& info) const override;
    bool copyPresentedFrame(uint8_t* pixels, size_t length) override;
#if HAS_DISPLAY_DIAGNOSTICS
    bool getPresentationStats(DisplayPresentationStats* out) override;
#endif

private:
    sticky_epaper::Panel panel;
    SemaphoreHandle_t mutex = nullptr;
    StaticSemaphore_t wakeStorage{};
    SemaphoreHandle_t wake = nullptr;
    EpaperSchedule schedule;
    std::atomic<bool> stopping{false};
    uint8_t* drawing = nullptr;
    uint8_t* sending = nullptr;
    uint8_t* presented = nullptr;
    size_t frameBytes = 0;
    std::atomic<bool> available{false};
    bool grayscale = false;
    uint8_t rotation = 0, bwConversion = 0, bwThreshold = 128;
    int16_t currentX = 0, currentY = 0;
    uint16_t currentW = 0, currentH = 0;
    bool pending = false, forceFull = false, baselineValid = false;
    uint32_t lastChange = 0, firstPending = 0, lastRefresh = 0;
    uint16_t partialsSinceFull = 0;
#if HAS_AUTO_SHUTDOWN
    uint32_t presentedRequestSequence = 0;
#endif
#if HAS_DISPLAY_DIAGNOSTICS
    DisplayPresentationStats stats;
    uint64_t hardwareCompletedUs = 0;
#endif
    void releaseResources();
};
