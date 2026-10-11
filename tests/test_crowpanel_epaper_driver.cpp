#include "drivers/crowpanel42_epaper_driver.h"
#include "drivers/monochrome_render.h"
#include "config_manager.h"
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <vector>

struct Command { uint8_t code; std::vector<uint8_t> data; };
static std::vector<Command> commands;
static uint32_t clock_ms = 1000, busy_until = 0;
static bool busy_stuck = false, ota_active = false;
static int pin_values[49] = {};
static int allocations = 0, fail_allocation = 0, live_allocations = 0;
static int failures = 0;
static std::function<void()> waveform_hook;
static EpaperRefreshSettings settings = {EPAPER_RENDER_MODE_BW, 1000, 500, true, 2};
#define CHECK(condition) do { if (!(condition)) { std::printf("FAIL %d: %s\n", __LINE__, #condition); ++failures; } } while (0)

extern "C" unsigned long millis() { return clock_ms; }
void delay(unsigned long milliseconds) { clock_ms += milliseconds; }
void vTaskDelay(TickType_t ticks) { clock_ms += ticks; }
void pinMode(int, int) {}
void digitalWrite(int pin, int value) { pin_values[pin] = value; }
int digitalRead(int pin) {
    if (pin == 47 || pin == 7) return pin_values[pin];
    CHECK(pin == 48);
    return busy_stuck || int32_t(busy_until - clock_ms) > 0 ? HIGH : LOW;
}
void crowpanel_test_spi_begin(int clock, int miso, int mosi, int cs) {
    CHECK(clock == 12 && miso == -1 && mosi == 11 && cs == 45);
    CHECK(pin_values[7] == HIGH);
}
void crowpanel_test_spi_byte(uint8_t value) {
    CHECK(pin_values[45] == LOW);
    if (pin_values[46] == LOW) {
        commands.push_back({value, {}});
        if (value == 0x17) {
            busy_until = clock_ms + 10;
            if (waveform_hook) waveform_hook();
        }
    } else commands.back().data.push_back(value);
}
void crowpanel_test_spi_data(const uint8_t* data, size_t length) {
    CHECK(pin_values[45] == LOW && pin_values[46] == HIGH);
    commands.back().data.assign(data, data + length);
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
    commands.clear();
    clock_ms = 1000;
    busy_until = 0;
    busy_stuck = false;
    ota_active = false;
    allocations = 0;
    fail_allocation = 0;
    waveform_hook = nullptr;
    settings.epaper_full_refresh_threshold = 2;
}
static void pixel(CrowPanel42EpaperDriver& driver, int x, int y, uint16_t color) {
    driver.flushSrcStride = 0;
    driver.setAddrWindow(x, y, 1, 1);
    driver.pushColors(&color, 1, false);
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
        driver.flushSrcStride = 4;
        driver.setAddrWindow(0, 0, 1, 2);
        driver.pushColors(data, 2, false);
        driver.present();
        CHECK(update_modes() == std::vector<uint8_t>({0xf7, 0xf7}));
        size_t activations = 0;
        for (const auto& command : commands) {
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
        pixel(driver, 0, 0, 0);
        driver.present();
        const size_t indices[] = {0, 49, 14999, 14950};
        const uint8_t values[] = {0x7f, 0xfe, 0xfe, 0x7f};
        CHECK(frames().back().at(indices[rotation]) == values[rotation]);
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
            pixel(driver, 1, 0, 0);
            CHECK(driver.requestFullRefresh());
        };
        driver.present();
        CHECK(injected);
        CHECK(update_modes() == std::vector<uint8_t>({0xf7, 0xf7, 0xf7, 0xf7}));
        auto images = frames();
        CHECK(images.at(1).at(0) == 0x7f && images.at(3).at(0) == 0x3f);
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
        CHECK(clock_ms - start <= 15600 && clock_ms - start >= 15420);
    }
    for (int failure = 1; failure <= 2; ++failure) {
        reset();
        {
            CrowPanel42EpaperDriver driver;
            fail_allocation = failure;
            driver.init();
            CHECK(!driver.isAvailable() && !driver.requestFullRefresh());
            driver.present();
            CHECK(commands.empty());
        }
        CHECK(live_allocations == 0);
    }
    std::printf("CrowPanel driver: %s\n", failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}