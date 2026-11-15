#include "boards/crowpanel-4.2-interactive/drivers/display/crowpanel42_epaper_driver.h"
#include "drivers/monochrome_render.h"
#include "presented_frame.h"
#include "config_manager.h"
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <vector>

struct Command { uint8_t code; std::vector<uint8_t> data; };
static std::vector<Command> commands;
static uint32_t clock_ms = 1000, busy_until = 0, busy_from = 0, busy_delay = 0;
static bool busy_stuck = false, busy_missing = false, ota_active = false;
static bool waveform_scheduled = false;
static int pin_values[49] = {};
static int allocations = 0, fail_allocation = 0, live_allocations = 0;
static int live_mutexes = 0;
static bool fail_mutex = false;
static int spi_buses = 0;
static bool transaction_open = false;
static bool simulate_upload_time = false;
static size_t individual_data_transfers = 0;
static int failures = 0;
static std::function<void()> waveform_hook;
static std::function<void()> wait_hook;
static std::function<void()> upload_hook;
static std::function<void(uint8_t)> command_hook;
static uint32_t sequence = 0;
static EpaperRefreshSettings settings = {EPAPER_RENDER_MODE_BW, 1000, 500, true, 2, MONOCHROME_CONVERSION_THRESHOLD, MONOCHROME_THRESHOLD};
#define CHECK(condition) do { if (!(condition)) { std::printf("FAIL %d: %s\n", __LINE__, #condition); ++failures; } } while (0)

extern "C" unsigned long millis() { return clock_ms; }
int64_t esp_timer_get_time() { return uint64_t(clock_ms) * 1000; }
SemaphoreHandle_t test_freertos_create_mutex() {
    if (fail_mutex) return nullptr;
    auto* semaphore = new StaticSemaphore_t{};
    semaphore->signaled = true;
    ++live_mutexes;
    return semaphore;
}
void test_freertos_delete_mutex(SemaphoreHandle_t semaphore) {
    CHECK(live_mutexes > 0);
    --live_mutexes;
    delete semaphore;
}
void delay(unsigned long milliseconds) {
    CHECK(!transaction_open);
    clock_ms += milliseconds;
}
void vTaskDelay(TickType_t ticks) {
    CHECK(!transaction_open);
    clock_ms += ticks;
    if (wait_hook) wait_hook();
}
void pinMode(int, int) {}
void digitalWrite(int pin, int value) {
    if (pin == 47 && value == LOW && waveform_scheduled)
        CHECK(busy_missing || int32_t(busy_until - clock_ms) <= 0);
    pin_values[pin] = value;
}
int digitalRead(int pin) {
    if (pin == 47 || pin == 7) return pin_values[pin];
    CHECK(pin == 48);
    const bool waveformBusy = waveform_scheduled && !busy_missing && int32_t(clock_ms - busy_from) >= 0 &&
        int32_t(busy_until - clock_ms) > 0;
    return busy_stuck || waveformBusy ? LOW : HIGH;
}
void crowpanel_test_spi_begin(int clock, int miso, int mosi, int cs) {
    CHECK(clock == 12 && miso == -1 && mosi == 11 && cs == 45);
    CHECK(pin_values[7] == HIGH);
    ++spi_buses;
}
void crowpanel_test_spi_end() {
    CHECK(!transaction_open);
    CHECK(spi_buses == 1);
    --spi_buses;
}
void crowpanel_test_spi_begin_transaction() {
    CHECK(!transaction_open && spi_buses == 1);
    transaction_open = true;
}
void crowpanel_test_spi_end_transaction() {
    CHECK(transaction_open && pin_values[45] == HIGH);
    transaction_open = false;
}
void crowpanel_test_spi_byte(uint8_t value) {
    CHECK(transaction_open && pin_values[45] == LOW);
    if (pin_values[46] == LOW) {
        commands.push_back({value, {}});
        if (command_hook) command_hook(value);
        if (value == 0x17) {
            waveform_scheduled = true;
            busy_from = clock_ms + busy_delay;
            busy_until = busy_from + 10;
            if (waveform_hook) waveform_hook();
        }
    } else {
        ++individual_data_transfers;
        commands.back().data.push_back(value);
    }
}
void crowpanel_test_spi_data(const uint8_t* data, size_t length) {
    CHECK(transaction_open && pin_values[45] == LOW && pin_values[46] == HIGH);
    commands.back().data.insert(commands.back().data.end(), data, data + length);
    if (simulate_upload_time && length == 15000) clock_ms += 12;
    if (upload_hook && length == 15000) upload_hook();
}
void* heap_caps_malloc(size_t size, uint32_t) {
    if (++allocations == fail_allocation) return nullptr;
    void* data = std::malloc(size);
    if (data) ++live_allocations;
    return data;
}
void heap_caps_free(void* data) { if (data) { --live_allocations; std::free(data); } }
EpaperRefreshSettings config_manager_get_epaper_refresh_settings() { return settings; }
bool ota_activity_is_active() { return ota_active; }

static void reset() {
    CHECK(live_allocations == 0 && live_mutexes == 0 && spi_buses == 0 && !transaction_open);
    commands.clear();
    clock_ms = 1000;
    busy_until = busy_from = busy_delay = 0;
    busy_stuck = busy_missing = false;
    waveform_scheduled = false;
    ota_active = false;
    allocations = 0;
    fail_allocation = 0;
    fail_mutex = false;
    individual_data_transfers = 0;
    simulate_upload_time = false;
    waveform_hook = nullptr;
    wait_hook = nullptr;
    upload_hook = nullptr;
    command_hook = nullptr;
    sequence = 0;
    test_freertos_force_wait_timeout = true;
    test_freertos_semaphore_take_hook = [](SemaphoreHandle_t, TickType_t ticks) {
        if (ticks && ticks != portMAX_DELAY) {
            CHECK(!transaction_open);
            clock_ms += ticks;
            if (wait_hook) wait_hook();
        }
    };
    settings = {EPAPER_RENDER_MODE_BW, 1000, 500, true, 2, MONOCHROME_CONVERSION_THRESHOLD, MONOCHROME_THRESHOLD};
    settings.epaper_full_refresh_threshold = 2;
    settings.epaper_bw_conversion = MONOCHROME_CONVERSION_THRESHOLD;
    settings.epaper_bw_threshold = MONOCHROME_THRESHOLD;
}
static void flush_pixel(CrowPanel42EpaperDriver& driver, int x, int y, uint16_t color) {
    driver.flushSrcStride = 0;
    driver.setAddrWindow(x, y, 1, 1);
    driver.pushColors(&color, 1, false);
}
static void commit(CrowPanel42EpaperDriver& driver, DisplayIntent intent = DisplayIntent::Background) {
    driver.commitFrame({++sequence, sequence, intent, uint64_t(clock_ms) * 1000, uint64_t(clock_ms) * 1000});
}
static void pixel(CrowPanel42EpaperDriver& driver, int x, int y, uint16_t color,
                  DisplayIntent intent = DisplayIntent::Background) {
    driver.beginFrame();
    flush_pixel(driver, x, y, color);
    commit(driver, intent);
}
static std::vector<uint8_t> update_modes() {
    std::vector<uint8_t> modes;
    for (const auto& command : commands) if (command.code == 0x21 && command.data.size() == 42)
        modes.push_back(command.data.at(1) == 0x54 ? 0xf7 : 0xff);
    return modes;
}
static std::vector<std::vector<uint8_t>> frames() {
    std::vector<std::vector<uint8_t>> result;
    for (const auto& command : commands) if (command.code == 0x13) result.push_back(command.data);
    return result;
}

int main() {
    CHECK(!monochrome_is_white(0));
    CHECK(!monochrome_is_white(127));
    CHECK(monochrome_is_white(128));
    CHECK(monochrome_is_white(255));
    CHECK(!monochrome_is_white(63, 0, 0, MONOCHROME_CONVERSION_THRESHOLD, 64));
    CHECK(monochrome_is_white(64, 0, 0, MONOCHROME_CONVERSION_THRESHOLD, 64));
    CHECK(monochrome_is_white(1, 0, 0, MONOCHROME_CONVERSION_THRESHOLD, 1));
    CHECK(!monochrome_is_white(254, 0, 0, MONOCHROME_CONVERSION_THRESHOLD, 255));
    CHECK(monochrome_is_white(255, 0, 0, MONOCHROME_CONVERSION_THRESHOLD, 255));
    unsigned bayerWhite = 0;
    for (int row = 0; row < 4; ++row) {
        for (int column = 0; column < 4; ++column) {
            CHECK(!monochrome_is_white(0, column, row, MONOCHROME_CONVERSION_BAYER));
            CHECK(monochrome_is_white(255, column, row, MONOCHROME_CONVERSION_BAYER));
            const bool white = monochrome_is_white(128, column, row, MONOCHROME_CONVERSION_BAYER);
            bayerWhite += white;
            CHECK(white == monochrome_is_white(128, column + 4, row + 4, MONOCHROME_CONVERSION_BAYER, 1));
        }
    }
    CHECK(bayerWhite == 8);
    for (uint8_t tone : {uint8_t(64), uint8_t(128), uint8_t(192)}) {
        unsigned blueWhite = 0;
        for (int row = 0; row < 64; ++row) {
            for (int column = 0; column < 64; ++column) {
                CHECK(!monochrome_is_white(0, column, row, MONOCHROME_CONVERSION_BLUE_NOISE));
                CHECK(monochrome_is_white(255, column, row, MONOCHROME_CONVERSION_BLUE_NOISE));
                const bool white = monochrome_is_white(tone, column, row, MONOCHROME_CONVERSION_BLUE_NOISE);
                blueWhite += white;
                CHECK(white == monochrome_is_white(tone, column + 64, row + 64, MONOCHROME_CONVERSION_BLUE_NOISE, 1));
            }
        }
        CHECK(blueWhite == unsigned(tone) * 16);
    }
    reset();
    {
        CrowPanel42EpaperDriver driver;
        driver.init();
        for (int row = 0; row < 4; ++row) {
            for (int column = 0; column < 8; ++column) {
                pixel(driver, column, row, 0x7bef);
                pixel(driver, column + 8, row, 0x8410);
            }
        }
        driver.present();
        const auto image = frames().back();
        for (size_t row = 0; row < 4; ++row) {
            CHECK(image.at(row * 50) == 0x00);
            CHECK(image.at(row * 50 + 1) == 0xff);
        }
    }
    CHECK(live_allocations == 0);
    static_assert(config_manager_epaper_mode_supported(EPAPER_RENDER_MODE_BW), "B/W required");
    static_assert(!config_manager_epaper_mode_supported(EPAPER_RENDER_MODE_GRAYSCALE), "Grayscale unsupported");
    for (uint8_t conversion : {uint8_t(MONOCHROME_CONVERSION_THRESHOLD), uint8_t(MONOCHROME_CONVERSION_BAYER), uint8_t(MONOCHROME_CONVERSION_BLUE_NOISE)}) {
        for (uint8_t rotation : {uint8_t(0), uint8_t(1)}) {
            reset();
            settings.epaper_bw_conversion = conversion;
            settings.epaper_bw_threshold = 64;
            {
                CrowPanel42EpaperDriver driver;
                driver.init();
                driver.setRotation(rotation);
                settings.epaper_bw_conversion = MONOCHROME_CONVERSION_THRESHOLD;
                settings.epaper_bw_threshold = 255;
                for (int row = 0; row < 64; ++row) {
                    for (int column = 0; column < 64; ++column) pixel(driver, column + 3, row + 5, 0x7bef);
                }
                driver.present();
                std::vector<uint8_t> image(15200);
                CrowPanel42EpaperDriver::PresentedFrameInfo info;
                CHECK(driver.getPresentedFrameInfo(info));
                CHECK(driver.copyPresentedFrame(image.data(),
                    presented_frame_bytes(info.width, info.height, info.bitDepth)));
                const size_t rowBytes = presented_frame_row_bytes(info.width, info.bitDepth);
                for (int row = 0; row < 64; ++row) {
                    for (int column = 0; column < 64; ++column) {
                        const size_t offset = size_t(row + 5) * rowBytes + (column + 3) / 8;
                        const bool white = image[offset] & (0x80 >> ((column + 3) & 7));
                        CHECK(white == monochrome_is_white(124, column + 3, row + 5, conversion, 64));
                    }
                }
            }
            CHECK(live_allocations == 0);
        }
    }
    reset();
    {
        CrowPanel42EpaperDriver driver;
        driver.init();
        CHECK(driver.isAvailable());
        const uint8_t expected[] = {0x00, 0x01, 0x06, 0x30, 0x61, 0x82, 0x50, 0x60, 0xe3};
        CHECK(commands.size() == sizeof(expected));
        for (size_t index = 0; index < sizeof(expected); ++index) CHECK(commands.at(index).code == expected[index]);
        CHECK(commands.at(0).data == std::vector<uint8_t>({0x3f, 0x4d}));
        CHECK(commands.at(4).data == std::vector<uint8_t>({1, 0x90, 1, 0x2c}));
        uint16_t data[] = {0, 0xffff, 0xffff, 0};
        driver.beginFrame();
        driver.flushSrcStride = 4;
        driver.setAddrWindow(0, 0, 1, 2);
        driver.pushColors(data, 2, false);
        commit(driver);
        driver.present();
        CHECK(update_modes() == std::vector<uint8_t>({0xf7, 0xf7}));
        size_t activations = 0;
        size_t white_planes = 0;
        for (const auto& command : commands) {
            if (command.code == 0x10 || (command.code == 0x13 && white_planes == 1)) {
                CHECK(command.data.size() == 15000);
                for (uint8_t value : command.data) CHECK(value == 0xff);
                ++white_planes;
            }
            if (command.code >= 0x20 && command.code <= 0x24) {
                CHECK(command.data.size() == 42);
                CHECK(command.data.at(0) == 1 && command.data.at(5) == 1);
                for (size_t index = 7; index < command.data.size(); ++index) CHECK(command.data[index] == 0);
            }
            if (command.code == 0x17) {
                CHECK(command.data == std::vector<uint8_t>({0xa5}));
                ++activations;
            }
        }
        CHECK(activations == 2);
        CHECK(white_planes == 2);
        CHECK(individual_data_transfers == 0);
        auto images = frames();
        CHECK(images.size() == 2 && images.back().size() == 15000);
        CHECK(images.back().at(0) == 0x7f && images.back().at(50) == 0xff);
        const size_t count = commands.size();
        pixel(driver, 0, 0, 0);
        driver.present();
        CHECK(commands.size() == count);
        CHECK(driver.requestFullRefresh());
        driver.present();
        CHECK(update_modes() == std::vector<uint8_t>({0xf7, 0xf7, 0xf7, 0xf7}));
        const uint32_t start = clock_ms;
        pixel(driver, 1, 0, 0);
        driver.present();
        CHECK(int32_t(clock_ms - busy_until) >= 0);
        pixel(driver, 2, 0, 0);
        driver.present();
        pixel(driver, 3, 0, 0);
        driver.present();
        CHECK(clock_ms - start >= 1500);
        CHECK(update_modes() == std::vector<uint8_t>({0xf7, 0xf7, 0xf7, 0xf7, 0xff, 0xff, 0xf7, 0xf7}));
    }
    CHECK(live_allocations == 0);
    for (uint8_t rotation = 0; rotation < 4; ++rotation) {
        reset();
        CrowPanel42EpaperDriver driver;
        driver.init();
        driver.setRotation(rotation);
        CHECK(driver.width() == (rotation & 1 ? 300 : 400));
        CHECK(driver.height() == (rotation & 1 ? 400 : 300));
        std::vector<uint8_t> captured(15200);
        CrowPanel42EpaperDriver::PresentedFrameInfo info;
        CHECK(!driver.getPresentedFrameInfo(info));
        CHECK(!driver.copyPresentedFrame(captured.data(), captured.size()));
        pixel(driver, 0, 0, 0);
        pixel(driver, driver.width() - 1, 1, 0);
        waveform_hook = [&] {
            CHECK(!driver.getPresentedFrameInfo(info));
            CHECK(!driver.copyPresentedFrame(captured.data(), captured.size()));
        };
        driver.present();
        waveform_hook = nullptr;
        CHECK(driver.getPresentedFrameInfo(info));
        CHECK(info.width == driver.width() && info.height == driver.height() && info.bitDepth == 1);
        const size_t indices[] = {0, 49, 14999, 14950};
        const uint8_t values[] = {0x7f, 0xfe, 0xfe, 0x7f};
        CHECK(frames().back().at(indices[rotation]) == values[rotation]);
        CHECK(!driver.copyPresentedFrame(nullptr, captured.size()));
        const size_t required = presented_frame_bytes(info.width, info.height, info.bitDepth);
        CHECK(!driver.copyPresentedFrame(captured.data(), required - 1));
        pixel(driver, 1, 0, 0);
        const size_t commandCount = commands.size();
        CHECK(driver.copyPresentedFrame(captured.data(), captured.size()));
        CHECK(commands.size() == commandCount);
        const size_t rowBytes = presented_frame_row_bytes(info.width, info.bitDepth);
        for (uint16_t row = 0; row < info.height; ++row) {
            for (uint16_t column = 0; column < info.width; ++column) {
                const bool white = captured[size_t(row) * rowBytes + column / 8] & (0x80 >> (column & 7));
                const bool expectedWhite = !((column == 0 && row == 0) ||
                    (column == info.width - 1 && row == 1));
                CHECK(white == expectedWhite);
            }
        }
    }
    for (bool alreadyPresented : {false, true}) {
        reset();
        CrowPanel42EpaperDriver driver;
        driver.init();
        if (alreadyPresented) {
            pixel(driver, 20, 0, 0);
            driver.present();
        }
        pixel(driver, 0, 0, 0);
        const uint32_t started = clock_ms;
        uint32_t firstWaveform = 0;
        int redraws = 0;
        wait_hook = [&] {
            if (redraws < 10) pixel(driver, ++redraws, 0, 0);
        };
        waveform_hook = [&] {
            if (!firstWaveform) {
                firstWaveform = clock_ms;
                wait_hook = nullptr;
            }
        };
        driver.present();
        CHECK(firstWaveform != 0 && firstWaveform - started <= 750);
        waveform_hook = nullptr;
        wait_hook = nullptr;
    }
    for (bool requestFull : {false, true}) {
        reset();
        CrowPanel42EpaperDriver driver;
        driver.init();
        pixel(driver, 0, 0, 0);
        bool injected = false;
        waveform_hook = [&] {
            if (injected || frames().size() != 2) return;
            injected = true;
            pixel(driver, 1, 0, 0);
            if (requestFull) CHECK(driver.requestFullRefresh());
        };
        driver.present();
        CHECK(injected);
        CHECK(update_modes() == (requestFull ? std::vector<uint8_t>({0xf7, 0xf7, 0xf7, 0xf7})
                                             : std::vector<uint8_t>({0xf7, 0xf7, 0xff})));
        auto images = frames();
        CHECK(images.at(1).at(0) == 0x7f && images.back().at(0) == 0x3f);
        waveform_hook = nullptr;
    }
    reset();
    {
        CrowPanel42EpaperDriver driver;
        driver.init();
        pixel(driver, 0, 0, 0);
        ota_active = true;
        const size_t count = commands.size();
        driver.present();
        CHECK(commands.size() == count);
        ota_active = false;
        driver.present();
        waveform_hook = [&] { ota_active = true; pixel(driver, 2, 0, 0); };
        pixel(driver, 1, 0, 0);
        driver.present();
        CHECK(ota_active && frames().back().at(0) == 0x3f);
        CrowPanel42EpaperDriver::PresentedFrameInfo info;
        CHECK(!driver.getPresentedFrameInfo(info));
        waveform_hook = nullptr;
        ota_active = false;
        driver.present();
        CHECK(frames().back().at(0) == 0x1f);
    }
    reset();
    {
        CrowPanel42EpaperDriver driver;
        busy_stuck = true;
        const uint32_t start = clock_ms;
        driver.init();
        CHECK(driver.isAvailable());
        CHECK(clock_ms - start == 210);
        pixel(driver, 0, 0, 0);
        driver.present();
        CHECK(!driver.isAvailable() && !driver.requestFullRefresh());
        CrowPanel42EpaperDriver::PresentedFrameInfo info;
        CHECK(!driver.getPresentedFrameInfo(info));
        CHECK(clock_ms - start == 15360);
    }
    for (int failure = 1; failure <= 3; ++failure) {
        reset();
        {
            CrowPanel42EpaperDriver driver;
            fail_allocation = failure <= 2 ? failure : 0;
            fail_mutex = failure == 3;
            driver.init();
            CHECK(!driver.isAvailable() && !driver.requestFullRefresh());
            CHECK(live_allocations == 0);
            CHECK(live_mutexes == 0);
            driver.present();
            CHECK(commands.empty());
        }
        CHECK(live_allocations == 0);
        CHECK(live_mutexes == 0);
    }
    reset();
    {
        CrowPanel42EpaperDriver driver;
        driver.init();
        pixel(driver, 0, 0, 0);
        const size_t count = commands.size();
        // An older presentation signal must not expose the next multi-flush frame.
        driver.beginFrame();
        flush_pixel(driver, 1, 0, 0);
        driver.present();
        CHECK(commands.size() == count);
        flush_pixel(driver, 2, 0, 0);
        commit(driver, DisplayIntent::Interactive);
        driver.present();
        CHECK(frames().back().at(0) == 0x1f);
    }
    reset();
    {
        CrowPanel42EpaperDriver driver;
        driver.init();
        pixel(driver, 0, 0, 0);
        driver.present();
        pixel(driver, 1, 0, 0);
        uint32_t accepted = 0, triggered = 0;
        wait_hook = [&] {
            if (accepted) return;
            accepted = clock_ms;
            pixel(driver, 2, 0, 0, DisplayIntent::Interactive);
            // A background commit must not erase pending interaction priority.
            pixel(driver, 3, 0, 0);
        };
        waveform_hook = [&] { triggered = clock_ms; };
        driver.present();
        CHECK(accepted && triggered - accepted < 20);
        CHECK(frames().back().at(0) == 0x0f);
#if HAS_DISPLAY_DIAGNOSTICS
        DisplayPresentationStats stats;
        CHECK(driver.getPresentationStats(&stats));
        CHECK(stats.last.requested_us == uint64_t(accepted) * 1000);
        CHECK(stats.last.request_sequence == 3);
        CHECK(stats.last.coalesce_us < 20000);
#endif
    }
    reset();
    {
        CrowPanel42EpaperDriver driver;
        driver.init();
        pixel(driver, 0, 0, 0);
        driver.present();
        const size_t count = commands.size();
        pixel(driver, 0, 0, 0, DisplayIntent::Interactive);
        driver.present();
        driver.beginFrame();
        flush_pixel(driver, 1, 0, 0);
        flush_pixel(driver, 1, 0, 0xffff);
        commit(driver, DisplayIntent::Interactive);
        driver.present();
        CHECK(commands.size() == count);
        const uint32_t started = clock_ms;
        pixel(driver, 2, 0, 0);
        driver.present();
        CHECK(clock_ms - started >= settings.epaper_min_refresh_interval_ms);
    }
    reset();
    {
        CrowPanel42EpaperDriver driver;
        driver.init();
        pixel(driver, 0, 0, 0);
        bool injected = false;
        waveform_hook = [&] {
            if (injected || frames().size() != 2) return;
            injected = true;
            driver.beginFrame();
            flush_pixel(driver, 1, 0, 0);
            CHECK(driver.requestFullRefresh());
        };
        driver.present();
        CHECK(injected && frames().size() == 2);
        std::vector<uint8_t> captured(15000);
        CHECK(driver.copyPresentedFrame(captured.data(), captured.size()));
        CHECK(captured.at(0) == 0x7f);
        waveform_hook = nullptr;
        flush_pixel(driver, 2, 0, 0);
        commit(driver, DisplayIntent::Interactive);
        driver.present();
        CHECK(update_modes() == std::vector<uint8_t>({0xf7, 0xf7, 0xf7, 0xf7}));
        CHECK(frames().back().at(0) == 0x1f);
    }
    for (bool stop : {false, true}) {
        reset();
        CrowPanel42EpaperDriver driver;
        driver.init();
        pixel(driver, 0, 0, 0);
        driver.present();
        const size_t count = commands.size();
        command_hook = [&](uint8_t code) {
            if (code != 0x20) return;
            if (stop) driver.stopPresentation();
            else ota_active = true;
        };
        pixel(driver, 1, 0, 0, DisplayIntent::Interactive);
        driver.present();
        CHECK(commands.size() == count + 3);
        CHECK(commands.back().code == 0x20);
        CHECK(!transaction_open);
        command_hook = nullptr;
    }
    for (bool stop : {false, true}) {
        reset();
        CrowPanel42EpaperDriver driver;
        driver.init();
        pixel(driver, 0, 0, 0);
        driver.present();
        pixel(driver, 1, 0, 0);
        const size_t count = commands.size();
        const uint32_t started = clock_ms;
        wait_hook = [&] {
            if (stop) driver.stopPresentation();
            else ota_active = true;
        };
        driver.present();
        CHECK(commands.size() == count && clock_ms - started <= 20);
        CHECK(!transaction_open);
        wait_hook = nullptr;
        if (stop) {
            CHECK(!driver.requestFullRefresh());
        } else {
            ota_active = false;
            driver.present();
            CHECK(frames().back().at(0) == 0x3f);
        }
    }
    // Both pre-activation upload and owned waveform boundaries preserve lifetime.
    for (bool stop : {false, true}) {
        for (bool activated : {false, true}) {
            reset();
            CrowPanel42EpaperDriver driver;
            driver.init();
            pixel(driver, 0, 0, 0);
            driver.present();
            const size_t oldModes = update_modes().size();
            auto cancel = [&] {
                if (stop) driver.stopPresentation();
                else ota_active = true;
                pixel(driver, 2, 0, 0, DisplayIntent::Interactive);
            };
            if (activated) waveform_hook = cancel;
            else upload_hook = cancel;
            pixel(driver, 1, 0, 0, DisplayIntent::Interactive);
            driver.present();
            CHECK(update_modes().size() == oldModes + (activated ? 1 : 0));
            if (activated) CHECK(int32_t(clock_ms - busy_until) >= 0);
            CHECK(!transaction_open && spi_buses == 1 && live_allocations == 2);
            CrowPanel42EpaperDriver::PresentedFrameInfo info;
            CHECK(!driver.getPresentedFrameInfo(info));
            std::vector<uint8_t> captured(15000);
            CHECK(!driver.copyPresentedFrame(captured.data(), captured.size()));
#if HAS_DISPLAY_DIAGNOSTICS
            DisplayPresentationStats stats;
            CHECK(driver.getPresentationStats(&stats));
            CHECK(!stats.last.success && stats.last.published_us == 0);
#endif
            waveform_hook = upload_hook = nullptr;
            const size_t count = commands.size();
            driver.present();
            CHECK(commands.size() == count);
            if (!stop) {
                ota_active = false;
                driver.present();
                CHECK(driver.getPresentedFrameInfo(info));
                CHECK(frames().back().at(0) == 0x1f);
                const auto modes = update_modes();
                CHECK(modes.at(modes.size() - 2) == 0xf7 && modes.back() == 0xf7);
            }
        }
    }
    for (bool stop : {false, true}) {
        reset();
        CrowPanel42EpaperDriver driver;
        driver.init();
        pixel(driver, 0, 0, 0);
        waveform_hook = [&] {
            if (stop) driver.stopPresentation();
            else ota_active = true;
        };
        driver.present();
        // Finish the first clearing waveform and its guard, but never reset/start the second.
        CHECK(update_modes() == std::vector<uint8_t>({0xf7}));
        CHECK(clock_ms - busy_until >= 500);
        CHECK(!transaction_open);
        waveform_hook = nullptr;
        if (!stop) {
            ota_active = false;
            driver.present();
            CHECK(update_modes() == std::vector<uint8_t>({0xf7, 0xf7, 0xf7}));
        }
    }
    reset();
    {
        CrowPanel42EpaperDriver driver;
        driver.init();
        pixel(driver, 0, 0, 0);
        busy_delay = 7;
        driver.present();
        CHECK(int32_t(clock_ms - busy_until) >= 500);
        CrowPanel42EpaperDriver::PresentedFrameInfo info;
        CHECK(driver.getPresentedFrameInfo(info));
        busy_missing = true;
        pixel(driver, 1, 0, 0, DisplayIntent::Interactive);
        const uint32_t started = clock_ms;
        driver.present();
        CHECK(clock_ms - started == 15000);
        CHECK(!driver.isAvailable() && !driver.getPresentedFrameInfo(info));
        CHECK(!transaction_open);
    }
    reset();
    {
        CrowPanel42EpaperDriver driver;
        driver.init();
        pixel(driver, 0, 0, 0);
        driver.present();
        waveform_hook = [&] { busy_stuck = true; driver.stopPresentation(); };
        pixel(driver, 1, 0, 0, DisplayIntent::Interactive);
        const uint32_t started = clock_ms;
        driver.present();
        CHECK(clock_ms - started == 15000);
        CHECK(!driver.isAvailable() && !transaction_open);
        waveform_hook = nullptr;
        busy_stuck = false;
        driver.init();
        CHECK(driver.isAvailable() && live_allocations == 2 && live_mutexes == 1 && spi_buses == 1);
        pixel(driver, 2, 0, 0, DisplayIntent::Interactive);
        driver.present();
        CHECK(frames().back().at(0) == 0xdf);
        CHECK(update_modes().back() == 0xf7);
    }
    for (bool interactive : {false, true}) {
        reset();
        CrowPanel42EpaperDriver driver;
        driver.init();
        clock_ms = UINT32_MAX - 2500;
        pixel(driver, 0, 0, 0);
        driver.present();
        const uint32_t started = clock_ms;
        pixel(driver, 1, 0, 0, interactive ? DisplayIntent::Interactive : DisplayIntent::Background);
        driver.present();
        CHECK(clock_ms - started == (interactive ? 10 : 510));
    }
#if HAS_DISPLAY_DIAGNOSTICS
    reset();
    {
        CrowPanel42EpaperDriver driver;
        driver.init();
        simulate_upload_time = true;
        pixel(driver, 0, 0, 0, DisplayIntent::Interactive);
        driver.present();
        DisplayPresentationStats stats;
        CHECK(driver.getPresentationStats(&stats));
        CHECK(stats.attempts == 1 && stats.last.success);
        CHECK(stats.last.full && stats.last.width == 400 && stats.last.height == 300);
        CHECK(stats.last.uploaded_bytes == 15000 && stats.last.upload_us == 12000);
        CHECK(stats.last.trigger_us >= stats.last.snapshot_us + stats.last.upload_us);
        CHECK(stats.last.waveform_done_us == stats.last.trigger_us + 10000);
        CHECK(stats.last.published_us >= stats.last.waveform_done_us + 500000);
        CHECK(stats.last.ready_us == stats.last.eligible_us && stats.last.coalesce_us == 0);
        CHECK(!driver.getPresentationStats(nullptr));
    }
#endif
    CHECK(live_mutexes == 0);
    CHECK(spi_buses == 0 && live_allocations == 0 && !transaction_open);
    std::printf("CrowPanel driver: %s\n", failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}