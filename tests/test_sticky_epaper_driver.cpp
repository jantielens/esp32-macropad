#include "../src/app/board_config.h"
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
static uint32_t clockMs = 1000, resetAt = 0, busyUntil = 0;
static bool ssd2677 = true, disconnected = false, stuck = false, ota = false, boardReady = true;
static int pins[49] = {}, failures = 0, liveAllocations = 0, allocations = 0, failAllocation = 0;
static int liveMutexes = 0, spiBuses = 0;
static bool failMutex = false;
static bool drivePowered = false, powerPinConfigured = false;
static uint8_t probeId = 7, temperature = 25;
static std::function<void()> streamHook, waitHook;
static uint32_t sequence = 0;
static EpaperRefreshSettings settings = {EPAPER_RENDER_MODE_BW, 1000, 500, true, 2, 0, 128};
#define CHECK(condition) do { if (!(condition)) { std::printf("FAIL %d: %s\n", __LINE__, #condition); ++failures; } } while (0)

extern "C" unsigned long millis() { return clockMs; }
int64_t esp_timer_get_time() { return uint64_t(clockMs) * 1000; }
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
    fixture.publisher.interactionChanged(uint64_t(clockMs) * 1000);
    lv_refr_now(display);
    CHECK(fixture.flushes > 1);
    fixture.driver.present();
    CHECK(modes().size() == 1);
    CHECK(snapshotPixel(fixture.driver, 0, 0) == 0 && snapshotPixel(fixture.driver, 799, 479) == 0);

    // Same production outer-boundary helper: new content need not wait for the
    // display timer. Background work and blocked (sleep/OTA/navigation) work do.
    const unsigned initialFlushes = fixture.flushes;
    lv_obj_set_style_bg_color(lv_screen_active(), lv_color_white(), 0);
    CHECK(!fixture.publisher.renderIfNeeded(fixture.driver, display, false));
    lv_timer_handler();
    CHECK(fixture.flushes == initialFlushes);
    fixture.publisher.interactionChanged(uint64_t(clockMs) * 1000);
    CHECK(!fixture.publisher.renderIfNeeded(fixture.driver, display, true));
    CHECK(fixture.flushes == initialFlushes);
    CHECK(fixture.publisher.renderIfNeeded(fixture.driver, display, false));
    CHECK(fixture.flushes > initialFlushes);
    CHECK(!fixture.publisher.needsPromptRender());
    fixture.driver.present();
    CHECK(snapshotPixel(fixture.driver, 799, 479) == 1);
    const unsigned noOpFlushes = fixture.flushes;
    fixture.publisher.interactionChanged(uint64_t(clockMs) * 1000);
    CHECK(fixture.publisher.renderIfNeeded(fixture.driver, display, false));
    CHECK(!fixture.publisher.needsPromptRender() && fixture.flushes == noOpFlushes);
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

int main(int argc, char** argv) {
    if (argc == 2 && !strcmp(argv[1], "--benchmark")) {
        benchmarkConversion();
        benchmarkPanelPacking();
        return failures ? 1 : 0;
    }
    coherentLvglFrame();
    conversionAndWireEquivalence();
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
                CHECK(frame(controller2677 ? 0x10 : gray ? 0x24 : 0x26).size() == size_t(controller2677 ? 200 : 100));
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
