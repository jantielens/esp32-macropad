#include <SPI.h>
#include "drivers/it8951_epaper_driver.h"
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <vector>

struct Transaction { uint32_t hz; std::vector<uint8_t> bytes; bool imageData = false; };
static std::vector<Transaction> transactions;
static uint32_t clockMs = 1000;
static bool busy = false, ota = false, badVcom = false;
static uint16_t command = 0;
static int allocations = 0, failAllocation = 0, liveAllocations = 0, failures = 0;
static bool failMutex = false;
static int liveMutexes = 0;
static int pins[49] = {};
static std::vector<unsigned long> delays;
static std::function<void()> waveformHook, uploadHook;
static EpaperRefreshSettings settings = {EPAPER_RENDER_MODE_GRAYSCALE, 1000, 500, true, 2, 0, 128};
#define CHECK(condition) do { if (!(condition)) { std::printf("FAIL %d: %s\n", __LINE__, #condition); ++failures; } } while (0)

extern "C" unsigned long millis() { return clockMs; }
void delay(unsigned long ms) { clockMs += ms; delays.push_back(ms); }
void vTaskDelay(TickType_t ticks) { clockMs += ticks; }
void yield() {}
void pinMode(int pin, int) { CHECK(pin >= 0 && pin < 49 && pin != 17); }
void digitalWrite(int pin, int value) { CHECK(pin >= 0 && pin < 49 && pin != 17); pins[pin] = value; }
int digitalRead(int pin) { CHECK(pin == EPAPER_BUSY_PIN); return busy ? LOW : HIGH; }
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
void* heap_caps_malloc(size_t size, uint32_t) {
    if (++allocations == failAllocation) return nullptr;
    void* result = std::malloc(size);
    if (result) ++liveAllocations;
    return result;
}
void heap_caps_free(void* data) { if (data) { --liveAllocations; std::free(data); } }
void it8951_test_spi_begin(int clock, int miso, int mosi, int cs) {
    CHECK(clock == EPAPER_SPI_SCK_PIN && miso == EPAPER_SPI_MISO_PIN && mosi == EPAPER_SPI_MOSI_PIN && cs == -1);
    if (IT8951_E1003_PROTOCOL) CHECK(pins[11] == HIGH && pins[21] == HIGH);
}
void it8951_test_spi_transaction(uint32_t hz) { transactions.push_back({hz, {}}); }
static uint16_t word(const std::vector<uint8_t>& bytes, size_t offset) {
    return (uint16_t(bytes.at(offset)) << 8) | bytes.at(offset + 1);
}
uint8_t it8951_test_spi_byte(uint8_t value) {
    CHECK(pins[EPAPER_CS_PIN] == LOW);
    auto& tx = transactions.back();
    tx.bytes.push_back(value);
    const size_t index = tx.bytes.size() - 1;
    if (index == 3 && word(tx.bytes, 0) == 0x6000) {
        command = word(tx.bytes, 2);
        if (command == 0x0034 && waveformHook) waveformHook();
    }
    if (index >= 4 && word(tx.bytes, 0) == 0x1000) {
        CHECK(tx.hz == (IT8951_E1003_PROTOCOL ? 10000000u : 1000000u));
        const uint16_t info[] = {1872, 1404, 0x1234, 0x0001};
        const size_t position = (index - 4) / 2;
        const uint16_t result = command == 0x0039 ? (badVcom ? 1000 : IT8951_VCOM_MV) :
            (position < 4 ? info[position] : 0);
        return index & 1 ? result & 0xff : result >> 8;
    }
    return 0;
}
void it8951_test_spi_data(const uint8_t* data, size_t length) {
    CHECK(pins[EPAPER_CS_PIN] == LOW);
    transactions.back().imageData = true;
    auto& bytes = transactions.back().bytes;
    bytes.insert(bytes.end(), data, data + length);
    if (uploadHook) uploadHook();
}
static std::vector<std::vector<uint16_t>> arguments(uint16_t wanted) {
    std::vector<std::vector<uint16_t>> result;
    bool matching = false;
    for (const auto& tx : transactions) {
        if (tx.bytes.size() == 4 && word(tx.bytes, 0) == 0x6000) {
            matching = word(tx.bytes, 2) == wanted;
            if (matching) result.emplace_back();
        } else if (matching && !tx.imageData && tx.bytes.size() == 4 && word(tx.bytes, 0) == 0) {
            result.back().push_back(word(tx.bytes, 2));
        }
    }
    return result;
}
static void pixel(IT8951EpaperDriver& driver, int x, int y, uint16_t color) {
    driver.setAddrWindow(x, y, 1, 1);
    driver.pushColors(&color, 1, false);
}
static void reset() {
    CHECK(liveAllocations == 0 && liveMutexes == 0);
    transactions.clear();
    delays.clear();
    clockMs = 1000;
    busy = ota = badVcom = failMutex = false;
    allocations = failAllocation = 0;
    waveformHook = uploadHook = nullptr;
}
int main() {
    for (int mode : {EPAPER_RENDER_MODE_BW, EPAPER_RENDER_MODE_GRAYSCALE}) {
        reset();
        DeviceConfig config{};
        config.epaper_render_mode = mode;
        config.epaper_bw_threshold = 128;
        {
            IT8951EpaperDriver driver(&config);
            driver.init();
            CHECK(driver.isAvailable());
            CHECK(driver.presentationMode() == mode);
            CHECK(delays.at(1) == (IT8951_E1003_PROTOCOL ? 10u : 200u));
            CHECK(arguments(0x0039).front() == std::vector<uint16_t>({IT8951_E1003_PROTOCOL ? 2u : 1u, IT8951_VCOM_MV}));
            if (!IT8951_E1003_PROTOCOL) CHECK(arguments(0x0011).front() == std::vector<uint16_t>({4, 1}));
            driver.setRotation(2);
            CHECK(driver.width() == 1872 && driver.height() == 1404);
            pixel(driver, 1, 2, 0);
            driver.present();
            CHECK(arguments(0x0034).back() == std::vector<uint16_t>({0, 0, 1872, 1404, 2}));
            transactions.clear();
            pixel(driver, 9, 2, 0);
            pixel(driver, 13, 2, 0x8410);
            driver.present();
            const uint16_t expectedX = IT8951_E1003_PROTOCOL ? 8 : 1856;
            const uint16_t waveform = mode == EPAPER_RENDER_MODE_BW ? 1 : 2;
            CHECK(arguments(0x0021).back() == std::vector<uint16_t>({IT8951_E1003_PROTOCOL ? 0x20u : 0x120u, expectedX, 1401, 8, 1}));
            CHECK(arguments(0x0034).back() == std::vector<uint16_t>({expectedX, 1401, 8, 1, waveform}));
            bool foundPixels = false;
            const uint8_t grayByte = mode == EPAPER_RENDER_MODE_BW ? 0xff : 0x8f;
            const std::vector<uint8_t> expectedBytes = IT8951_E1003_PROTOCOL ?
                std::vector<uint8_t>{0, 0, 0xff, 0x0f, 0xff, grayByte} :
                std::vector<uint8_t>{0, 0, 0xff, grayByte, 0xff, 0x0f};
            for (const auto& tx : transactions) {
                if (tx.imageData && tx.bytes == expectedBytes) foundPixels = true;
            }
            CHECK(foundPixels);
            const size_t unchanged = transactions.size();
            pixel(driver, 9, 2, 0);
            driver.present();
            CHECK(transactions.size() == unchanged);
            pixel(driver, 17, 2, 0);
            waveformHook = [&] { pixel(driver, 25, 2, 0); };
            driver.present();
            waveformHook = nullptr;
            driver.present();
            CHECK(arguments(0x0034).back() == std::vector<uint16_t>({0, 0, 1872, 1404, 2}));
            transactions.clear();
            pixel(driver, 33, 2, 0);
            busy = true;
            driver.present();
            busy = false;
            driver.present();
            CHECK(arguments(0x0034).back() == std::vector<uint16_t>({0, 0, 1872, 1404, 2}));
            transactions.clear();
            CHECK(driver.requestFullRefresh());
            ota = true;
            driver.present();
            CHECK(transactions.empty());
            ota = false;
            uploadHook = [] { ota = true; };
            driver.present();
            CHECK(arguments(0x0034).empty());
            CHECK(!arguments(0x0022).empty());
            ota = false;
            uploadHook = nullptr;
            driver.present();
            CHECK(arguments(0x0034).back() == std::vector<uint16_t>({0, 0, 1872, 1404, 2}));
        }
    }
    for (int failed : {1, 2, 3}) {
        reset();
        failAllocation = failed;
        failMutex = failed == 3;
        {
            IT8951EpaperDriver driver(nullptr);
            driver.init();
            CHECK(!driver.isAvailable());
            CHECK(!driver.requestFullRefresh());
            driver.present();
        }
    }
    reset();
    busy = true;
    {
        IT8951EpaperDriver driver(nullptr);
        driver.init();
        CHECK(!driver.isAvailable());
    }
    if (!IT8951_E1003_PROTOCOL) {
        reset();
        badVcom = true;
        IT8951EpaperDriver driver(nullptr);
        driver.init();
        CHECK(!driver.isAvailable());
    }
    CHECK(liveAllocations == 0 && liveMutexes == 0);
    std::printf("%s: IT8951 %s transport and presentation\n", failures ? "FAIL" : "PASS",
                IT8951_E1003_PROTOCOL ? "E1003" : "Waveshare");
    return failures ? 1 : 0;
}
