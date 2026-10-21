#include <SPI.h>
#include "drivers/it8951_epaper_driver.h"
#include "drivers/monochrome_render.h"
#include "presented_frame.h"
#include "presented_png.h"
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <utility>
#include <vector>

struct Transaction { uint32_t hz; std::vector<uint8_t> bytes; bool imageData = false; };
static std::vector<Transaction> transactions;
static uint32_t clockMs = 1000;
static bool busy = false, ota = false, badVcom = false;
static uint16_t command = 0;
static uint16_t expectedVcom = EPAPER_DEFAULT_VCOM_MV;
static int allocations = 0, failAllocation = 0, liveAllocations = 0, failures = 0;
static bool failMutex = false;
static int liveMutexes = 0;
static int pins[49] = {};
static std::vector<unsigned long> delays;
static std::function<void()> waveformHook, uploadHook;
static EpaperRefreshSettings settings = {EPAPER_RENDER_MODE_GRAYSCALE, 1000, 500, true, 2, 0, 128};
#define CHECK(condition) do { if (!(condition)) { std::printf("FAIL %d: %s\n", __LINE__, #condition); ++failures; } } while (0)

static uint32_t readBigEndian32(const uint8_t* bytes) {
    return (uint32_t(bytes[0]) << 24) | (uint32_t(bytes[1]) << 16) |
        (uint32_t(bytes[2]) << 8) | bytes[3];
}

static uint32_t referenceCrc32(const uint8_t* bytes, size_t length) {
    uint32_t crc = 0xFFFFFFFF;
    for (size_t index = 0; index < length; ++index) {
        crc ^= bytes[index];
        for (uint8_t bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ ((crc & 1) ? 0xEDB88320 : 0);
    }
    return ~crc;
}

static uint32_t referenceAdler32(const std::vector<uint8_t>& bytes) {
    uint32_t a = 1, b = 0;
    for (uint8_t value : bytes) {
        a = (a + value) % 65521;
        b = (b + a) % 65521;
    }
    return (b << 16) | a;
}

static void checkPresentedPng(uint16_t width, uint16_t height, uint8_t bitDepth,
                              const std::vector<uint8_t>& pixels,
                              const std::vector<uint8_t>& expectedScanlines) {
    size_t pixelBytes = 0, pngCapacity = 0;
    CHECK(presented_png::encoded_size(width, height, bitDepth, &pixelBytes, &pngCapacity));
    CHECK(pixelBytes == pixels.size());
    if (!pixelBytes || !pngCapacity || pixelBytes != pixels.size()) return;

    std::vector<uint8_t> png(pngCapacity);
    size_t pngSize = 0;
    CHECK(presented_png::encode(width, height, bitDepth, pixels.data(), pixels.size(),
                                png.data(), png.size(), &pngSize));
    CHECK(pngSize == png.size());
    if (pngSize != png.size()) return;

    static const uint8_t signature[] = {137, 80, 78, 71, 13, 10, 26, 10};
    CHECK(memcmp(png.data(), signature, sizeof(signature)) == 0);
    size_t offset = sizeof(signature);
    std::vector<uint8_t> compressed;
    bool sawHeader = false, sawImage = false, sawEnd = false;
    while (offset + 12 <= png.size()) {
        const uint32_t chunkBytes = readBigEndian32(png.data() + offset);
        offset += 4;
        const size_t chunkStart = offset;
        if (chunkBytes > png.size() - offset - 4) {
            CHECK(false);
            return;
        }
        const uint8_t* type = png.data() + offset;
        const uint8_t* data = type + 4;
        const uint32_t storedCrc = readBigEndian32(data + chunkBytes);
        CHECK(storedCrc == referenceCrc32(type, size_t(chunkBytes) + 4));
        if (memcmp(type, "IHDR", 4) == 0) {
            CHECK(!sawHeader && chunkBytes == 13);
            sawHeader = true;
            CHECK(readBigEndian32(data) == width && readBigEndian32(data + 4) == height);
            CHECK(data[8] == bitDepth && data[9] == 0 && data[10] == 0 &&
                  data[11] == 0 && data[12] == 0);
        } else if (memcmp(type, "IDAT", 4) == 0) {
            CHECK(sawHeader && !sawEnd);
            sawImage = true;
            compressed.insert(compressed.end(), data, data + chunkBytes);
        } else if (memcmp(type, "IEND", 4) == 0) {
            CHECK(sawImage && chunkBytes == 0);
            sawEnd = true;
        } else {
            CHECK(false);
        }
        offset = chunkStart + size_t(chunkBytes) + 8;
        if (sawEnd) break;
    }
    CHECK(sawHeader && sawImage && sawEnd && offset == png.size());
    if (compressed.size() < 6) {
        CHECK(false);
        return;
    }
    CHECK(compressed[0] == 0x78 && compressed[1] == 0x01);
    std::vector<uint8_t> decoded;
    size_t deflateOffset = 2;
    bool finalBlock = false;
    while (!finalBlock && deflateOffset + 5 <= compressed.size() - 4) {
        const uint8_t header = compressed[deflateOffset++];
        finalBlock = (header & 1) != 0;
        CHECK((header & 0xFE) == 0);
        const uint16_t blockBytes = uint16_t(compressed[deflateOffset]) |
            (uint16_t(compressed[deflateOffset + 1]) << 8);
        const uint16_t inverseBytes = uint16_t(compressed[deflateOffset + 2]) |
            (uint16_t(compressed[deflateOffset + 3]) << 8);
        deflateOffset += 4;
        CHECK(uint16_t(~blockBytes) == inverseBytes);
        if (blockBytes > compressed.size() - 4 - deflateOffset) {
            CHECK(false);
            return;
        }
        decoded.insert(decoded.end(), compressed.begin() + deflateOffset,
                       compressed.begin() + deflateOffset + blockBytes);
        deflateOffset += blockBytes;
    }
    CHECK(finalBlock && deflateOffset + 4 == compressed.size());
    CHECK(decoded == expectedScanlines);
    CHECK(readBigEndian32(compressed.data() + deflateOffset) == referenceAdler32(decoded));

    size_t ignored = 0;
    CHECK(!presented_png::encoded_size(0, height, bitDepth, nullptr, &ignored));
    CHECK(!presented_png::encode(width, height, bitDepth, pixels.data(), pixels.size(),
                                 png.data(), png.size() - 1, &pngSize));
}

static void testPresentedPng() {
    checkPresentedPng(9, 2, 1, {0x80, 0x80, 0x01, 0x00},
                      {0, 0x80, 0x80, 0, 0x01, 0x00});
    checkPresentedPng(5, 2, 4, {0x12, 0x34, 0x50, 0x67, 0x89, 0xA0},
                      {0, 0x12, 0x34, 0x50, 0, 0x67, 0x89, 0xA0});
    std::vector<uint8_t> pixels(512 * 128);
    std::vector<uint8_t> scanlines;
    scanlines.reserve((512 + 1) * 128);
    for (uint16_t row = 0; row < 128; ++row) {
        scanlines.push_back(0);
        for (uint16_t column = 0; column < 512; ++column) {
            const uint8_t value = uint8_t(row * 17 + column * 31);
            pixels[size_t(row) * 512 + column] = value;
            scanlines.push_back(value);
        }
    }
    checkPresentedPng(4095, 128, 1, pixels, scanlines);
}

extern "C" unsigned long millis() { return clockMs; }
unsigned long micros() { return uint32_t(clockMs * 1000); }
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
        const uint16_t result = command == 0x0039 ? (badVcom ? 1000 : expectedVcom) :
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
    ++clockMs;
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
    expectedVcom = EPAPER_DEFAULT_VCOM_MV;
    waveformHook = uploadHook = nullptr;
}

static void paintReference(std::vector<uint8_t>& expected, const DeviceConfig& config, int rotation,
                           int x, int y, int width, int height, const std::vector<uint16_t>& source, int stride) {
    for (int row = 0; row < height; ++row) {
        for (int column = 0; column < width; ++column) {
            const int logicalX = x + column, logicalY = y + row;
            const uint16_t rgb565 = source[row * stride + column];
            const uint8_t red = ((rgb565 >> 11) & 31) * 255 / 31;
            const uint8_t green = ((rgb565 >> 5) & 63) * 255 / 63;
            const uint8_t blue = (rgb565 & 31) * 255 / 31;
            const uint8_t luminance = (red * 77 + green * 150 + blue * 29) >> 8;
            const uint8_t gray = config.epaper_render_mode == EPAPER_RENDER_MODE_BW ?
                (monochrome_is_white(luminance, logicalX, logicalY, config.epaper_bw_conversion,
                                     config.epaper_bw_threshold) ? 15 : 0) :
                (uint16_t(luminance) * 15 + 127) / 255;
            int nativeX = logicalX, nativeY = logicalY;
            if (rotation == 1) { nativeX = 1871 - logicalY; nativeY = logicalX; }
            if (rotation == 2) { nativeX = 1871 - logicalX; nativeY = 1403 - logicalY; }
            if (rotation == 3) { nativeX = logicalY; nativeY = 1403 - logicalX; }
            uint8_t& value = expected[nativeY * 936 + nativeX / 2];
            value = nativeX & 1 ? (value & 0xF0) | gray : (value & 0x0F) | (gray << 4);
        }
    }
}

static void checkConversion(int rotation, int mode, int conversion) {
    reset();
    DeviceConfig config{};
    config.epaper_render_mode = mode;
    config.epaper_bw_conversion = conversion;
    config.epaper_bw_threshold = 128;
    IT8951EpaperDriver driver(&config);
    driver.init();
    driver.setRotation(rotation);
    std::vector<uint8_t> expected(1872 * 1404 / 2, 0xFF);
    auto paint = [&](int x, int y, int width, int height, int stride, uint16_t seed) {
        std::vector<uint16_t> source(stride * height, 0x07E0);
        for (int row = 0; row < height; ++row) {
            for (int column = 0; column < width; ++column) {
                source[row * stride + column] = uint16_t(seed + row * width + column);
            }
        }
        driver.flushSrcStride = stride * sizeof(uint16_t);
        driver.setAddrWindow(x, y, width, height);
        driver.pushColors(source.data(), width * height, false);
        paintReference(expected, config, rotation, x, y, width, height, source, stride);
    };
    // Cover every RGB565 input, then overwrite odd/even nibble boundaries.
    paint(10, 12, 256, 256, 260, 0);
    paint(11, 13, 1, 3, 4, 0xFFFF);
    paint(12, 13, 3, 2, 4, 0x840E);
    paint(15, 16, 4, 3, 6, 0x7BE0);
    paint(16, 19, 4, 1, 4, 0);
    paint(driver.width() - 3, driver.height() - 2, 3, 2, 4, 0xFFFF);
    driver.present();
    if (IT8951_E1003_PROTOCOL) {
        for (int row = 0; row < 1404; ++row) {
            for (int word = 0; word < 234; ++word) {
                const int left = row * 936 + word * 2;
                const int right = row * 936 + (467 - word) * 2;
                std::swap(expected[left], expected[right]);
                std::swap(expected[left + 1], expected[right + 1]);
            }
        }
    }
    expected.insert(expected.begin(), {0, 0});
    int uploads = 0;
    for (const auto& transaction : transactions) {
        if (!transaction.imageData) continue;
        ++uploads;
        CHECK(transaction.bytes == expected);
    }
    CHECK(uploads == 1);
}

static uint8_t readPresentedGray(const std::vector<uint8_t>& pixels, size_t rowBytes,
                                uint16_t x, uint16_t y, uint8_t bitDepth) {
    const uint8_t value = pixels[size_t(y) * rowBytes +
        (bitDepth == 1 ? x / 8 : x / 2)];
    return bitDepth == 1 ? ((value >> (7 - (x & 7))) & 1) :
        ((x & 1) ? value & 0x0F : value >> 4);
}

static void checkPresentedCapture(uint8_t rotation, int mode) {
    reset();
    DeviceConfig config{};
    config.epaper_render_mode = mode;
    config.epaper_bw_threshold = 128;
    IT8951EpaperDriver driver(&config);
    driver.init();
    driver.setRotation(rotation);

    IT8951EpaperDriver::PresentedFrameInfo info;
    CHECK(!driver.getPresentedFrameInfo(info));
    std::vector<uint8_t> pixels(1872 * 1404 / 2);
    CHECK(!driver.copyPresentedFrame(pixels.data(), pixels.size()));
    pixel(driver, 0, 0, 0);
    pixel(driver, driver.width() - 1, 0, 0x8410);
    waveformHook = [&] {
        CHECK(!driver.getPresentedFrameInfo(info));
        CHECK(!driver.copyPresentedFrame(pixels.data(), pixels.size()));
    };
    driver.present();
    waveformHook = nullptr;

    CHECK(driver.getPresentedFrameInfo(info));
    CHECK(info.width == driver.width() && info.height == driver.height());
    CHECK(info.bitDepth == (mode == EPAPER_RENDER_MODE_BW ? 1 : 4));
    const size_t rowBytes = presented_frame_row_bytes(info.width, info.bitDepth);
    const size_t required = rowBytes * info.height;
    CHECK(required == presented_frame_bytes(info.width, info.height, info.bitDepth));
    CHECK(driver.copyPresentedFrame(pixels.data(), required));
    CHECK(readPresentedGray(pixels, rowBytes, 0, 0, info.bitDepth) == 0);

    const uint8_t red = ((0x8410 >> 11) & 31) * 255 / 31;
    const uint8_t green = ((0x8410 >> 5) & 63) * 255 / 63;
    const uint8_t blue = (0x8410 & 31) * 255 / 31;
    const uint8_t luminance = (red * 77 + green * 150 + blue * 29) >> 8;
    const uint8_t expected = mode == EPAPER_RENDER_MODE_BW ?
        (monochrome_is_white(luminance, driver.width() - 1, 0,
            MONOCHROME_CONVERSION_THRESHOLD, 128) ? 1 : 0) :
        (uint16_t(luminance) * 15 + 127) / 255;
    CHECK(readPresentedGray(pixels, rowBytes, info.width - 1, 0, info.bitDepth) == expected);

    if (rotation == 1) {
        pixel(driver, 1, 0, 0);
        CHECK(driver.copyPresentedFrame(pixels.data(), required));
        CHECK(readPresentedGray(pixels, rowBytes, 1, 0, info.bitDepth) ==
            (info.bitDepth == 1 ? 1 : 15));
        driver.present();
        CHECK(driver.copyPresentedFrame(pixels.data(), required));
        CHECK(readPresentedGray(pixels, rowBytes, 1, 0, info.bitDepth) == 0);
    }
}

int main() {
    testPresentedPng();
    for (int rotation : {0, 1, 2, 3}) {
        checkPresentedCapture(rotation, EPAPER_RENDER_MODE_BW);
        checkPresentedCapture(rotation, EPAPER_RENDER_MODE_GRAYSCALE);
        checkConversion(rotation, EPAPER_RENDER_MODE_GRAYSCALE, MONOCHROME_CONVERSION_THRESHOLD);
        for (int conversion : {MONOCHROME_CONVERSION_THRESHOLD, MONOCHROME_CONVERSION_BAYER,
                               MONOCHROME_CONVERSION_BLUE_NOISE}) {
            checkConversion(rotation, EPAPER_RENDER_MODE_BW, conversion);
        }
    }
    for (int mode : {EPAPER_RENDER_MODE_BW, EPAPER_RENDER_MODE_GRAYSCALE}) {
        reset();
        DeviceConfig config{};
        config.epaper_render_mode = mode;
        config.epaper_bw_threshold = 128;
        {
            IT8951EpaperDriver driver(&config);
#if HAS_DISPLAY_DIAGNOSTICS
            DisplayPresentationStats telemetry;
            CHECK(!driver.getPresentationStats(&telemetry));
#endif
            driver.init();
            CHECK(driver.isAvailable());
            CHECK(driver.presentationMode() == mode);
#if HAS_DISPLAY_DIAGNOSTICS
            CHECK(driver.getPresentationStats(&telemetry) && telemetry.attempts == 0);
#endif
            CHECK(delays.at(1) == (IT8951_E1003_PROTOCOL ? 10u : 200u));
            CHECK(arguments(0x0039).front() == std::vector<uint16_t>({IT8951_E1003_PROTOCOL ? 2u : 1u, EPAPER_DEFAULT_VCOM_MV}));
            if (!IT8951_E1003_PROTOCOL) CHECK(arguments(0x0011).front() == std::vector<uint16_t>({4, 1}));
            driver.setRotation(2);
            CHECK(driver.width() == 1872 && driver.height() == 1404);
            pixel(driver, 1, 2, 0);
            waveformHook = [] { clockMs += 25; };
            driver.present();
            waveformHook = nullptr;
            CHECK(arguments(0x0034).back() == std::vector<uint16_t>({0, 0, 1872, 1404, 2}));
#if HAS_DISPLAY_DIAGNOSTICS
            CHECK(driver.getPresentationStats(&telemetry));
            CHECK(telemetry.attempts == 1 && telemetry.full_updates == 1);
            CHECK(telemetry.last.success && telemetry.last.full && telemetry.last.waveform == 2);
            CHECK(telemetry.last.uploaded_bytes == 1872u * 1404 / 2);
            CHECK(telemetry.last.upload_us == 1404u * 1000);
            CHECK(telemetry.last.controller_us == 25000);
            CHECK(telemetry.last.coalesce_us == EPAPER_REFRESH_SETTLE_MS * 1000);
            CHECK(telemetry.last.total_us == telemetry.last.coalesce_us + telemetry.last.upload_us + 25000);
            CHECK(telemetry.last.dirty_to_ready_us == telemetry.last.total_us);
#endif
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
#if HAS_DISPLAY_DIAGNOSTICS
            CHECK(driver.getPresentationStats(&telemetry));
            CHECK(telemetry.partial_updates == 1 && telemetry.last.uploaded_bytes == 4);
            CHECK(telemetry.last.upload_us == 1000 && telemetry.last.waveform == waveform);
            CHECK(telemetry.last.width == 8 && telemetry.last.height == 1);
            const uint32_t completed = telemetry.last.completed_ms;
#endif
            const size_t unchanged = transactions.size();
            pixel(driver, 9, 2, 0);
            driver.present();
            CHECK(transactions.size() == unchanged);
#if HAS_DISPLAY_DIAGNOSTICS
            CHECK(driver.getPresentationStats(&telemetry) && telemetry.unchanged_skips == 1);
            CHECK(telemetry.attempts == 2 && telemetry.last.completed_ms == completed);
#endif
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
            IT8951EpaperDriver::PresentedFrameInfo failedFrame;
            CHECK(!driver.getPresentedFrameInfo(failedFrame));
#if HAS_DISPLAY_DIAGNOSTICS
            CHECK(driver.getPresentationStats(&telemetry));
            CHECK(telemetry.failures == 1 && !telemetry.last.success);
            CHECK(telemetry.last.hrdy_wait_us >= (IT8951_E1003_PROTOCOL ? 250u : 10000u) * 1000);
#endif
            busy = false;
            driver.present();
            CHECK(driver.getPresentedFrameInfo(failedFrame));
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
#if HAS_DISPLAY_DIAGNOSTICS
            CHECK(driver.getPresentationStats(&telemetry) && telemetry.ota_deferrals == 2);
#endif
            ota = false;
            uploadHook = nullptr;
            driver.present();
            CHECK(arguments(0x0034).back() == std::vector<uint16_t>({0, 0, 1872, 1404, 2}));
        }
    }
    reset();
    {
        IT8951EpaperDriver driver(nullptr);
        driver.init();
        clockMs = UINT32_MAX / 1000 - 100;
        std::vector<uint16_t> data(32 * 32, 0);
        driver.setAddrWindow(0, 0, 32, 32);
        driver.pushColors(data.data(), data.size(), false);
#if HAS_DISPLAY_DIAGNOSTICS
        DisplayPresentationStats telemetry;
        CHECK(driver.getPresentationStats(&telemetry));
        CHECK(telemetry.flush_calls == 1 && telemetry.flush_pixels == 1024);
        CHECK(telemetry.yield_us == 1000 && telemetry.conversion_us == 0);
#endif
        driver.present();
#if HAS_DISPLAY_DIAGNOSTICS
        CHECK(driver.getPresentationStats(&telemetry) && telemetry.attempts == 1);
        CHECK(telemetry.last.dirty_to_ready_us == telemetry.last.total_us + 1000);
#endif
    }
    for (uint16_t configuredVcom : {0, 1480, 5001}) {
        reset();
        DeviceConfig config{};
        config.epaper_vcom_mv = configuredVcom;
        expectedVcom = configuredVcom ? configuredVcom : EPAPER_DEFAULT_VCOM_MV;
        IT8951EpaperDriver driver(&config);
        driver.init();
        if (configuredVcom > EPAPER_VCOM_MAX_MV) {
            CHECK(!driver.isAvailable());
            CHECK(arguments(0x0039).empty());
        } else {
            CHECK(driver.isAvailable());
            CHECK(arguments(0x0039).front() ==
                std::vector<uint16_t>({IT8951_E1003_PROTOCOL ? 2u : 1u, expectedVcom}));
            config.epaper_vcom_mv = 1600;
            const size_t initializedTransactions = transactions.size();
            driver.present();
            CHECK(transactions.size() == initializedTransactions);
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
