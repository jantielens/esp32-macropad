#include <Inkplate.h>
#include "boards/inkplate6flick-interactive/drivers/inkplate6flick_epaper_driver.h"
#include "app/presented_frame.h"
#include <esp_heap_caps.h>

#include <cstdio>
#include <cstdlib>
#include <functional>
#include <unordered_map>

static unsigned failures = 0, fullCalls = 0, partialCalls = 0, powerOffCalls = 0;
static unsigned liveMutexes = 0;
static uint32_t clockMs = 1000;
static bool ota = false, powerFailure = false, failAllocation = false, failMutex = false, powered = false;
static std::function<void()> refreshHook, powerHook, waitHook;
static std::unordered_map<void*, size_t> allocations;
static std::vector<uint8_t> physical;
static EpaperRefreshSettings settings{EPAPER_RENDER_MODE_BW, 1000, 250, true, 10, 0, 128};

#define CHECK(condition) do { if (!(condition)) { \
    std::printf("FAIL %d: %s\n", __LINE__, #condition); ++failures; \
} } while (0)

unsigned long millis() { return clockMs; }
unsigned long micros() { return uint32_t(clockMs * 1000); }
bool ota_activity_is_active() { return ota; }
EpaperRefreshSettings config_manager_get_epaper_refresh_settings() { return settings; }
SemaphoreHandle_t test_freertos_create_mutex() {
    if (failMutex) return nullptr;
    auto* mutex = new StaticSemaphore_t{};
    mutex->signaled = true;
    ++liveMutexes;
    return mutex;
}
void test_freertos_delete_mutex(SemaphoreHandle_t mutex) { --liveMutexes; delete mutex; }
void* heap_caps_malloc(size_t size, uint32_t caps) {
    CHECK(caps == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (failAllocation) return nullptr;
    void* result = std::malloc(size);
    if (result) allocations.emplace(result, size);
    return result;
}
void heap_caps_free(void* pointer) {
    CHECK(allocations.erase(pointer) == 1);
    std::free(pointer);
}
bool inkplate_test_power_on() {
    powered = !powerFailure;
    if (powered && powerHook) powerHook();
    return powered;
}
void inkplate_test_power_off() { powered = false; ++powerOffCalls; }
void inkplate_test_refresh(Inkplate& display, bool partial) {
    CHECK(powered);
    CHECK(display._partial == display.mono.data() && display.DMemory4Bit == display.gray.data());
    if (partial) CHECK(display.mode == INKPLATE_1BIT);
    const auto& source = display.mode == INKPLATE_1BIT ? display.mono : display.gray;
    physical = source;
    if (partial) ++partialCalls;
    else ++fullCalls;
    if (refreshHook) refreshHook();
    CHECK(source == physical);
    clockMs += 50;
}

static void reset() {
    CHECK(allocations.empty() && liveMutexes == 0 && !powered);
    fullCalls = partialCalls = powerOffCalls = 0;
    clockMs = 1000;
    ota = powerFailure = failAllocation = failMutex = false;
    refreshHook = powerHook = waitHook = nullptr;
    settings.epaper_min_refresh_interval_ms = 250;
    settings.epaper_full_refresh_threshold = 10;
    test_freertos_force_wait_timeout = true;
    test_freertos_semaphore_take_hook = [](SemaphoreHandle_t, TickType_t ticks) {
        if (ticks && ticks != portMAX_DELAY) {
            CHECK(ticks <= 20);
            clockMs += ticks;
            if (waitHook) waitHook();
        }
    };
}

static void pixel(Inkplate6FlickEpaperDriver& driver, int x, int y, uint16_t color) {
    driver.setAddrWindow(x, y, 1, 1);
    driver.pushColors(&color, 1, false);
}

static void baseline(Inkplate6FlickEpaperDriver& driver) {
    driver.init();
    CHECK(driver.isAvailable() && allocations.size() == 1 && liveMutexes == 1);
    auto* library = inkplate6flick_lvgl_instance();
    CHECK(library && library->begun);
    CHECK(library->_partial == library->mono.data() && library->DMemory4Bit == library->gray.data());
    CHECK(allocations.begin()->second == (driver.presentationMode() == EPAPER_RENDER_MODE_BW ? 97024u : 388096u));
    std::vector<uint16_t> white(size_t(driver.width()) * driver.height(), 0xFFFF);
    driver.setAddrWindow(0, 0, driver.width(), driver.height());
    driver.pushColors(white.data(), white.size(), false);
    driver.present();
    CHECK(fullCalls == 1 && partialCalls == 0 && !powered);
}

static bool snapshot(Inkplate6FlickEpaperDriver& driver, std::vector<uint8_t>& data) {
    DisplayDriver::PresentedFrameInfo info;
    if (!driver.getPresentedFrameInfo(info)) return false;
    data.resize(presented_frame_bytes(info.width, info.height, info.bitDepth));
    return driver.copyPresentedFrame(data.data(), data.size());
}

static uint8_t capturedGray(Inkplate6FlickEpaperDriver& driver,
                            const std::vector<uint8_t>& data, int x, int y) {
    if (driver.presentationMode() == EPAPER_RENDER_MODE_BW)
        return data[size_t(y) * presented_frame_row_bytes(driver.width(), 1) + x / 8]
            & (0x80 >> (x & 7)) ? 1 : 0;
    const uint8_t packed = data[size_t(y) * (driver.width() / 2) + x / 2];
    return x & 1 ? packed & 15 : packed >> 4;
}

static void checkOwnershipAndPending(int mode) {
    reset();
    DeviceConfig config{};
    config.epaper_render_mode = mode;
    config.epaper_bw_threshold = 128;
    Inkplate6FlickEpaperDriver driver(&config);
    driver.setRotation(1);
    baseline(driver);
    auto* library = inkplate6flick_lvgl_instance();
    const auto oldPhysical = physical;
    pixel(driver, 2, 3, 0);
    CHECK((mode == EPAPER_RENDER_MODE_BW ? library->mono : library->gray) == oldPhysical);
    std::vector<uint8_t> capture;
    CHECK(snapshot(driver, capture) && capturedGray(driver, capture, 2, 3) == (mode == EPAPER_RENDER_MODE_BW ? 1 : 15));
    refreshHook = [&] {
        CHECK(!snapshot(driver, capture));
        pixel(driver, 6, 3, mode == EPAPER_RENDER_MODE_BW ? 0 : 0x8410);
        CHECK(driver.requestFullRefresh());
    };
    driver.present();
    refreshHook = nullptr;
    CHECK(snapshot(driver, capture));
    CHECK(capturedGray(driver, capture, 2, 3) == 0);
    CHECK(capturedGray(driver, capture, 6, 3) == (mode == EPAPER_RENDER_MODE_BW ? 1 : 15));
    CHECK(library->_partial == library->mono.data() && library->DMemory4Bit == library->gray.data());
    driver.present();
    CHECK(fullCalls == (mode == EPAPER_RENDER_MODE_BW ? 2u : 3u));
    CHECK(partialCalls == (mode == EPAPER_RENDER_MODE_BW ? 1u : 0u));
    CHECK(snapshot(driver, capture));
    CHECK(capturedGray(driver, capture, 6, 3) == (mode == EPAPER_RENDER_MODE_BW ? 0 : 9));
    const unsigned calls = fullCalls + partialCalls;
    pixel(driver, 2, 3, 0);
    driver.present();
    CHECK(fullCalls + partialCalls == calls);
}

static void checkWaitingAndRecovery() {
    reset();
    Inkplate6FlickEpaperDriver driver(nullptr);
    baseline(driver);
    pixel(driver, 2, 0, 0);
    unsigned updates = 0;
    const uint32_t started = clockMs;
    uint32_t lastFlush = started;
    waitHook = [&] {
        if (clockMs - started < 900 && clockMs - lastFlush >= 100) {
            lastFlush = clockMs;
            pixel(driver, 4 + updates++, 0, 0);
            if (updates == 1) CHECK(driver.requestFullRefresh());
        }
    };
    driver.present();
    waitHook = nullptr;
    CHECK(updates == 8 && clockMs - started == 800 + 150 + 50);
    CHECK(fullCalls == 2);

    pixel(driver, 20, 0, 0);
    waitHook = [] { ota = true; };
    const uint32_t beforeOta = clockMs;
    driver.present();
    CHECK(clockMs - beforeOta <= 20 && fullCalls == 2);
    waitHook = nullptr;
    ota = false;
    driver.present();
    CHECK(partialCalls == 1);

    pixel(driver, 24, 0, 0);
    powerHook = [] { ota = true; };
    driver.present();
    CHECK(!powered && partialCalls == 1);
    std::vector<uint8_t> capture;
    CHECK(!snapshot(driver, capture));
    powerHook = nullptr;
    ota = false;
    driver.present();
    CHECK(fullCalls == 3 && snapshot(driver, capture));

    CHECK(driver.requestFullRefresh());
    powerFailure = true;
    driver.present();
    CHECK(!snapshot(driver, capture) && fullCalls == 3);
    powerFailure = false;
    driver.present();
    CHECK(fullCalls == 4 && snapshot(driver, capture));

    settings.epaper_full_refresh_threshold = 1;
    pixel(driver, 28, 0, 0);
    driver.present();
    CHECK(partialCalls == 2);
    pixel(driver, 32, 0, 0);
    driver.present();
    CHECK(fullCalls == 5);
    driver.setBacklightBrightness(50);
    auto* library = inkplate6flick_lvgl_instance();
    CHECK(library->frontlight.enabled && library->frontlight.brightness == 32);
    const unsigned changes = library->frontlight.stateChanges + library->frontlight.brightnessChanges;
    driver.setBacklightBrightness(50);
    CHECK(library->frontlight.stateChanges + library->frontlight.brightnessChanges == changes);
    driver.setBacklightBrightness(0);
    CHECK(!library->frontlight.enabled && snapshot(driver, capture));
}

int main() {
    checkOwnershipAndPending(EPAPER_RENDER_MODE_BW);
    checkOwnershipAndPending(EPAPER_RENDER_MODE_GRAYSCALE);
    checkWaitingAndRecovery();
    for (bool mutexFailure : {false, true}) {
        reset();
        {
            Inkplate6FlickEpaperDriver driver(nullptr);
            failMutex = mutexFailure;
            failAllocation = !mutexFailure;
            driver.init();
            CHECK(!driver.isAvailable() && allocations.empty() && liveMutexes == 0);
            CHECK(!driver.requestFullRefresh());
            failMutex = failAllocation = false;
            baseline(driver);
            driver.init();
            CHECK(driver.isAvailable() && allocations.size() == 1 && liveMutexes == 1);
        }
        auto* library = inkplate6flick_lvgl_instance();
        CHECK(library->_partial == library->mono.data() && library->DMemory4Bit == library->gray.data());
    }
    CHECK(allocations.empty() && liveMutexes == 0);
    delete inkplate6flick_lvgl_instance();
    std::printf("%s: Inkplate library-owned buffers, waiting and recovery\n", failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
