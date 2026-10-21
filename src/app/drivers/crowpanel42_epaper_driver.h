#pragma once

#include "../display_driver.h"
#include <SPI.h>
#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

class CrowPanel42EpaperDriver : public DisplayDriver {
public:
    ~CrowPanel42EpaperDriver() override;
    void init() override;
    void setRotation(uint8_t value) override { rotation = value & 3; }
    int width() override { return rotation & 1 ? 300 : 400; }
    int height() override { return rotation & 1 ? 400 : 300; }
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
    bool requestFullRefresh() override;
    bool isAvailable() const override { return available.load(); }
    int presentationMode() const override { return 0; }
    bool getPresentedFrameInfo(PresentedFrameInfo& info) const override;
    bool copyPresentedFrame(uint8_t* pixels, size_t length) override;

private:
    static constexpr size_t framebufferBytes = 15000;
    SPIClass spi{FSPI};
    SemaphoreHandle_t mutex = nullptr;
    uint8_t* drawing = nullptr;
    uint8_t* snapshot = nullptr;
    std::atomic<bool> available{false};
    uint8_t rotation = 0;
    uint8_t bwConversion = 0, bwThreshold = 128;
    int16_t currentX = 0, currentY = 0;
    uint16_t currentW = 0, currentH = 0;
    bool pending = false, forceFull = false, hasPresented = false;
    uint32_t lastChange = 0, firstPendingChange = 0, lastRefresh = 0;
    uint16_t partialsSinceFull = 0;
    void releaseResources();
    void send(uint8_t command, const uint8_t* data = nullptr, size_t length = 0);
    void fillRam(uint8_t command);
    bool waitReady(const char* stage);
    bool initializePanel(uint32_t resetSettleMs = 0);
    bool update(uint8_t mode);
    bool refresh(bool full);
};