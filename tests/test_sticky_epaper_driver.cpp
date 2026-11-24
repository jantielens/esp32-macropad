#include "../src/app/board_config.h"
#include "boards/reterminal-sticky-interactive/sticky_hardware.h"
#include "boards/reterminal-sticky-interactive/drivers/display/sticky_epaper_driver.h"
#include "config_manager.h"
#include "presented_frame.h"
#include "display_frame_publisher.h"
#include "app/drivers/monochrome_render.h"
#include <esp_heap_caps.h>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <functional>
#include <algorithm>
#include <chrono>
#include <cstring>

struct Command { uint8_t code; std::vector<uint8_t> data; };
static std::vector<Command> commands;
struct SpiTransaction {
    SPISettings settings;
    size_t bytes = 0;
    size_t payloadBytes = 0;
    bool ended = false;
};
static std::vector<SpiTransaction> transactions;
static bool transactionOpen = false;
static uint32_t clockMs = 1000, resetAt = 0, busyFrom = 0, busyUntil = 0, commandDelayMs = 0;
static uint32_t clockFractionUs = 0;
static bool simulateSpiTime = false;
static uint32_t refreshAt = 0, refreshDelayMs = 0, refreshDurationMs = 6;
static bool refreshScheduled = false, commandBusy = true;
static bool ssd2677 = true, disconnected = false, stuck = false, ota = false, boardReady = true;
static int pins[49] = {}, failures = 0, liveAllocations = 0, allocations = 0, failAllocation = 0;
static int liveMutexes = 0, spiBuses = 0;
static bool failMutex = false;
static bool drivePowered = false, powerPinConfigured = false;
static unsigned hardwareResets = 0;
struct RamState {
    unsigned xStart = 0, xEnd = 799, yStart = 0, yEnd = 479, x = 0, y = 0;
    std::vector<uint8_t> current, previous;
};
static RamState ram;
static bool modelRam = false;
static uint8_t probeId = 7, temperature = 25;
static uint8_t selectedBank = 0;
static std::function<void()> streamHook, waitHook, activationHook, otaCheckHook;
static uint32_t sequence = 0;
static EpaperRefreshSettings settings = {EPAPER_RENDER_MODE_BW, 1000, 500, true, 2, 0, 128};
#define CHECK(condition) do { if (!(condition)) { std::printf("FAIL %d: %s\n", __LINE__, #condition); ++failures; } } while (0)

extern "C" unsigned long millis() { return clockMs; }
int64_t esp_timer_get_time() { return uint64_t(clockMs) * 1000 + clockFractionUs; }
void delay(unsigned long ms) { clockMs += ms; }
void vTaskDelay(TickType_t ticks) {
    CHECK(!transactionOpen);
    clockMs += ticks;
    if (waitHook) waitHook();
}
void pinMode(int pin, int mode) {
    if (pin == EPAPER_POWER_PIN) powerPinConfigured = mode == OUTPUT;
}
static bool refreshInFlight() {
    return refreshScheduled && refreshDurationMs &&
        clockMs - refreshAt < refreshDelayMs + refreshDurationMs;
}
void digitalWrite(int pin, int value) {
#if USE_SD_STORAGE
    if (pin == EPAPER_CS_PIN && value == LOW)
        CHECK(transactionOpen && transactions.back().bytes > 0);
#endif
    if (pin == EPAPER_POWER_PIN) CHECK(powerPinConfigured);
    if (pin == EPAPER_RESET_PIN && value == LOW) CHECK(!refreshInFlight());
    if (pin == EPAPER_RESET_PIN && value == LOW) ++hardwareResets;
    if (pin == EPAPER_RESET_PIN && value == LOW) selectedBank = 0;
    if (pin == 17 && pins[pin] == LOW && value == HIGH) resetAt = clockMs;
    if (pin == EPAPER_POWER_PIN && value == LOW) drivePowered = false;
    pins[pin] = value;
}
int digitalRead(int pin) {
    CHECK(pin == 18);
    if (disconnected) return LOW;
    const uint32_t refreshElapsed = clockMs - refreshAt;
    const bool refreshBusy = refreshScheduled && refreshDurationMs &&
        refreshElapsed >= refreshDelayMs && refreshElapsed - refreshDelayMs < refreshDurationMs;
    const bool active = stuck || clockMs - resetAt < 6 ||
        (int32_t(clockMs - busyFrom) >= 0 && int32_t(busyUntil - clockMs) > 0) || refreshBusy;
    return active ? (ssd2677 ? LOW : HIGH) : (ssd2677 ? HIGH : LOW);
}
void sticky_test_spi_begin(int clock, int miso, int mosi, int cs) {
    CHECK(clock == 13 && miso == 12 && mosi == 14 && cs == 15);
    ++spiBuses;
}
void sticky_test_spi_end() {
    CHECK(!transactionOpen);
    --spiBuses;
    CHECK(spiBuses >= 0);
}
void sticky_test_spi_begin_transaction(const SPISettings& settings) {
    CHECK(!transactionOpen && spiBuses == 1);
    CHECK(settings.bitOrder == MSBFIRST && settings.mode == SPI_MODE0);
    transactions.push_back({settings});
    transactionOpen = true;
}
void sticky_test_spi_end_transaction() {
    CHECK(transactionOpen && pins[15] == HIGH);
    transactions.back().ended = true;
    transactionOpen = false;
}
uint8_t sticky_test_spi_byte(uint8_t value) {
#if USE_SD_STORAGE
    if (pins[15] == HIGH) {
        CHECK(transactionOpen && value == 0xff &&
              (transactions.back().settings.clock == 4000000u || transactions.back().settings.clock == 20000000u));
        ++transactions.back().bytes;
        return 0xff;
    }
#endif
    CHECK(transactionOpen && pins[15] == LOW);
    CHECK(transactions.back().settings.clock == (pins[16] == LOW ? 20000000u : 4000000u));
    ++transactions.back().bytes;
    if (pins[16] == LOW) {
        commands.push_back({value, {}});
        if (ssd2677 && value == 0x40) CHECK(selectedBank == 0x10);
        if (ssd2677 && value == 0xa5) CHECK(selectedBank == 0x12);
        if (ssd2677 && value == 0x04) drivePowered = true;
        if (ssd2677 && value == 0x02) {
            CHECK(!refreshInFlight());
            drivePowered = false;
        }
        if ((ssd2677 && value == 0x12) || (!ssd2677 && value == 0x20)) {
            refreshAt = clockMs;
            refreshScheduled = true;
            if (activationHook) activationHook();
        } else if (commandBusy && ((ssd2677 && (value == 0x04 || value == 0xa5 || value == 0x02)) ||
                   (!ssd2677 && value == 0x12))) {
            busyFrom = clockMs + commandDelayMs;
            busyUntil = busyFrom + 6;
        }
        return 0;
    }
    CHECK(!commands.empty());
    return commands.back().code == 0x70 ? probeId : temperature;
}
void sticky_test_spi_data(const uint8_t* data, size_t length) {
    CHECK(transactionOpen && pins[15] == LOW && pins[16] == HIGH && !commands.empty());
    CHECK(transactions.back().settings.clock == 20000000);
    transactions.back().bytes += length;
    if (length >= 100) transactions.back().payloadBytes += length;
    commands.back().data.insert(commands.back().data.end(), data, data + length);
    if (ssd2677 && commands.back().code == 0xe0) {
        CHECK(length == 1);
        selectedBank = commands.back().data.at(0);
    }
    if (modelRam && !ssd2677) {
        const auto word = [](size_t index) {
            return unsigned(commands.back().data.at(index)) | (unsigned(commands.back().data.at(index + 1)) << 8);
        };
        switch (commands.back().code) {
            case 0x44: CHECK(length == 4); ram.xStart = word(0); ram.xEnd = word(2); break;
            case 0x45: CHECK(length == 4); ram.yStart = word(0); ram.yEnd = word(2); break;
            case 0x4e: CHECK(length == 2); ram.x = word(0); break;
            case 0x4f: CHECK(length == 2); ram.y = word(0); break;
            case 0x24:
            case 0x26: {
                auto& plane = commands.back().code == 0x24 ? ram.current : ram.previous;
                for (size_t i = 0; i < length; ++i) {
                    CHECK(ram.x < 800 && ram.y < 480 && ram.x % 8 == 0);
                    plane.at(ram.y * 100 + ram.x / 8) = data[i];
                    ram.x += 8;
                    if (ram.x > ram.xEnd) {
                        ram.x = ram.xStart;
                        if (++ram.y > ram.yEnd) ram.y = ram.yStart;
                    }
                }
                break;
            }
        }
    }
    if (simulateSpiTime) {
        clockFractionUs += uint64_t(length) * 8 * 1000000 / transactions.back().settings.clock;
        clockMs += clockFractionUs / 1000;
        clockFractionUs %= 1000;
    }
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
#if USE_SD_STORAGE
static SPIClass sharedSpi{HSPI};
SPIClass& sticky_shared_spi() { return sharedSpi; }
#endif
bool ota_activity_is_active() {
    if (otaCheckHook) otaCheckHook();
    return ota;
}
EpaperRefreshSettings config_manager_get_epaper_refresh_settings() { return settings; }

static void reset(bool controller2677 = true, bool gray = false) {
    CHECK(liveAllocations == 0 && liveMutexes == 0 && spiBuses == int(USE_SD_STORAGE) && !transactionOpen);
    commands.clear();
    transactions.clear();
    clockMs = 1000; resetAt = busyFrom = busyUntil = commandDelayMs = 0;
    clockFractionUs = 0; simulateSpiTime = false;
    refreshAt = refreshDelayMs = 0; refreshDurationMs = 6;
    refreshScheduled = false; commandBusy = true;
    ssd2677 = controller2677; probeId = controller2677 ? 7 : 1;
    disconnected = stuck = ota = failMutex = false;
    drivePowered = false;
    hardwareResets = 0;
    modelRam = false;
    ram = {};
    powerPinConfigured = false;
    pins[EPAPER_POWER_PIN] = HIGH;
    boardReady = true; allocations = failAllocation = 0;
    streamHook = waitHook = activationHook = otaCheckHook = nullptr; temperature = 25;
    selectedBank = 0;
    test_freertos_force_wait_timeout = true;
    test_freertos_semaphore_take_hook = [](SemaphoreHandle_t, TickType_t ticks) {
        if (ticks && ticks != portMAX_DELAY) {
            clockMs += ticks;
            if (waitHook) waitHook();
        }
    };
    settings = {uint8_t(gray ? EPAPER_RENDER_MODE_GRAYSCALE : EPAPER_RENDER_MODE_BW), 1000, 500, true, 2, 0, 128};
}
static void flushPixel(StickyEpaperDriver& driver, int x, int y, uint16_t color) {
    driver.flushSrcStride = 0;
    driver.setAddrWindow(x, y, 1, 1);
    driver.pushColors(&color, 1, false);
}
static void commit(StickyEpaperDriver& driver, DisplayIntent intent = DisplayIntent::Background) {
    driver.commitFrame({++sequence, sequence, intent, uint64_t(clockMs) * 1000, uint64_t(clockMs) * 1000});
}
static void pixel(StickyEpaperDriver& driver, int x, int y, uint16_t color,
                  DisplayIntent intent = DisplayIntent::Background) {
    driver.beginFrame();
    flushPixel(driver, x, y, color);
    commit(driver, intent);
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

struct RenderFixture {
    StickyEpaperDriver driver;
    DisplayFramePublisher publisher;
    unsigned flushes = 0;
    unsigned commits = 0;
    bool requestDuringStart = false;
};

static void coherentLvglFrame() {
    reset(false);
    RenderFixture fixture;
    fixture.driver.init();
    lv_init();
    lv_display_t* display = lv_display_create(800, 480);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    std::vector<uint8_t> buffer(800 * 2 * 10);
    lv_display_set_buffers(display, buffer.data(), nullptr, buffer.size(), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_user_data(display, &fixture);
    lv_timer_set_period(lv_display_get_refr_timer(display), 100);
    lv_display_add_event_cb(display, [](lv_event_t* event) {
        auto* f = static_cast<RenderFixture*>(lv_event_get_user_data(event));
        const auto code = lv_event_get_code(event);
        if (f->publisher.event(f->driver, code, uint64_t(clockMs) * 1000)) ++f->commits;
        if (code == LV_EVENT_REFR_START && f->requestDuringStart) {
            const size_t count = commands.size();
            CHECK(f->driver.requestFullRefresh());
            f->driver.present();
            CHECK(commands.size() == count);
        }
    }, LV_EVENT_ALL, &fixture);
    lv_display_set_flush_cb(display, [](lv_display_t* d, const lv_area_t* area, uint8_t* data) {
        auto* f = static_cast<RenderFixture*>(lv_display_get_user_data(d));
        const size_t count = commands.size();
        f->driver.flushSrcStride = lv_draw_buf_width_to_stride(lv_area_get_width(area), LV_COLOR_FORMAT_RGB565);
        f->driver.setAddrWindow(area->x1, area->y1, lv_area_get_width(area), lv_area_get_height(area));
        f->driver.pushColors(reinterpret_cast<uint16_t*>(data),
            lv_area_get_width(area) * lv_area_get_height(area), false);
        f->driver.present(); // Stale worker signal during every partial flush.
        CHECK(commands.size() == count);
        ++f->flushes;
        lv_display_flush_ready(d);
    });
    lv_obj_set_style_bg_color(lv_screen_active(), lv_color_black(), 0);
    const uint32_t initialRequest = fixture.publisher.interactionChanged(uint64_t(clockMs) * 1000);
    lv_refr_now(display);
    CHECK(fixture.flushes > 1);
    CHECK(!fixture.driver.hasPresentedInteraction(initialRequest));
    streamHook = [&] { CHECK(!fixture.driver.hasPresentedInteraction(initialRequest)); };
    fixture.driver.present();
    streamHook = nullptr;
    CHECK(fixture.driver.hasPresentedInteraction(initialRequest));
    CHECK(modes().size() == 1);
    CHECK(snapshotPixel(fixture.driver, 0, 0) == 0 && snapshotPixel(fixture.driver, 799, 479) == 0);

    // Same production outer-boundary helper: new content need not wait for the
    // display timer. Background work and blocked (sleep/OTA/navigation) work do.
    const unsigned initialFlushes = fixture.flushes;
    lv_obj_set_style_bg_color(lv_screen_active(), lv_color_white(), 0);
    CHECK(!fixture.publisher.renderIfNeeded(fixture.driver, display, false));
    lv_timer_handler();
    CHECK(fixture.flushes == initialFlushes);
    const uint32_t changedRequest = fixture.publisher.interactionChanged(uint64_t(clockMs) * 1000);
    CHECK(!fixture.publisher.renderIfNeeded(fixture.driver, display, true));
    CHECK(fixture.flushes == initialFlushes);
    CHECK(fixture.publisher.renderIfNeeded(fixture.driver, display, false));
    CHECK(fixture.flushes > initialFlushes);
    CHECK(!fixture.publisher.needsPromptRender());
    CHECK(!fixture.driver.hasPresentedInteraction(changedRequest));
    fixture.driver.present();
    CHECK(fixture.driver.hasPresentedInteraction(changedRequest));
    CHECK(snapshotPixel(fixture.driver, 799, 479) == 1);
    const unsigned noOpFlushes = fixture.flushes;
    const uint32_t unchangedRequest = fixture.publisher.interactionChanged(uint64_t(clockMs) * 1000);
    CHECK(fixture.publisher.renderIfNeeded(fixture.driver, display, false));
    CHECK(!fixture.publisher.needsPromptRender() && fixture.flushes == noOpFlushes);
    CHECK(fixture.driver.hasPresentedInteraction(unchangedRequest));
    // Completion must wake pending manual work even if this refresh converts no pixels.
    const unsigned commits = fixture.commits;
    fixture.requestDuringStart = true;
    lv_refr_now(display);
    fixture.requestDuringStart = false;
    CHECK(fixture.commits == commits + 1 && fixture.flushes == noOpFlushes);
    const size_t manual = modes().size();
    fixture.driver.present();
    CHECK(modes().size() == manual + 1 && modes().back() == 0xf7);
#if HAS_DISPLAY_DIAGNOSTICS
    DisplayPresentationStats stats;
    CHECK(fixture.driver.getPresentationStats(&stats));
    CHECK(stats.last.frame_sequence == fixture.commits);
#endif
    lv_obj_set_style_bg_color(lv_screen_active(), lv_color_black(), 0);
    const size_t beforeShutdownNotice = modes().size();
    const uint32_t noticeRequest = fixture.publisher.interactionChanged(uint64_t(clockMs) * 1000);
    fixture.publisher.interactionChanged(uint64_t(clockMs) * 1000, true);
    fixture.publisher.interactionChanged(uint64_t(clockMs) * 1000);
    fixture.driver.present();
    CHECK(modes().size() == beforeShutdownNotice);
    CHECK(fixture.publisher.renderIfNeeded(fixture.driver, display, false));
    CHECK(!fixture.driver.hasPresentedInteraction(noticeRequest));
    streamHook = [&] { CHECK(!fixture.driver.hasPresentedInteraction(noticeRequest)); };
    fixture.driver.present();
    streamHook = nullptr;
    CHECK(modes().size() == beforeShutdownNotice + 1 && modes().back() == 0xf7);
    CHECK(fixture.driver.hasPresentedInteraction(noticeRequest));
    CHECK(snapshotPixel(fixture.driver, 799, 479) == 0);
    // Even identical notification pixels need the explicitly requested full waveform.
    const uint32_t repeatedNotice = fixture.publisher.interactionChanged(uint64_t(clockMs) * 1000, true);
    CHECK(!sticky_display_hold_for_shutdown(repeatedNotice));
    CHECK(fixture.publisher.renderIfNeeded(fixture.driver, display, false));
    CHECK(!fixture.driver.hasPresentedInteraction(repeatedNotice));
    streamHook = [] { ota = true; };
    fixture.driver.present();
    streamHook = nullptr;
    CHECK(!sticky_display_hold_for_shutdown(repeatedNotice));
    CHECK(!fixture.driver.hasPresentedInteraction(repeatedNotice));
    ota = false;
    fixture.driver.present();
    CHECK(modes().size() == beforeShutdownNotice + 2 && modes().back() == 0xf7);
    CHECK(fixture.driver.hasPresentedInteraction(repeatedNotice));
    CHECK(sticky_display_hold_for_shutdown(repeatedNotice));
    // Live data may still render, but must not partially update the retained glass.
    lv_obj_set_style_bg_color(lv_screen_active(), lv_color_white(), 0);
    const uint32_t laterRequest = fixture.publisher.interactionChanged(uint64_t(clockMs) * 1000);
    CHECK(fixture.publisher.renderIfNeeded(fixture.driver, display, false));
    clockMs += 1000;
    fixture.driver.present();
    CHECK(modes().size() == beforeShutdownNotice + 2);
    CHECK(!fixture.driver.hasPresentedInteraction(laterRequest));
    CHECK(snapshotPixel(fixture.driver, 799, 479) == 0);
    sticky_display_release_shutdown();
    fixture.driver.present();
    CHECK(modes().size() == beforeShutdownNotice + 3);
    CHECK(fixture.driver.hasPresentedInteraction(laterRequest));
    CHECK(snapshotPixel(fixture.driver, 799, 479) == 1);
    lv_obj_set_style_bg_color(lv_screen_active(), lv_color_white(), 0);
    // Local binding requests coalesce, survive blocked owner passes, and only
    // become render intent after resolution at the safe outer boundary.
    const uint64_t bindingRequest = uint64_t(clockMs) * 1000;
    fixture.publisher.requestBindingRefresh(bindingRequest);
    fixture.publisher.requestBindingRefresh(bindingRequest + 100);
    CHECK(!fixture.publisher.needsPromptRender());
    uint64_t requested = 0;
    CHECK(!fixture.publisher.takeBindingRefresh(true, requested));
    CHECK(fixture.publisher.takeBindingRefresh(false, requested) && requested == bindingRequest);
    CHECK(!fixture.publisher.takeBindingRefresh(false, requested));
    lv_obj_t* counter = lv_label_create(lv_screen_active());
    lv_label_set_text(counter, "1");
    fixture.publisher.interactionChanged(requested);
    CHECK(fixture.publisher.renderIfNeeded(fixture.driver, display, false));
    uint32_t uploadAt = 0;
    streamHook = [&] { if (!uploadAt) uploadAt = clockMs; };
    const uint32_t beforeLocalPresent = clockMs;
    fixture.driver.present();
    CHECK(uploadAt && uploadAt - beforeLocalPresent < EPAPER_REFRESH_SETTLE_MS);
    streamHook = nullptr;

    fixture.publisher.requestBindingRefresh(uint64_t(clockMs) * 1000);
    CHECK(fixture.publisher.takeBindingRefresh(false, requested));
    lv_label_set_text(counter, "1");
    fixture.publisher.interactionChanged(requested);
    const size_t beforeNoChange = modes().size();
    CHECK(fixture.publisher.renderIfNeeded(fixture.driver, display, false));
    fixture.driver.present();
    CHECK(modes().size() == beforeNoChange && !fixture.publisher.needsPromptRender());

    // Idle/background invalidations still follow the unchanged display timer.
    const unsigned idleFlushes = fixture.flushes;
    lv_timer_reset(lv_display_get_refr_timer(display));
    lv_obj_set_style_bg_color(lv_screen_active(), lv_color_black(), 0);
    lv_tick_inc(99);
    lv_timer_handler();
    CHECK(fixture.flushes == idleFlushes);
    lv_tick_inc(1);
    lv_timer_handler();
    CHECK(fixture.flushes > idleFlushes && !fixture.publisher.needsPromptRender());
    lv_display_delete(display);
    lv_deinit();
}
static std::vector<uint8_t> referenceWire(const std::vector<uint8_t>& current,
        const std::vector<uint8_t>& previous, bool gray, bool full, bool highPlane = false) {
    const int bits = ssd2677 ? 2 : 1, pixelsPerByte = 8 / bits;
    std::vector<uint8_t> result(480 * 800 / pixelsPerByte);
    auto level = [gray](const std::vector<uint8_t>& source, int x, int y) {
        const uint8_t packed = source[size_t(y) * (gray ? 400 : 100) + x / (gray ? 2 : 8)];
        return gray ? (x & 1 ? packed & 15 : packed >> 4) / 5 :
            (packed & (0x80 >> (x & 7)) ? 3 : 0);
    };
    for (int y = 0; y < 480; ++y) {
        for (int outputX = 0; outputX < 800; ++outputX) {
            const int x = gray ? outputX : 799 - outputX;
            const uint8_t value = level(current, x, y);
            const uint8_t encoded = ssd2677 ?
                (!gray && !full ? (level(previous, x, y) ? 2 : 0) | (value ? 1 : 0) : value) :
                (gray ? (highPlane ? !(value & 1) : !(value & 2)) : bool(value));
            const size_t index = size_t(y) * (800 / pixelsPerByte) + outputX / pixelsPerByte;
            result[index] |= encoded << ((pixelsPerByte - 1 - outputX % pixelsPerByte) * bits);
        }
    }
    return result;
}

static void conversionAndWireEquivalence() {
    for (bool controller2677 : {false, true}) {
        for (bool gray : {false, true}) {
            for (uint8_t mode = 0; mode < (gray ? 1 : 3); ++mode) {
                for (uint8_t rotation = 0; rotation < 4; ++rotation) {
                    reset(controller2677, gray);
                    settings.epaper_bw_conversion = mode;
                    settings.epaper_bw_threshold = rotation == 0 ? 0 : rotation == 3 ? 255 : 128;
                    settings.epaper_full_refresh_threshold = 200;
                    StickyEpaperDriver driver;
                    driver.init();
                    driver.setRotation(rotation);
                    const int width = driver.width(), height = driver.height();
                    const int depth = gray ? 4 : 1;
                    std::vector<uint8_t> native(gray ? 192000 : 48000, 0xff);
                    std::vector<uint8_t> logical(native.size(), 0xff);
                    for (int generation = 0; generation < 2; ++generation) {
                        const auto previous = native;
                        driver.beginFrame();
                        auto patch = [&](int x, int y, int w, int h, bool swap) {
                            const int stride = w + 3;
                            std::vector<uint16_t> source(stride * h, 0xdead);
                            for (int row = 0; row < h; ++row) {
                                for (int column = 0; column < w; ++column) {
                                    // Odd multiplication visits every RGB565 value in the large patch.
                                    const uint16_t color = uint16_t((row * w + column) * 7919 + generation * 1009);
                                    source[row * stride + column] = swap ? uint16_t((color << 8) | (color >> 8)) : color;
                                    const int lx = x + column, ly = y + row;
                                    if (lx < 0 || ly < 0 || lx >= width || ly >= height) continue;
                                    const uint32_t red = (color >> 11) * 255 / 31;
                                    const uint32_t green = ((color >> 5) & 63) * 255 / 63;
                                    const uint32_t blue = (color & 31) * 255 / 31;
                                    const uint8_t luminance = (red * 77 + green * 150 + blue * 29) >> 8;
                                    const uint8_t value = gray ? ((uint16_t(luminance) * 3 + 127) / 255) * 5 :
                                        monochrome_is_white(luminance, lx, ly, mode, settings.epaper_bw_threshold);
                                    uint16_t nx, ny;
                                    presented_frame_native_coords(800, 480, rotation, lx, ly, &nx, &ny);
                                    presented_frame_write_gray(native.data(), gray ? 400 : 100, nx, ny, depth, value);
                                    presented_frame_write_gray(logical.data(),
                                        presented_frame_row_bytes(width, depth), lx, ly, depth, value);
                                }
                            }
                            driver.flushSrcStride = stride * sizeof(uint16_t);
                            driver.setAddrWindow(x, y, w, h);
                            driver.pushColors(source.data(), w * h, swap);
                        };
                        patch(3, 5, 257, 256, false);
                        patch(-3, -2, 19, 7, true);
                        patch(width - 5, height - 3, 13, 9, true);
                        commit(driver, DisplayIntent::Interactive);
                        commands.clear();
                        driver.present();
                        std::vector<uint8_t> screenshot(logical.size());
                        CHECK(driver.copyPresentedFrame(screenshot.data(), screenshot.size()));
                        CHECK(screenshot == logical);
                        if (generation && native == previous) {
                            CHECK(commands.empty());
                            continue;
                        }
                        const bool full = !generation || gray;
                        CHECK(frame(controller2677 ? 0x10 : 0x24) == referenceWire(native, previous, gray, full));
                        if (!controller2677)
                            CHECK(frame(0x26) == referenceWire(gray ? native : previous, previous, gray, full, true));
                    }
                }
            }
        }
    }
}

static void benchmarkConversion() {
    for (uint8_t rotation : {0, 1}) {
        reset(false);
        StickyEpaperDriver driver;
        driver.init();
        driver.setRotation(rotation);
        const int width = driver.width(), height = driver.height();
        std::vector<uint16_t> strip(width * 10);
        std::vector<double> samples;
        for (int sample = 0; sample < 31; ++sample) {
            for (size_t i = 0; i < strip.size(); ++i)
                strip[i] = uint16_t(i * 7919 + sample * 1009);
            const auto start = std::chrono::steady_clock::now();
            driver.beginFrame();
            for (int y = 0; y < height; y += 10) {
                driver.setAddrWindow(0, y, width, 10);
                driver.pushColors(strip.data(), strip.size(), false);
            }
            commit(driver, DisplayIntent::Interactive);
            const double elapsed = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - start).count();
            if (sample) samples.push_back(elapsed);
        }
        std::sort(samples.begin(), samples.end());
        std::printf("HOST conversion rotation=%u n=%zu median=%.3fms p95=%.3fms max=%.3fms\n",
            rotation, samples.size(), (samples[14] + samples[15]) / 2, samples[28], samples.back());
    }
}

static void benchmarkPanelPacking() {
    for (bool controller2677 : {false, true}) {
        for (bool gray : {false, true}) {
            reset(controller2677, gray);
            sticky_epaper::Panel panel;
            CHECK(panel.begin());
            std::vector<uint8_t> current(gray ? 192000 : 48000), previous(current.size());
            for (size_t i = 0; i < current.size(); ++i) {
                current[i] = gray ? uint8_t(((i & 3) * 5 << 4) | (((i + 1) & 3) * 5)) : uint8_t(i * 73);
                previous[i] = gray ? 0xff : uint8_t(~current[i]);
            }
            std::vector<double> samples;
            for (int sample = 0; sample < 31; ++sample) {
                commands.clear();
                const auto start = std::chrono::steady_clock::now();
                CHECK(panel.refresh(current.data(), previous.data(), gray, gray) == sticky_epaper::Result::Success);
                const double elapsed = std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - start).count();
                if (sample) samples.push_back(elapsed);
            }
            std::sort(samples.begin(), samples.end());
            std::printf("HOST mock-panel SSD%d gray=%u n=%zu median=%.3fms p95=%.3fms max=%.3fms\n",
                controller2677 ? 2677 : 1677, gray, samples.size(),
                (samples[14] + samples[15]) / 2, samples[28], samples.back());
            panel.end();
        }
    }
}

static bool ramWrite(uint8_t code) {
    return code == (ssd2677 ? 0x10 : 0x26) || (!ssd2677 && code == 0x24);
}

static void batchedStreaming() {
    for (bool controller2677 : {false, true}) {
        for (bool gray : {false, true}) {
            reset(controller2677, gray);
            {
                sticky_epaper::Panel panel;
                CHECK(panel.begin());
                std::vector<uint8_t> pixels(gray ? 192000 : 48000, 0xff);
                simulateSpiTime = true;
                uint64_t lastYield = 0, longestWork = 0;
                unsigned yields = 0;
                streamHook = [&] {
                    if (!lastYield) lastYield = esp_timer_get_time() - 320;
                };
                waitHook = [&] {
                    if (commands.empty() || !ramWrite(commands.back().code)) return;
                    const uint64_t now = esp_timer_get_time();
                    longestWork = std::max(longestWork, now - 1000 - lastYield);
                    lastYield = now;
                    ++yields;
                };
                CHECK(panel.refresh(pixels.data(), pixels.data(), gray, true) == sticky_epaper::Result::Success);
                size_t payloadTransactions = 0, payloadBytes = 0, ramCommands = 0;
                for (const auto& transaction : transactions) {
                    CHECK(transaction.ended);
                    if (!transaction.payloadBytes) continue;
                    CHECK(transaction.payloadBytes == 800 && transaction.bytes == 800u + USE_SD_STORAGE);
                    ++payloadTransactions;
                    payloadBytes += transaction.payloadBytes;
                }
                for (const auto& command : commands) if (ramWrite(command.code)) ++ramCommands;
                CHECK(payloadTransactions == 120 && payloadBytes == 96000);
                CHECK(ramCommands == (controller2677 ? 1u : 2u));
                CHECK(yields >= 8 && yields <= 10 && longestWork <= 4320);
#if HAS_DISPLAY_DIAGNOSTICS
                CHECK(panel.timing.uploaded_bytes == 96000 && panel.timing.packed_bytes == 96000);
                CHECK(panel.timing.spi_us == 38400 && panel.timing.yield_us == yields * 1000);
#endif
                streamHook = waitHook = nullptr;
                panel.end();
            }
            const unsigned rowBytes = controller2677 ? 200 : 100;
            for (unsigned cancelAt : {3u, 800 / rowBytes + 1}) {
                reset(controller2677, gray);
                {
                    StickyEpaperDriver driver;
                    driver.init();
                    unsigned checks = 0;
                    otaCheckHook = [&] {
                        if (!commands.empty() && ramWrite(commands.back().code) &&
                            commands.back().data.empty() && ++checks == cancelAt) ota = true;
                    };
                    pixel(driver, 0, 0, 0, DisplayIntent::Interactive);
                    driver.present();
                    CHECK(ota && frame(controller2677 ? 0x10 : gray ? 0x24 : 0x26).empty());
                    CHECK(!transactionOpen && pins[15] == HIGH && !refreshScheduled);
                    DisplayDriver::PresentedFrameInfo info;
                    CHECK(!driver.getPresentedFrameInfo(info));
                    otaCheckHook = nullptr;
                    ota = false;
                    driver.present();
                    CHECK(snapshotPixel(driver, 0, 0) == 0);
                    CHECK(modes().back() == (controller2677 ? (gray ? 0xfa : 0xee) : (gray ? 0xd7 : 0xf7)));
                }
            }
        }
        reset(controller2677);
        {
            StickyEpaperDriver driver;
            driver.init();
            simulateSpiTime = true;
            bool slept = false;
            waitHook = [&] {
                if (commands.empty() || !ramWrite(commands.back().code) || commands.back().data.empty()) return;
                slept = ota = true;
            };
            pixel(driver, 0, 0, 0, DisplayIntent::Interactive);
            driver.present();
            const size_t sent = frame(controller2677 ? 0x10 : 0x26).size();
            CHECK(slept && sent > 800 && sent < 48000 && sent % 800 == 0);
            CHECK(!transactionOpen && pins[15] == HIGH && !refreshScheduled);
            DisplayDriver::PresentedFrameInfo info;
            CHECK(!driver.getPresentedFrameInfo(info));
#if HAS_DISPLAY_DIAGNOSTICS
            DisplayPresentationStats stats;
            CHECK(driver.getPresentationStats(&stats));
            CHECK(stats.last.uploaded_bytes == sent && stats.last.upload_us > 0);
#endif
            waitHook = nullptr;
            ota = false;
            driver.present();
            CHECK(snapshotPixel(driver, 0, 0) == 0 && modes().back() == (controller2677 ? 0xee : 0xf7));
        }
    }
}

static void preparationCaching() {
    reset(false);
    {
        sticky_epaper::Panel panel;
        CHECK(panel.begin());
        modelRam = true;
        std::vector<uint8_t> current(48000), previous(48000), gray(192000);
        for (size_t i = 0; i < current.size(); ++i) {
            current[i] = uint8_t(i * 73);
            previous[i] = uint8_t(~current[i]);
        }
        for (size_t i = 0; i < gray.size(); ++i)
            gray[i] = uint8_t(((i & 3) * 5 << 4) | (((i + 1) & 3) * 5));
        auto present = [&](bool grayscale, bool full, bool reused) {
            commands.clear();
            ram.current.assign(48000, 0x42);
            ram.previous.assign(48000, 0x42);
            const unsigned resets = hardwareResets;
            // Do not assume full-frame counter wrap or a retained full window.
            streamHook = [&] {
                if (commands.back().data.size() != 48000) return;
                ram.xStart = 16; ram.xEnd = 791; ram.yStart = 3; ram.yEnd = 478;
                ram.x = 32; ram.y = 17;
            };
            const auto& source = grayscale ? gray : current;
            CHECK(panel.refresh(source.data(), previous.data(), grayscale, full) == sticky_epaper::Result::Success);
            CHECK(hardwareResets == resets + (reused ? 0 : 1));
            CHECK(std::none_of(commands.begin(), commands.end(), [reused](const Command& command) {
                return reused && (command.code == 0x12 || command.code == 0x0c ||
                    command.code == 0x01 || command.code == 0x11 || command.code == 0x18);
            }));
            CHECK(ram.current == referenceWire(source, previous, grayscale, full));
            CHECK(ram.previous == referenceWire(grayscale ? gray : previous, previous, grayscale, full, true));
            CHECK(frame(0x3c) == std::vector<uint8_t>{uint8_t(grayscale ? 0 : full ? 5 : 0x80)});
            CHECK(modes().back() == (grayscale ? 0xd7 : full ? 0xf7 : 0xff));
            if (grayscale) CHECK(frame(0x1a) == (std::vector<uint8_t>{0x67, 0}));
            else CHECK(std::none_of(commands.begin(), commands.end(), [](const Command& c) { return c.code == 0x1a; }));
            streamHook = nullptr;
        };
        present(false, true, false);
        present(false, false, false);
        present(false, false, true);
        present(false, true, false);
        present(false, false, false);
        present(false, false, true);
        present(true, true, false);
        present(false, false, false);
        present(false, false, true);
        std::atomic<bool> stopped{false};
        panel.setStopFlag(&stopped);
        for (unsigned failure = 0; failure < 5; ++failure) {
            if (failure == 0) ota = true;
            if (failure == 1) streamHook = [] { ota = true; };
            if (failure == 2) activationHook = [&] { stopped.store(true); };
            if (failure == 3) refreshDurationMs = 0;
            if (failure == 4) stuck = true;
            const size_t count = commands.size();
            CHECK(panel.refresh(current.data(), previous.data(), false, false) ==
                (failure >= 3 ? sticky_epaper::Result::Error : sticky_epaper::Result::Deferred));
            CHECK(!transactionOpen && pins[15] == HIGH && !refreshInFlight());
            if (failure == 4) CHECK(commands.size() == count);
            ota = stuck = false;
            stopped.store(false);
            streamHook = activationHook = nullptr;
            refreshDurationMs = 6;
            present(false, false, false);
            present(false, false, true);
        }
        panel.end();
        ssd2677 = true; probeId = 7;
        CHECK(panel.begin());
        for (unsigned update = 0; update < 2; ++update) {
            const unsigned resets = hardwareResets;
            CHECK(panel.refresh(current.data(), previous.data(), false, false) == sticky_epaper::Result::Success);
            CHECK(hardwareResets == resets + (update ? 0 : 1) && modes().back() == 0x19);
        }
        panel.end();
        ssd2677 = false; probeId = 1;
        CHECK(panel.begin());
        present(false, false, false);
        present(false, false, true);
        panel.end();
    }
    reset(false);
    {
        settings.epaper_full_refresh_threshold = 100;
        StickyEpaperDriver driver;
        driver.init();
        auto update = [&](int x, uint8_t mode, bool reused) {
            const unsigned resets = hardwareResets;
            pixel(driver, x, 0, 0, DisplayIntent::Interactive);
            driver.present();
            CHECK(hardwareResets == resets + (reused ? 0 : 1));
            CHECK(snapshotPixel(driver, x, 0) == 0 && modes().back() == mode);
        };
        update(0, 0xf7, false);
        update(1, 0xff, false);
        update(2, 0xff, true);
        streamHook = [] { ota = true; };
        pixel(driver, 3, 0, 0, DisplayIntent::Interactive);
        const uint32_t interruptedRequest = sequence;
        driver.present();
        CHECK(!driver.hasPresentedInteraction(interruptedRequest));
        DisplayDriver::PresentedFrameInfo info;
        CHECK(!driver.getPresentedFrameInfo(info) && !refreshInFlight());
        streamHook = nullptr;
        ota = false;
        driver.present();
        CHECK(snapshotPixel(driver, 3, 0) == 0 && modes().back() == 0xf7);
        CHECK(driver.hasPresentedInteraction(interruptedRequest));
        update(4, 0xff, false);
        update(5, 0xff, true);
        CHECK(driver.requestFullRefresh());
        driver.present();
        CHECK(modes().back() == 0xf7);
        update(6, 0xff, false);
        update(7, 0xff, true);
        settings.epaper_full_refresh_threshold = 1;
        update(8, 0xf7, false);
        update(9, 0xff, false);
        update(10, 0xf7, false);
    }
}

static void waveformPreparationCaching() {
    reset();
    {
        sticky_epaper::Panel panel;
        CHECK(panel.begin());
        std::vector<uint8_t> current(48000), previous(48000), gray(192000);
        for (size_t i = 0; i < current.size(); ++i) {
            current[i] = uint8_t(i * 73);
            previous[i] = uint8_t(~current[i]);
        }
        for (size_t i = 0; i < gray.size(); ++i)
            gray[i] = uint8_t(((i & 3) * 5 << 4) | (((i + 1) & 3) * 5));
        auto present = [&](uint8_t temp, bool grayscale, bool full, uint8_t waveform, bool reused) {
            temperature = temp;
            commands.clear();
            const unsigned resets = hardwareResets;
            const uint32_t start = clockMs;
            const size_t firstTransaction = transactions.size();
            uint32_t latchReadyAt = 0;
            bool checkedSettle = false;
            waitHook = [&] {
                if (!commands.empty() && commands.back().code == 0xa5) latchReadyAt = busyUntil;
            };
            streamHook = [&] {
                if (!checkedSettle) {
                    CHECK(clockMs - latchReadyAt >= 10);
                    checkedSettle = true;
                }
            };
            const auto& source = grayscale ? gray : current;
            CHECK(panel.refresh(source.data(), previous.data(), grayscale, full) == sticky_epaper::Result::Success);
            waitHook = streamHook = nullptr;
            CHECK(checkedSettle);
            CHECK(std::count_if(transactions.begin() + firstTransaction, transactions.end(),
                [](const SpiTransaction& transaction) { return transaction.settings.clock == 4000000; }) == 1);
            CHECK(hardwareResets == resets + (reused ? 0 : 1));
            CHECK(modes() == std::vector<uint8_t>{waveform});
            CHECK(frame(0x10) == referenceWire(source, previous, grayscale, full));
            for (uint8_t code : {0x40, 0xa5, 0x04, 0x12, 0x02})
                CHECK(std::count_if(commands.begin(), commands.end(),
                    [code](const Command& command) { return command.code == code; }) == 1);
            if (reused) {
                CHECK(std::none_of(commands.begin(), commands.end(), [](const Command& command) {
                    return command.code == 0x00 || command.code == 0x06 || command.code == 0x61;
                }));
            } else CHECK(frame(0x61) == (std::vector<uint8_t>{3, 0x20, 2, 0xa8}));
            CHECK(selectedBank == 0x12 && !drivePowered && pins[EPAPER_POWER_PIN] == HIGH);
            return clockMs - start;
        };
        const uint32_t cold = present(1, false, true, 0xe8, false);
        const uint32_t cached = present(10, false, true, 0xe8, true);
        CHECK(cold - cached >= 70 && cold - cached <= 100);
        struct Case { uint8_t temp; bool gray, full; uint8_t waveform; bool reused; };
        const Case cases[] = {
            {11, false, true, 0xeb, false}, {20, false, true, 0xeb, true},
            {21, false, true, 0xee, false}, {30, false, true, 0xee, true},
            {31, false, true, 0xf1, false}, {127, false, true, 0xf1, true},
            {31, false, false, 0x23, false}, {127, false, false, 0x23, true},
            {30, false, false, 0x19, false}, {21, false, false, 0x19, true},
            {20, false, false, 0x0f, false}, {11, false, false, 0x0f, true},
            {10, false, false, 0x08, false}, {1, false, false, 0x08, true},
            {15, true, true, 0xf1, false}, {15, true, true, 0xf1, true},
            {16, true, true, 0xfa, false}, {127, true, true, 0xfa, true},
            {31, false, true, 0xf1, false}, {15, true, true, 0xf1, false},
            {0, true, true, 0xfa, false}, {128, true, true, 0xfa, true},
            {255, true, true, 0xfa, true}, {25, true, true, 0xfa, true},
            {25, false, true, 0xee, false}, {25, false, false, 0x19, false},
            {0, false, false, 0x19, true}, {128, false, false, 0x19, true},
            {255, false, false, 0x19, true},
            {10, false, false, 0x08, false}, {0, false, false, 0x19, false}
        };
        for (const auto& c : cases) present(c.temp, c.gray, c.full, c.waveform, c.reused);
        for (uint8_t cancelAt : {0x00, 0x40, 0xa5, 0x10, 0x12, 0x02}) {
            commands.clear();
            temperature = cancelAt == 0x00 ? 10 : 25;
            bool cancelledOnce = false;
            waitHook = [cancelAt, &cancelledOnce] {
                if (cancelAt == 0x02 && cancelledOnce) ota = false;
                else if (!commands.empty() && commands.back().code == cancelAt) {
                    ota = true;
                    cancelledOnce = true;
                }
            };
            if (cancelAt == 0x10) streamHook = [] { ota = true; };
            if (cancelAt == 0x12) activationHook = [] { ota = true; };
            CHECK(panel.refresh(current.data(), previous.data(), false, false) == sticky_epaper::Result::Deferred);
            CHECK(!drivePowered && !refreshInFlight() && !transactionOpen && pins[15] == HIGH);
            waitHook = streamHook = activationHook = nullptr;
            ota = false;
            present(25, false, false, 0x19, false);
            present(25, false, false, 0x19, true);
        }
        for (uint8_t failAt : {0x00, 0x40, 0xa5, 0x04, 0x02}) {
            commands.clear();
            temperature = failAt == 0x00 ? 10 : 25;
            waitHook = [failAt] {
                if (!commands.empty() && commands.back().code == failAt) stuck = true;
            };
            CHECK(panel.refresh(current.data(), previous.data(), false, false) == sticky_epaper::Result::Error);
            CHECK(!drivePowered && !transactionOpen && pins[15] == HIGH);
            if (failAt == 0x04 || failAt == 0x02) CHECK(pins[EPAPER_POWER_PIN] == LOW);
            waitHook = nullptr;
            stuck = false;
            present(25, false, false, 0x19, false);
            present(25, false, false, 0x19, true);
        }
        panel.end();
        CHECK(panel.begin());
        present(25, false, false, 0x19, false);
        present(25, false, false, 0x19, true);
        panel.end();
    }
}

static void spiClockSettings() {
    for (bool controller2677 : {false, true}) {
        for (bool gray : {false, true}) {
            reset(controller2677, gray);
            sticky_epaper::Panel panel;
            CHECK(panel.begin());
            std::vector<uint8_t> pixels(gray ? 192000 : 48000, 0xff);
            CHECK(panel.refresh(pixels.data(), pixels.data(), gray, true) == sticky_epaper::Result::Success);
            CHECK(panel.refresh(pixels.data(), pixels.data(), gray, gray) == sticky_epaper::Result::Success);
            size_t reads = 0, writes = 0;
            for (const auto& transaction : transactions) {
                CHECK(transaction.ended && transaction.bytes > 0);
                if (transaction.settings.clock == 4000000) {
                    CHECK(transaction.bytes == (USE_SD_STORAGE ? 2u : 1u));
                    ++reads;
                } else {
                    CHECK(transaction.settings.clock == 20000000);
                    ++writes;
                }
            }
            CHECK(reads == (controller2677 ? 3u : 1u) && writes > 0);
            panel.end();
        }
    }
}

static void refreshCompletion() {
    for (bool controller2677 : {false, true}) {
        for (uint32_t assertionDelay : {0u, 7u, 19u}) {
            reset(controller2677);
            {
                StickyEpaperDriver driver;
                driver.init();
                commandDelayMs = 1;
                refreshDelayMs = assertionDelay;
                refreshDurationMs = 40;
                bool checkedInFlight = false;
                waitHook = [&] {
                    if (!refreshInFlight()) return;
                    checkedInFlight = true;
                    DisplayDriver::PresentedFrameInfo info;
                    CHECK(!driver.getPresentedFrameInfo(info));
                    if (controller2677) CHECK(drivePowered);
                };
                pixel(driver, 0, 0, 0, DisplayIntent::Interactive);
                driver.present();
                CHECK(checkedInFlight && clockMs - refreshAt >= assertionDelay + 40);
                CHECK(clockMs - refreshAt < assertionDelay + 50);
                CHECK(snapshotPixel(driver, 0, 0) == 0);
                CHECK(!drivePowered);
                waitHook = nullptr;
            }
        }
        for (bool stop : {false, true}) {
            reset(controller2677);
            {
                StickyEpaperDriver driver;
                driver.init();
                refreshDelayMs = 7;
                refreshDurationMs = 40;
                activationHook = [&] {
                    CHECK(sticky_display_is_busy());
                    if (stop) driver.stopPresentation();
                    else ota = true;
                };
                pixel(driver, 0, 0, 0, DisplayIntent::Interactive);
                driver.present();
                CHECK(!sticky_display_is_busy());
                CHECK(clockMs - refreshAt >= 47);
                DisplayDriver::PresentedFrameInfo info;
                CHECK(!driver.getPresentedFrameInfo(info) && !drivePowered && pins[15] == HIGH);
                activationHook = nullptr;
                ota = false;
                if (stop) {
                    driver.init();
                    pixel(driver, 0, 0, 0, DisplayIntent::Interactive);
                }
                driver.present();
                CHECK(driver.getPresentedFrameInfo(info));
                CHECK(modes().back() == (controller2677 ? 0xee : 0xf7));
            }
        }
        for (uint32_t duration : {0u, 20000u}) {
            reset(controller2677);
            {
                StickyEpaperDriver driver;
                driver.init();
                refreshDelayMs = 7;
                refreshDurationMs = duration;
                pixel(driver, 0, 0, 0, DisplayIntent::Interactive);
                driver.present();
                const uint32_t elapsed = clockMs - refreshAt;
                CHECK(duration ? elapsed >= 15000 && elapsed < 15120 : elapsed >= 20 && elapsed < 140);
                DisplayDriver::PresentedFrameInfo info;
                CHECK(!driver.getPresentedFrameInfo(info));
                CHECK(!controller2677 || (!drivePowered && pins[EPAPER_POWER_PIN] == LOW));
                if (!controller2677 && duration) {
                    const size_t count = commands.size();
                    stuck = true;
                    driver.present();
                    CHECK(commands.size() == count && !driver.getPresentedFrameInfo(info));
                    stuck = false;
                }
                refreshDurationMs = 6;
                driver.present();
                CHECK(snapshotPixel(driver, 0, 0) == 0);
                CHECK(modes().back() == (controller2677 ? 0xee : 0xf7));
            }
        }
        reset(controller2677);
        {
            StickyEpaperDriver driver;
            driver.init();
            refreshDelayMs = 7;
            refreshDurationMs = 40;
            bool updated = false;
            waitHook = [&] {
                if (updated || !refreshInFlight()) return;
                updated = true;
                pixel(driver, 1, 0, 0, DisplayIntent::Interactive);
            };
            pixel(driver, 0, 0, 0, DisplayIntent::Interactive);
            driver.present();
            CHECK(updated && modes().size() == 2);
            CHECK(snapshotPixel(driver, 0, 0) == 0 && snapshotPixel(driver, 1, 0) == 0);
            waitHook = nullptr;
        }
        reset(controller2677);
        {
            sticky_epaper::Panel panel;
            CHECK(panel.begin());
            commandBusy = false; // Command-ready waits must not demand an assertion.
            std::vector<uint8_t> pixels(48000, 0xff);
            clockMs = UINT32_MAX - 180;
            busyUntil = clockMs;
            resetAt = clockMs - 10;
            refreshDelayMs = 7;
            refreshDurationMs = 200;
            bool cancelledOnce = false;
            waitHook = [&] {
                if (!refreshInFlight()) return;
                ota = !cancelledOnce;
                cancelledOnce = true;
            };
            CHECK(panel.refresh(pixels.data(), pixels.data(), false, true) == sticky_epaper::Result::Deferred);
            CHECK(cancelledOnce && clockMs < 1000 && clockMs - refreshAt >= 207);
            waitHook = nullptr;
            ota = false;
            panel.end();
        }
    }
    reset();
    {
        StickyEpaperDriver driver;
        driver.init();
        bool cancelledOnce = false;
        waitHook = [&] {
            if (commands.empty() || commands.back().code != 0x02) return;
            ota = !cancelledOnce;
            cancelledOnce = true;
        };
        pixel(driver, 0, 0, 0, DisplayIntent::Interactive);
        driver.present();
        DisplayDriver::PresentedFrameInfo info;
        CHECK(cancelledOnce && !ota && !drivePowered && !driver.getPresentedFrameInfo(info));
        waitHook = nullptr;
        driver.present();
        CHECK(snapshotPixel(driver, 0, 0) == 0 && modes().back() == 0xee);
    }
}

int main(int argc, char** argv) {
#if USE_SD_STORAGE
    CHECK(sharedSpi.begin(13, 12, 14, 15));
#endif
    if (argc == 2 && !strcmp(argv[1], "--benchmark")) {
        benchmarkConversion();
        benchmarkPanelPacking();
        return failures ? 1 : 0;
    }
    coherentLvglFrame();
    conversionAndWireEquivalence();
    batchedStreaming();
    preparationCaching();
    waveformPreparationCaching();
    spiClockSettings();
    refreshCompletion();
    for (bool controller2677 : {false, true}) {
        reset(controller2677);
        {
            StickyEpaperDriver driver;
            driver.init();
            pixel(driver, 0, 0, 0);
            driver.present();
            const uint32_t completed = clockMs;
            pixel(driver, 1, 0, 0);
            uint32_t firstCommand = 0;
            streamHook = [&] { if (!firstCommand) firstCommand = clockMs; };
            driver.present();
            CHECK(firstCommand - completed >= 500);
            streamHook = nullptr;

            pixel(driver, 2, 0, 0);
            uint32_t interrupted = 0, upload = 0;
            waitHook = [&] {
                if (interrupted) return;
                interrupted = clockMs;
                pixel(driver, 3, 0, 0, DisplayIntent::Interactive);
            };
            streamHook = [&] { if (!upload) upload = clockMs; };
            driver.present();
            CHECK(interrupted && upload - interrupted < 150);
            CHECK(snapshotPixel(driver, 2, 0) == 0 && snapshotPixel(driver, 3, 0) == 0);
            waitHook = streamHook = nullptr;

            // A stale worker wake between flushes must not expose a partial frame.
            const size_t count = commands.size();
            driver.beginFrame();
            flushPixel(driver, 4, 0, 0);
            driver.present();
            CHECK(commands.size() == count);
            flushPixel(driver, 5, 0, 0);
            commit(driver, DisplayIntent::Interactive);
            driver.present();
            CHECK(snapshotPixel(driver, 4, 0) == 0 && snapshotPixel(driver, 5, 0) == 0);
#if HAS_DISPLAY_DIAGNOSTICS
            DisplayPresentationStats stats;
            CHECK(driver.getPresentationStats(&stats));
            CHECK(stats.last.frame_sequence == sequence && stats.last.request_sequence == sequence);
            CHECK(stats.last.coalesce_us == 0);
            CHECK(stats.last.uploaded_bytes == 96000);
            CHECK(stats.last.ready_us == stats.last.snapshot_us);
            CHECK(stats.last.trigger_us > stats.last.snapshot_us);
            CHECK(stats.last.waveform_done_us >= stats.last.trigger_us);
            CHECK(stats.last.published_us >= stats.last.waveform_done_us);
#endif

            // No-op interaction and reverted pixels cannot make later bindings urgent.
            pixel(driver, 5, 0, 0, DisplayIntent::Interactive);
            driver.present();
            pixel(driver, 6, 0, 0, DisplayIntent::Interactive);
            pixel(driver, 6, 0, 0xffff, DisplayIntent::Interactive);
            const size_t unchanged = commands.size();
            driver.present();
            CHECK(commands.size() == unchanged);
            const uint32_t beforeBackground = clockMs;
            pixel(driver, 7, 0, 0);
            upload = 0;
            streamHook = [&] { if (!upload) upload = clockMs; };
            driver.present();
            CHECK(upload - beforeBackground >= EPAPER_REFRESH_SETTLE_MS);
            streamHook = nullptr;

            // New frames and manual requests survive completion of older hardware work.
            pixel(driver, 8, 0, 0, DisplayIntent::Interactive);
            bool added = false;
            streamHook = [&] {
                if (added) return;
                added = true;
                pixel(driver, 9, 0, 0, DisplayIntent::Interactive);
                CHECK(driver.requestFullRefresh());
            };
            const size_t presentations = modes().size();
            driver.present();
            streamHook = nullptr;
            CHECK(modes().size() == presentations + 2);
            CHECK(snapshotPixel(driver, 9, 0) == 0);
            CHECK(modes().back() == (controller2677 ? 0xee : 0xf7));

            pixel(driver, 10, 0, 0);
            const size_t beforeStop = commands.size();
            waitHook = [&] { driver.stopPresentation(); };
            driver.present();
            CHECK(commands.size() == beforeStop);
            waitHook = nullptr;
        }
    }
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
                CHECK(frame(controller2677 ? 0x10 : gray ? 0x24 : 0x26).size() == 800);
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
            CHECK(!driver.getPresentedFrameInfo(info));
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
        settings.epaper_min_refresh_interval_ms = 0;
        {
            StickyEpaperDriver driver;
            driver.init();
            pixel(driver, 0, 0, 0);
            driver.present();
            pixel(driver, 1, 0, 0);
            driver.present();
            pixel(driver, 2, 0, 0);
            commands.clear();
            const unsigned resets = hardwareResets;
            waitHook = [failAt] {
                if (!commands.empty() && commands.back().code == failAt) stuck = true;
            };
            const uint32_t start = clockMs;
            driver.present();
            CHECK(hardwareResets == resets);
            CHECK(!drivePowered && pins[EPAPER_POWER_PIN] == LOW && clockMs - start < 16000);
            DisplayDriver::PresentedFrameInfo info;
            CHECK(!driver.getPresentedFrameInfo(info));
            waitHook = nullptr; stuck = false;
            CHECK(driver.isAvailable() && driver.requestFullRefresh());
            driver.present();
            CHECK(pins[EPAPER_POWER_PIN] == HIGH && !drivePowered);
            CHECK(snapshotPixel(driver, 2, 0) == 0 && modes().back() == 0xee);
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
