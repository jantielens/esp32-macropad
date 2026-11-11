#include "../src/app/board_config.h"
#include "boards/reterminal-sticky-interactive/drivers/display/sticky_epaper_driver.h"
#include "config_manager.h"
#include "presented_frame.h"
#include <esp_heap_caps.h>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <functional>

struct Command { uint8_t code; std::vector<uint8_t> data; };
static std::vector<Command> commands;
static uint32_t clockMs = 1000, resetAt = 0, busyUntil = 0;
static bool ssd2677 = true, disconnected = false, stuck = false, ota = false, boardReady = true;
static int pins[49] = {}, failures = 0, liveAllocations = 0, allocations = 0, failAllocation = 0;
static int liveMutexes = 0, spiBuses = 0;
static bool failMutex = false;
static bool drivePowered = false, powerPinConfigured = false;
static uint8_t probeId = 7, temperature = 25;
static std::function<void()> streamHook, waitHook;
static EpaperRefreshSettings settings = {EPAPER_RENDER_MODE_BW, 1000, 500, true, 2, 0, 128};
#define CHECK(condition) do { if (!(condition)) { std::printf("FAIL %d: %s\n", __LINE__, #condition); ++failures; } } while (0)

extern "C" unsigned long millis() { return clockMs; }
void delay(unsigned long ms) { clockMs += ms; }
void vTaskDelay(TickType_t ticks) { clockMs += ticks; if (waitHook) waitHook(); }
void pinMode(int pin, int mode) {
    if (pin == EPAPER_POWER_PIN) powerPinConfigured = mode == OUTPUT;
}
void digitalWrite(int pin, int value) {
    if (pin == EPAPER_POWER_PIN) CHECK(powerPinConfigured);
    if (pin == 17 && pins[pin] == LOW && value == HIGH) resetAt = clockMs;
    if (pin == EPAPER_POWER_PIN && value == LOW) drivePowered = false;
    pins[pin] = value;
}
int digitalRead(int pin) {
    CHECK(pin == 18);
    if (disconnected) return LOW;
    const bool active = stuck || clockMs - resetAt < 6 || int32_t(busyUntil - clockMs) > 0;
    return active ? (ssd2677 ? LOW : HIGH) : (ssd2677 ? HIGH : LOW);
}
void sticky_test_spi_begin(int clock, int miso, int mosi, int cs) {
    CHECK(clock == 13 && miso == 12 && mosi == 14 && cs == 15);
    ++spiBuses;
}
void sticky_test_spi_end() { --spiBuses; CHECK(spiBuses >= 0); }
uint8_t sticky_test_spi_byte(uint8_t value) {
    CHECK(pins[15] == LOW);
    if (pins[16] == LOW) {
        commands.push_back({value, {}});
        if (ssd2677 && value == 0x04) drivePowered = true;
        if (ssd2677 && value == 0x02) drivePowered = false;
        if ((ssd2677 && (value == 0x04 || value == 0x12 || value == 0xa5 || value == 0x02)) ||
            (!ssd2677 && (value == 0x12 || value == 0x20))) busyUntil = clockMs + 6;
        return 0;
    }
    CHECK(!commands.empty());
    return commands.back().code == 0x70 ? probeId : temperature;
}
void sticky_test_spi_data(const uint8_t* data, size_t length) {
    CHECK(pins[15] == LOW && pins[16] == HIGH && !commands.empty());
    commands.back().data.insert(commands.back().data.end(), data, data + length);
    if (streamHook && length >= 100) streamHook();
}
void* heap_caps_malloc(size_t size, uint32_t caps) {
    CHECK(caps == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (++allocations == failAllocation) return nullptr;
    void* memory = std::malloc(size);
    if (memory) ++liveAllocations;
    return memory;
}
void heap_caps_free(void* memory) { if (memory) { --liveAllocations; std::free(memory); } }
SemaphoreHandle_t test_freertos_create_mutex() {
    if (failMutex) return nullptr;
    auto* semaphore = new StaticSemaphore_t{};
    semaphore->signaled = true;
    ++liveMutexes;
    return semaphore;
}
void test_freertos_delete_mutex(SemaphoreHandle_t semaphore) { --liveMutexes; delete semaphore; }
bool sticky_hardware_ready() { return boardReady; }
bool ota_activity_is_active() { return ota; }
EpaperRefreshSettings config_manager_get_epaper_refresh_settings() { return settings; }

static void reset(bool controller2677 = true, bool gray = false) {
    CHECK(liveAllocations == 0 && liveMutexes == 0 && spiBuses == 0);
    commands.clear();
    clockMs = 1000; resetAt = busyUntil = 0;
    ssd2677 = controller2677; probeId = controller2677 ? 7 : 1;
    disconnected = stuck = ota = failMutex = false;
    drivePowered = false;
    powerPinConfigured = false;
    pins[EPAPER_POWER_PIN] = HIGH;
    boardReady = true; allocations = failAllocation = 0;
    streamHook = waitHook = nullptr; temperature = 25;
    settings = {uint8_t(gray ? EPAPER_RENDER_MODE_GRAYSCALE : EPAPER_RENDER_MODE_BW), 1000, 500, true, 2, 0, 128};
}
static void pixel(StickyEpaperDriver& driver, int x, int y, uint16_t color) {
    driver.flushSrcStride = 0;
    driver.setAddrWindow(x, y, 1, 1);
    driver.pushColors(&color, 1, false);
}
static std::vector<uint8_t> modes() {
    std::vector<uint8_t> result;
    for (const auto& command : commands) {
        if (command.code == (ssd2677 ? 0xe6 : 0x22)) result.push_back(command.data.at(0));
    }
    return result;
}
static const std::vector<uint8_t>& frame(uint8_t code) {
    for (auto command = commands.rbegin(); command != commands.rend(); ++command)
        if (command->code == code) return command->data;
    static const std::vector<uint8_t> empty;
    CHECK(false);
    return empty;
}
static uint8_t snapshotPixel(StickyEpaperDriver& driver, int x, int y) {
    DisplayDriver::PresentedFrameInfo info;
    CHECK(driver.getPresentedFrameInfo(info));
    std::vector<uint8_t> pixels(presented_frame_bytes(info.width, info.height, info.bitDepth));
    CHECK(driver.copyPresentedFrame(pixels.data(), pixels.size()));
    const uint8_t packed = pixels.at(size_t(y) * presented_frame_row_bytes(info.width, info.bitDepth) + x / (info.bitDepth == 1 ? 8 : 2));
    return info.bitDepth == 1 ? bool(packed & (0x80 >> (x & 7))) : (x & 1 ? packed & 15 : packed >> 4);
}
int main() {
    for (bool controller2677 : {false, true}) {
        reset(controller2677);
        {
            StickyEpaperDriver driver;
            driver.init();
            CHECK(driver.isAvailable() && driver.renderMode() == DisplayDriver::RenderMode::Buffered);
            pixel(driver, 0, 0, 0);
            CHECK(modes().empty());
            driver.present();
            CHECK(snapshotPixel(driver, 0, 0) == 0);
            CHECK(snapshotPixel(driver, 1, 0) == 1);
            CHECK(modes() == std::vector<uint8_t>{uint8_t(controller2677 ? 0xee : 0xf7)});
            const auto first = frame(controller2677 ? 0x10 : 0x24);
            CHECK(first.size() == (controller2677 ? 96000 : 48000));
            CHECK(first.at(controller2677 ? 199 : 99) == (controller2677 ? 0xfc : 0xfe));
            const size_t commandCount = commands.size();
            pixel(driver, 0, 0, 0);
            driver.present();
            CHECK(commands.size() == commandCount);
            pixel(driver, 1, 0, 0);
            driver.present();
            CHECK(modes().back() == (controller2677 ? 0x19 : 0xff));
            const auto second = frame(controller2677 ? 0x10 : 0x24);
            CHECK(second.at(controller2677 ? 199 : 99) == (controller2677 ? 0xf8 : 0xfc));
            if (!controller2677) CHECK(frame(0x26).at(99) == 0xfe);
            pixel(driver, 2, 0, 0); driver.present();
            pixel(driver, 3, 0, 0); driver.present();
            CHECK(modes().back() == (controller2677 ? 0xee : 0xf7));
            CHECK(driver.requestFullRefresh()); driver.present();
            CHECK(modes().back() == (controller2677 ? 0xee : 0xf7));
            // Padded stride, clipping and unaligned pixels preserve neighbors.
            uint16_t data[] = {0, 0xffff, 0x1234, 0xffff, 0, 0x1234};
            driver.flushSrcStride = 6;
            driver.setAddrWindow(-1, 10, 2, 2);
            driver.pushColors(data, 4, false);
            driver.present();
            CHECK(snapshotPixel(driver, 0, 10) == 1 && snapshotPixel(driver, 0, 11) == 0);
            for (uint8_t rotation = 0; rotation < 4; ++rotation) {
                driver.setRotation(rotation);
                pixel(driver, driver.width() - 1, driver.height() - 1, 0);
                driver.present();
                CHECK(snapshotPixel(driver, driver.width() - 1, driver.height() - 1) == 0);
            }
        }
        for (bool gray : {false, true}) {
            reset(controller2677, gray);
            {
                StickyEpaperDriver driver;
                driver.init();
                uint16_t ramp[] = {0, 0x52aa, 0xad55, 0xffff};
                driver.setAddrWindow(0, 0, 4, 1);
                driver.pushColors(ramp, 4, false);
                driver.present();
                if (gray) {
                    for (int x = 0; x < 4; ++x) CHECK(snapshotPixel(driver, x, 0) == x * 5);
                    if (controller2677) CHECK(frame(0x10).at(0) == 0x1b);
                    else {
                        CHECK(frame(0x24).at(0) == 0xc0);
                        CHECK(frame(0x26).at(0) == 0xa0);
                    }
                    pixel(driver, 4, 0, 0); driver.present();
                    CHECK(modes() == (controller2677 ? std::vector<uint8_t>{0xfa, 0xfa} : std::vector<uint8_t>{0xd7, 0xd7}));
                }
                // OTA entry during streaming leaves no completed snapshot and retries full.
                pixel(driver, 8, 8, 0);
                streamHook = [] { ota = true; };
                driver.present();
                CHECK(pins[15] == HIGH);
                DisplayDriver::PresentedFrameInfo info;
                CHECK(!driver.getPresentedFrameInfo(info));
                const size_t count = commands.size();
                driver.present(); CHECK(commands.size() == count);
                streamHook = nullptr; ota = false;
                driver.present();
                CHECK(driver.getPresentedFrameInfo(info) && snapshotPixel(driver, 8, 8) == 0);
                CHECK(modes().back() == (controller2677 ? (gray ? 0xfa : 0xee) : (gray ? 0xd7 : 0xf7)));
                pixel(driver, 9, 8, 0);
                stuck = true;
                const uint32_t start = clockMs;
                driver.present();
                CHECK(driver.isAvailable() && clockMs - start < 16000);
                CHECK(!driver.getPresentedFrameInfo(info));
                stuck = false;
                CHECK(driver.requestFullRefresh());
                driver.present();
                CHECK(driver.isAvailable() && driver.getPresentedFrameInfo(info));
            }
        }
        reset(controller2677);
        probeId = 0;
        { StickyEpaperDriver driver; driver.init(); CHECK(driver.isAvailable()); }
    }
    reset();
    disconnected = true; probeId = 0xff;
    { StickyEpaperDriver driver; driver.init(); CHECK(!driver.isAvailable() && liveAllocations == 0); }
    reset();
    probeId = 1; // Driven but misclassified ID must recover only with a real BUSY transition.
    { StickyEpaperDriver driver; driver.init(); CHECK(driver.isAvailable()); }
    reset();
    stuck = true;
    { StickyEpaperDriver driver; driver.init(); CHECK(!driver.isAvailable() && liveAllocations == 0); }
    reset();
    {
        StickyEpaperDriver driver;
        driver.init();
        pixel(driver, 0, 0, 0);
        driver.present();
        pixel(driver, 1, 0, 0);
        const size_t count = commands.size();
        waitHook = [] { ota = true; };
        driver.present();
        CHECK(commands.size() == count); // Cancel cadence wait before touching the controller.
        waitHook = nullptr; ota = false;
        driver.present();
        CHECK(snapshotPixel(driver, 1, 0) == 0);
        pixel(driver, 2, 0, 0);
        settings.epaper_min_refresh_interval_ms = 0;
        clockMs += 200;
        waitHook = [] { ota = true; };
        driver.present(); // Cancel an owned BUSY wait after refresh has started.
        DisplayDriver::PresentedFrameInfo info;
        CHECK(!driver.getPresentedFrameInfo(info) && pins[15] == HIGH);
        waitHook = nullptr; ota = false;
        driver.present();
        CHECK(snapshotPixel(driver, 2, 0) == 0 && modes().back() == 0xee);
    }
    for (uint8_t cancelAt : {0x04, 0x12, 0x02}) {
        reset();
        {
            StickyEpaperDriver driver;
            driver.init();
            pixel(driver, 0, 0, 0);
            waitHook = [cancelAt] {
                if (!commands.empty() && commands.back().code == cancelAt) ota = true;
            };
            const uint32_t start = clockMs;
            driver.present();
            CHECK(ota && !drivePowered && pins[15] == HIGH);
            CHECK(commands.back().code == 0x02 && clockMs - start < 1000);
            DisplayDriver::PresentedFrameInfo info;
            CHECK(driver.getPresentedFrameInfo(info) == (cancelAt == 0x02));
            const size_t count = commands.size();
            driver.present();
            CHECK(commands.size() == count);
            waitHook = nullptr; ota = false;
            CHECK(driver.isAvailable() && driver.requestFullRefresh());
            driver.present();
            CHECK(snapshotPixel(driver, 0, 0) == 0 && modes().back() == 0xee);
        }
    }
    for (uint8_t failAt : {0x04, 0x02}) {
        reset();
        {
            StickyEpaperDriver driver;
            driver.init();
            pixel(driver, 0, 0, 0);
            waitHook = [failAt] {
                if (!commands.empty() && commands.back().code == failAt) stuck = true;
            };
            const uint32_t start = clockMs;
            driver.present();
            CHECK(!drivePowered && pins[EPAPER_POWER_PIN] == LOW && clockMs - start < 16000);
            DisplayDriver::PresentedFrameInfo info;
            CHECK(!driver.getPresentedFrameInfo(info));
            waitHook = nullptr; stuck = false;
            CHECK(driver.isAvailable() && driver.requestFullRefresh());
            driver.present();
            CHECK(pins[EPAPER_POWER_PIN] == HIGH && !drivePowered);
            CHECK(snapshotPixel(driver, 0, 0) == 0 && modes().back() == 0xee);
        }
    }
    for (int failure = 1; failure <= 4; ++failure) {
        reset(); failAllocation = failure; failMutex = failure == 4;
        { StickyEpaperDriver driver; driver.init(); CHECK(!driver.isAvailable() && liveAllocations == 0 && liveMutexes == 0); }
    }
    reset(); boardReady = false;
    { StickyEpaperDriver driver; driver.init(); CHECK(!driver.isAvailable() && allocations == 0); }
    reset();
    std::printf("Sticky display checks: %d failures\n", failures);
    return failures ? 1 : 0;
}
