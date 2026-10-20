#!/usr/bin/env python3
"""Compile production StopWatch HAL against deterministic peripheral doubles."""
import pathlib
import re
import shutil
import subprocess

ROOT = pathlib.Path(__file__).resolve().parents[1]


def production(path):
    return re.sub(r"^\s*#include[^\n]*", "", (ROOT / path).read_text(), flags=re.M)


PRELUDE = r'''
#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <map>
#include <math.h>
#include <new>
#include <string>
#include <tuple>
#include <vector>
using std::min;
#define HAS_SENSOR_BATTERY_ADC false
#define HAS_MQTT false
#define HAS_BLE false
#define HAS_ES7210_MIC false
#define HAS_AUDIO_INPUT false
#define HAS_SOUND_PLAYER true
#define HAS_MUSIC_ANALYSIS false
#define HAS_AUDIO true
#define AUDIO_OUTPUT_DRIVER_ES8311 1
#define AUDIO_OUTPUT_DRIVER_PCM510XA 2
#define AUDIO_OUTPUT_DRIVER AUDIO_OUTPUT_DRIVER_ES8311
#define AUDIO_PA_PIN 27
#define AUDIO_PA_ACTIVE_LOW false
#define AUDIO_MIC_ADC_ADDR 0x40
#define AUDIO_I2S_DIN 20
#define DISPLAY_DRIVER 1
#define DISPLAY_DRIVER_ARDUINO_GFX_CO5300 14
#define TOUCH_DRIVER 1
#define TOUCH_DRIVER_CST820B_WIRE 8
#define AUDIO_CODEC_ADDR 0x18
#define AUDIO_SAMPLE_RATE 48000
#define AUDIO_I2S_MCLK 18
#define AUDIO_I2S_BCLK 17
#define AUDIO_I2S_LRCK 15
#define AUDIO_I2S_DOUT 21
#define AUDIO_DMA_DESC_NUM 6
#define AUDIO_DMA_FRAME_NUM 240
#define AUDIO_QUEUE_DEPTH 4
#define MUSIC_WORK_QUEUE_DEPTH 4
#define AUDIO_TASK_STACK_SIZE 24576
#define portMAX_DELAY 1000
#define DISPLAY_WIDTH 466
#define DISPLAY_HEIGHT 466
#define DISPLAY_ROTATION 0
#define TOUCH_INT 13
#define LVGL_BUFFER_SIZE (466 * 16)
#define LCD_QSPI_CS 39
#define LCD_QSPI_PCLK 40
#define LCD_QSPI_D0 41
#define LCD_QSPI_D1 42
#define LCD_QSPI_D2 46
#define LCD_QSPI_D3 45
#define TFT_SPI_FREQ_HZ 80000000
#define STOPWATCH_CHARGING_MODE 2
#define INPUT_PULLUP 2
#define OUTPUT 1
#define HIGH 1
#define LOW 0
#define FALLING 3
#define IRAM_ATTR
#define RGB565_BLACK 0
#define GFX_NOT_DEFINED -1
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define LOGE(...) ((void)0)
#define LOGW(...) ((void)0)
#define LOGI(...) ((void)0)
#define LOGT(...) ((void)0)
using i2s_mclk_multiple_t = int;
#define I2S_MCLK_MULTIPLE_256 256
using esp_err_t = int;
using gpio_num_t = int;
using i2s_chan_handle_t = int*;
#define ESP_OK 0
#define ESP_ERR_TIMEOUT 2
#define I2S_NUM_0 0
#define I2S_ROLE_MASTER 0
#define I2S_DATA_BIT_WIDTH_16BIT 16
#define I2S_SLOT_BIT_WIDTH_32BIT 32
#define I2S_SLOT_MODE_STEREO 2
#define I2S_GPIO_UNUSED -1
struct i2s_chan_config_t {
    bool auto_clear = false;
    int dma_desc_num = 0, dma_frame_num = 0;
};
struct i2s_std_clk_config_t { int rate = 0, mclk_multiple = 0; };
struct i2s_std_slot_config_t { int width = 16, slot_bit_width = 16, ws_width = 16; };
struct i2s_std_gpio_config_t {
    int mclk, bclk, ws, dout, din;
    struct { bool mclk_inv, bclk_inv, ws_inv; } invert_flags;
};
struct i2s_std_config_t {
    i2s_std_clk_config_t clk_cfg;
    i2s_std_slot_config_t slot_cfg;
    i2s_std_gpio_config_t gpio_cfg;
};
#define I2S_CHANNEL_DEFAULT_CONFIG(...) i2s_chan_config_t{}
#define I2S_STD_CLK_DEFAULT_CONFIG(rate) i2s_std_clk_config_t{int(rate),0}
#define I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(...) i2s_std_slot_config_t{}
static int channel = 0, receiveChannel = 0, liveChannels = 0;
static bool channelFails = false, channelWriteFails = false;
static bool channelInitFails = false, channelEnableFails = false, channelReadFails = false;
static bool channelWriteShort = false, testingPcm = false, txEnabled = false;
static std::vector<std::string> audioEvents;
static bool lifecycleTesting = false, silentFrameWritten = false;
static esp_err_t i2s_new_channel(i2s_chan_config_t* config, i2s_chan_handle_t* tx, i2s_chan_handle_t* rx) {
    assert(config->auto_clear);
    if (channelFails) return 1;
    *tx = &channel; ++liveChannels;
    if (rx) { *rx = &receiveChannel; ++liveChannels; }
    return ESP_OK;
}
static esp_err_t i2s_channel_init_std_mode(i2s_chan_handle_t, i2s_std_config_t* config) {
    assert(config->clk_cfg.rate == 48000);
    if (testingPcm) {
        assert(config->gpio_cfg.mclk == I2S_GPIO_UNUSED);
        assert(config->slot_cfg.slot_bit_width == 32 && config->slot_cfg.ws_width == 32);
    } else assert(config->clk_cfg.mclk_multiple == 256);
    if (channelInitFails) return 1;
    return ESP_OK;
}
static esp_err_t i2s_channel_enable(i2s_chan_handle_t handle) {
    if (channelEnableFails) return 1;
    audioEvents.push_back(handle == &channel ? "enableTX" : "enableRX");
    if (handle == &channel) txEnabled = true;
    return ESP_OK;
}
static esp_err_t i2s_channel_disable(i2s_chan_handle_t handle) {
    audioEvents.push_back(handle == &channel ? "disableTX" : "disableRX");
    if (handle == &channel) txEnabled = false;
    return ESP_OK;
}
static esp_err_t i2s_del_channel(i2s_chan_handle_t) {
    audioEvents.push_back("delete"); --liveChannels; return ESP_OK;
}
static esp_err_t i2s_channel_read(i2s_chan_handle_t, void* frames, size_t count, size_t* read, int) {
    if (channelReadFails) return ESP_ERR_TIMEOUT;
    memset(frames, 0, count); *read = count; return ESP_OK;
}
static esp_err_t i2s_channel_write(i2s_chan_handle_t, const void* frames, size_t count,
                                 size_t* written, int) {
    if (channelWriteFails) return 1;
    const auto* bytes = static_cast<const uint8_t*>(frames);
    if (!testingPcm) {
        assert(count == 128);
        for (size_t i = 0; i < count; ++i) assert(bytes[i] == 0);
    }
    silentFrameWritten = true;
    audioEvents.push_back("write");
    *written = channelWriteShort ? count - 1 : count; return ESP_OK;
}
using lv_display_t = int;
static std::vector<int> delays;
static unsigned long clockMs = 0;
static unsigned long totalDelayMs = 0;
static unsigned long millis() { return clockMs; }
static bool otaActive = false;
static bool ota_activity_try_begin() {
    if (otaActive) return false;
    otaActive = true; return true;
}
static void ota_activity_finish() { assert(otaActive); otaActive = false; }
static std::vector<uint32_t> failedShutdowns;
static bool action_continuation_complete(uint32_t token, bool success) {
    assert(!success); failedShutdowns.push_back(token); return true;
}
static int audioStops = 0;
static void audio_stop() { ++audioStops; }
static void delay(int value) {
    delays.push_back(value); totalDelayMs += value;
    if (txEnabled) audioEvents.push_back("clockedDelay");
}
static void vTaskDelay(int value) { delay(value); }
static void pinMode(int pin, int mode) {
    assert(((pin == 13 || pin == 38) && mode == INPUT_PULLUP) ||
           ((pin == 18 || pin == 27) && mode == OUTPUT));
}
static void digitalWrite(int pin, int value) {
    if (pin == 18) { assert(value == LOW); audioEvents.push_back("sckLow"); }
    else { assert(pin == 27); audioEvents.push_back(value == LOW ? "mute" : "unmute"); }
}
static int irq = 1;
static int digitalRead(int pin) { assert(pin == 13); return irq; }
static int digitalPinToInterrupt(int pin) { return pin; }
static void attachInterrupt(int pin, void (*)(), int mode) { assert(pin == 13 && mode == FALLING); }
static void detachInterrupt(int pin) { assert(pin == 13); }
static bool allocationFails = false;
static void* heap_caps_malloc(size_t size, int caps) {
    assert(caps == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    return allocationFails ? nullptr : malloc(size);
}
static void heap_caps_free(void* pointer) { free(pointer); }

using SemaphoreHandle_t = int*;
static int semaphore = 0, lockDepth = 0, mutexCreates = 0;
static bool mutexFails = false, lockFails = false;
static SemaphoreHandle_t xSemaphoreCreateMutex() {
    ++mutexCreates;
    return mutexFails ? nullptr : &semaphore;
}
static constexpr int pdTRUE = 1;
static int xSemaphoreTake(SemaphoreHandle_t, int) {
    if (lockFails) return 0;
    assert(lockDepth == 0);
    ++lockDepth;
    return pdTRUE;
}
static void xSemaphoreGive(SemaphoreHandle_t) { assert(lockDepth == 1); --lockDepth; }
using TickType_t = int;
#define pdMS_TO_TICKS(x) (x)
void i2c_bus_init();
bool i2c_bus_lock(TickType_t timeout = 50);
void i2c_bus_unlock();

struct MockWire {
    std::array<std::array<uint8_t, 256>, 128> regs{};
    std::vector<std::tuple<int, int, int>> writes;
    std::vector<std::tuple<int, int, int>> operations;
    std::vector<uint8_t> tx, rx;
    int address = 0, cursor = 0, begins = 0, transactions = 0, failureAt = -1;
    bool primary = true, fallback = false, pmic = true, shortRead = false, shortWrite = false;
    bool failCodecVolume = false;
    bool failAmpEnable = false;
    bool expanderProbeFails = false;
    int expanderReadHz = 0, frequency = 100000;
    std::vector<int> clockChanges;
    std::vector<std::pair<int, int>> identityReads;
    unsigned long expanderStartupMs = 0, railsEnabledAt = 0, panelResetReleasedAt = 0;
    bool begin(int sda, int scl, int hz = 100000) {
        assert(lockDepth == 1 && sda == 47 && scl == 48 && hz == 100000);
        ++begins; return true;
    }
    bool setClock(uint32_t hz) {
        assert(lockDepth == 1 && (hz == 100000 || hz == 400000));
        frequency = hz; clockChanges.push_back(hz); return true;
    }
    void beginTransmission(uint8_t addr) {
        assert(lockDepth == 1); address = addr; tx.clear();
    }
    size_t write(uint8_t value) {
        assert(lockDepth == 1); tx.push_back(value);
        return shortWrite ? 0 : 1;
    }
    int endTransmission(bool = true) {
        assert(lockDepth == 1);
        const int index = transactions++;
        operations.emplace_back(address, tx.empty() ? -1 : tx[0], tx.size() == 2 ? tx[1] : -1);
        if (index == failureAt || (address == 0x4f && !primary) ||
            (address == 0x6f && !fallback) || (address == 0x6e && !pmic)) return 4;
        if ((address == 0x4f || address == 0x6f) && (regs[0x6e][6] & 6) != 6) return 4;
        if ((address == 0x4f || address == 0x6f) &&
            totalDelayMs - railsEnabledAt < expanderStartupMs) return 2;
        if ((address == 0x4f || address == 0x6f) && expanderProbeFails && tx.empty()) return 2;
        if ((address == 0x4f || address == 0x6f) && expanderReadHz && frequency != expanderReadHz) return 2;
        if (failCodecVolume && address == 0x18 && tx.size() == 2 && tx[0] == 0x32) return 4;
        if (lifecycleTesting && address == 0x4f && tx.size() == 2 && tx[0] == 6 && (tx[1] & 2))
            assert(silentFrameWritten);
        if (failAmpEnable && address == 0x4f && tx.size() == 2 && tx[0] == 6 && (tx[1] & 2)) return 4;
        if (tx.size() == 2 && !shortWrite) {
            if (address == 0x6e && tx[0] == 6 && (regs[address][6] & 6) != 6 && (tx[1] & 6) == 6)
                railsEnabledAt = totalDelayMs;
            if ((address == 0x4f || address == 0x6f) && tx[0] == 5 &&
                !(regs[address][5] & 0x10) && (tx[1] & 0x10))
                panelResetReleasedAt = totalDelayMs;
            regs[address][tx[0]] = tx[1];
            writes.emplace_back(address, tx[0], tx[1]);
        }
        return 0;
    }
    size_t requestFrom(uint8_t addr, uint8_t length) {
        assert(lockDepth == 1 && addr == address && tx.size() == 1);
        if ((addr == 0x4f || addr == 0x6f) && tx[0] == 0 && length == 2)
            identityReads.emplace_back(addr, frequency);
        rx.clear(); cursor = 0;
        for (int i = 0; i < (shortRead ? length - 1 : length); ++i)
            rx.push_back(regs[addr][tx[0] + i]);
        return rx.size();
    }
    int available() { return rx.size() - cursor; }
    int read() {
        assert(lockDepth == 1);
        return cursor < int(rx.size()) ? rx[cursor++] : -1;
    }
} Wire;

static std::vector<std::pair<int,int>> commands;
static std::vector<uint16_t> drawn;
static std::array<int,4> rectangle{}, offsets{};
static bool gfxBegins = true;
static int panelBrightness = -1;
class Arduino_DataBus {
public:
    virtual ~Arduino_DataBus() = default;
    void sendCommand(int command) {
        if (command == 0x11) assert(totalDelayMs - Wire.panelResetReleasedAt >= 120);
        commands.emplace_back(command, -1);
    }
    void beginWrite() {}
    void endWrite() {}
    void writeCommand(int command) { sendCommand(command); }
    void writeC8D8(int command, int value) { commands.emplace_back(command, value); }
    void writeC8D16(int command, int value) { commands.emplace_back(command, value); }
};
class Arduino_ESP32QSPI : public Arduino_DataBus {
public:
    Arduino_ESP32QSPI(int cs, int clk, int d0, int d1, int d2, int d3) {
        assert(cs == 39 && clk == 40 && d0 == 41 && d1 == 42 && d2 == 46 && d3 == 45);
    }
};
class Arduino_CO5300 {
protected:
    Arduino_DataBus* _bus;
    virtual void tftInit() {}
    bool sleeping = false;
public:
    Arduino_CO5300(Arduino_DataBus* bus, int rst, int rotation, int w, int h,
                   int x1, int y1, int x2, int y2) : _bus(bus) {
        assert(rst == -1 && rotation == 0 && w == 468 && h == 466);
        offsets = {x1,y1,x2,y2};
    }
    virtual ~Arduino_CO5300() = default;
    bool begin(int hz) {
        assert(hz == 80000000);
        if (!gfxBegins) return false;
        tftInit(); return true;
    }
    void fillScreen(int color) { assert(color == 0); }
    void setBrightness(int value) { panelBrightness = value; }
    void displayOff() {
        _bus->sendCommand(0x28); delay(120);
        _bus->sendCommand(0x10); delay(120);
        sleeping = true;
    }
    void displayOn() {
        _bus->sendCommand(0x29); delay(120);
        _bus->sendCommand(0x11); delay(120);
        sleeping = false;
    }
    void draw16bitRGBBitmap(int x, int y, uint16_t* pixels, int w, int h) {
        if (sleeping) return;
        assert(((x + offsets[0]) & 1) == 0 && (w & 1) == 0);
        assert((y & 1) == 0 && (h & 1) == 0);
        assert(x >= 0 && x + w <= 468 && y >= 0 && y + h <= 466);
        rectangle = {x,y,w,h};
        drawn.assign(pixels, pixels + w*h);
    }
};

struct JsonValue {
    bool null = true;
    double value = 0;
    template<typename T> JsonValue& operator=(T v) { null = false; value = v; return *this; }
    JsonValue& operator=(std::nullptr_t) { null = true; return *this; }
};
struct JsonObject {
    std::map<std::string, JsonValue> values;
    JsonValue& operator[](const char* key) { return values[key]; }
};
struct SensorField {
    const char* key;
    const char* summary;
};
struct SensorCallbacks {
    const char* name = nullptr;
    void (*init)() = nullptr;
    void (*sample)(JsonObject&) = nullptr;
    const SensorField* fields = nullptr;
    size_t field_count = 0;
};
class SensorRegistry {
public:
    SensorCallbacks callbacks;
    bool add(const SensorCallbacks& value) { callbacks = value; return true; }
};
static void sensor_manager_set_number(JsonObject& doc, const char* key, float value, bool valid) {
    if (valid) doc[key] = value; else doc[key] = nullptr;
}
static void sensor_manager_set_bool(JsonObject& doc, const char* key, bool value, bool valid) {
    if (valid) doc[key] = value; else doc[key] = nullptr;
}
'''

BODY = "\n".join(production(path) for path in [
    "src/app/touch_sample.h",
    "src/app/touch_driver.h",
    "src/app/display_driver.h",
    "src/boards/m5stack-stopwatch/stopwatch_hardware.h",
    "src/app/audio_output_driver.h",
    "src/app/i2c_bus.cpp",
    "src/app/drivers/wire_cst820b_touch_driver.h",
    "src/app/drivers/arduino_gfx_co5300_driver.h",
    "src/boards/m5stack-stopwatch/stopwatch_hardware.cpp",
    "src/app/drivers/wire_cst820b_touch_driver.cpp",
    "src/app/drivers/arduino_gfx_co5300_driver.cpp",
    "src/app/sensors/battery_adc_sensor.cpp",
    "src/boards/m5stack-stopwatch/stopwatch_battery_sensor.cpp",
])

audio = production("src/app/drivers/es8311_audio_driver.cpp")
BODY += "\nAudioBoardControl audio_board_control_create() { return stopwatch_audio_control(); }\n"
BODY += production("src/app/drivers/es8311_audio_driver.h").replace("private:", "public:")
BODY += audio[audio.index("#define TAG"):audio.index("#if HAS_ES7210_MIC")]
BODY += audio[audio.index("struct ES8311Coeff"):
              audio.index("#if HAS_ES7210_MIC", audio.index("static bool es8311_write"))]
BODY += audio[audio.index("bool ES8311AudioDriver::initCodec"):
              audio.index("// The microphone shares")]
BODY += audio[audio.index("bool ES8311AudioDriver::cleanup"):]
LIFECYCLE_DOUBLES = r'''
static AudioOutputDriver* output_driver = nullptr;
static uint8_t current_volume = 0;
static bool audio_initialized = false;
static bool output_muted = true;
static int64_t output_last_write_us = 0;
static int* audio_queue = nullptr;
static int* music_work_queue = nullptr;
using TaskHandle_t = int*;
static std::atomic<TaskHandle_t> audio_task_handle{nullptr};
static uint32_t uxTaskGetStackHighWaterMark(TaskHandle_t) { return 512; }
static SemaphoreHandle_t g_music_catalog_refresh_sem = nullptr;
static int g_music_catalog_refresh_sem_storage = 0;
struct AudioCommand { int value; };
struct MusicWorkCommand { int value; };
static int queueCreates = 0, liveQueues = 0, queueFailureAt = -1;
static bool semaphoreFails = false, taskFails = false;
static int* xQueueCreate(int, size_t) {
    if (queueCreates++ == queueFailureAt) return nullptr;
    ++liveQueues; return new int(0);
}
static void vQueueDelete(int* queue) { assert(queue); --liveQueues; delete queue; }
static SemaphoreHandle_t xSemaphoreCreateBinaryStatic(int* storage) {
    return semaphoreFails ? nullptr : storage;
}
static void vSemaphoreDelete(SemaphoreHandle_t) {}
static bool music_catalog_store_init() { return true; }
static void audio_task(void*) {}
static bool rtos_create_task_internal_stack_pinned(
    void (*)(void*), const char*, int, void*, int, int** handle, void*, int) {
    if (taskFails) return false;
    *handle = &channel; return true;
}
static void device_telemetry_log_memory_snapshot(const char*) {}
'''
BODY += LIFECYCLE_DOUBLES
audio_lifecycle = production("src/app/audio.cpp")
LIFECYCLE_CODE = audio_lifecycle[audio_lifecycle.index("static void audio_init_abort",
                                             audio_lifecycle.index("// Public API")):
                        audio_lifecycle.index("void audio_set_volume")]
BODY += LIFECYCLE_CODE

GENERIC_TESTS = r'''
static void clear_failures() {
    queueFailureAt = -1; queueCreates = 0;
    semaphoreFails = taskFails = channelFails = channelWriteFails = false;
    channelInitFails = channelEnableFails = channelReadFails = channelWriteShort = false;
    Wire.failCodecVolume = false;
}
int main() {
    assert(board_hardware_init() && Wire.begins == 0);
    const AudioBoardControl control = audio_board_control_create();
    assert(control.mute && !control.power);
    i2c_bus_init();
    assert(i2c_bus_lock()); Wire.begin(47,48,100000); i2c_bus_unlock();
#if AUDIO_OUTPUT_DRIVER == AUDIO_OUTPUT_DRIVER_PCM510XA
    testingPcm = true;
#endif
    for (int failure = -1; failure < 10; ++failure) {
        clear_failures(); audioEvents.clear(); silentFrameWritten = false;
        audio_initialized = false;
        if (failure == 0) queueFailureAt = 0;
        if (failure == 1) queueFailureAt = 1;
        if (failure == 2) semaphoreFails = true;
        if (failure == 3) taskFails = true;
        if (failure == 4) channelFails = true;
        if (failure == 5) channelInitFails = true;
        if (failure == 6) channelEnableFails = true;
#if AUDIO_OUTPUT_DRIVER == AUDIO_OUTPUT_DRIVER_ES8311
        if (failure == 7) Wire.failCodecVolume = true;
        if (failure == 8) channelWriteFails = true;
        if (failure == 9) channelWriteShort = true;
#else
        if (failure >= 7) continue;
#endif
        audio_init(30);
        if (failure >= 0) {
            assert(!audio_initialized && liveChannels == 0 && liveQueues == 0);
            assert(audio_queue == nullptr && music_work_queue == nullptr);
            assert(std::find(audioEvents.begin(), audioEvents.end(), "mute") != audioEvents.end());
            clear_failures();
            audio_init(30);
        }
        assert(audio_initialized && output_driver->isAvailable());
        assert(liveChannels == (HAS_AUDIO_INPUT ? 2 : 1) && liveQueues == 2);
        const auto enabled = std::find(audioEvents.begin(), audioEvents.end(), "enableTX");
        const auto unmuted = std::find(audioEvents.begin(), audioEvents.end(), "unmute");
        assert(enabled != audioEvents.end() && unmuted > enabled);
        const int creates = queueCreates;
        audio_init(30); assert(queueCreates == creates);
#if HAS_AUDIO_INPUT
        AudioInputDriver* input = audio_input_driver_create();
        assert(input && input->inputAvailable());
        assert(input->inputFormat().sample_rate == 48000 && input->inputFormat().channels == 2);
        assert(input->captureStart());
        const size_t events = audioEvents.size();
        assert(input->captureStart() && audioEvents.size() == events);
        int16_t frames[64] = {};
        assert(input->readFrames(frames, 32, 5) == 32);
        channelReadFails = true;
        assert(input->readFrames(frames, 32, 5) == 0);
        channelReadFails = false;
        input->captureStop();
        assert(input->captureStart());
#endif
        audioEvents.clear(); audio_init_abort(); audio_initialized = false;
        assert(liveChannels == 0 && liveQueues == 0 && !output_driver->isAvailable());
        const auto muted = std::find(audioEvents.begin(), audioEvents.end(), "mute");
        const auto disabled = std::find(audioEvents.begin(), audioEvents.end(), "disableTX");
        assert(muted < disabled);
#if AUDIO_OUTPUT_DRIVER == AUDIO_OUTPUT_DRIVER_PCM510XA
        const auto settled = std::find(audioEvents.begin(), audioEvents.end(), "clockedDelay");
        assert(muted < settled && settled < disabled);
#endif
        assert(output_driver->end());
    }
#if AUDIO_OUTPUT_DRIVER == AUDIO_OUTPUT_DRIVER_PCM510XA
    PCM510xADriver missingMute(AudioBoardControl{});
    assert(!missingMute.begin(48000) && liveChannels == 0);
    AudioBoardControl powered = control;
    powered.power = [](bool enabled) {
        audioEvents.push_back(enabled ? "powerOn" : "powerOff"); return true;
    };
    PCM510xADriver driver(powered);
    assert(driver.begin(48000));
    assert(std::find(audioEvents.begin(), audioEvents.end(), "sckLow") != audioEvents.end());
    assert(driver.setVolume(100) && driver.setMuted(false));
    int16_t frames[64] = {};
    assert(driver.write(frames, 32));
    channelWriteShort = true; assert(!driver.write(frames, 32)); channelWriteShort = false;
    audioEvents.clear(); assert(driver.end());
    const auto disabled = std::find(audioEvents.begin(), audioEvents.end(), "disableTX");
    const auto poweredOff = std::find(audioEvents.begin(), audioEvents.end(), "powerOff");
    assert(disabled < poweredOff);
#endif
    assert(lockDepth == 0);
}
'''

TESTS = r'''
static void reset() {
    Wire = MockWire{};
    expander = 0; wireStarted = ready = audioPowered = false;
    motorRunning = false; motorDeadline = 0; clockMs = 0;
    motorStepCount = motorStepIndex = 0;
    shutdownToken = 0; shutdownCommandSent = false; shutdownDeadline = 0;
    otaActive = false; failedShutdowns.clear(); audioStops = 0;
    delays.clear(); commands.clear(); drawn.clear();
    totalDelayMs = 0;
    allocationFails = false; gfxBegins = true; irq = 1;
    lockFails = mutexFails = false;
    // Preserve unrelated outputs and PMIC configuration to exercise RMW.
    for (int address : {0x4f, 0x6f}) {
        Wire.regs[address][0x05] = 0xff;
        Wire.regs[address][0x06] = 0xff;
        Wire.regs[address][0x13] = 0xff;
        Wire.regs[address][0x14] = 0xff;
        // Warm boot after vendor firmware: all PWM channels still enabled.
        Wire.regs[address][0x1b] = 0x55;
        Wire.regs[address][0x1c] = 0xcb;
        Wire.regs[address][0x1d] = 0x66;
        Wire.regs[address][0x1e] = 0x87;
        Wire.regs[address][0x1f] = 0x77;
        Wire.regs[address][0x20] = 0x89;
        Wire.regs[address][0x21] = 0x88;
        Wire.regs[address][0x22] = 0xc2;
        Wire.regs[address][0x25] = 0xa5;
        Wire.regs[address][0x26] = 0xab;
    }
    Wire.regs[0x6e][0x06] = 0xe8; // Cold battery boot: LDO/DCDC initially OFF.
    Wire.regs[0x6e][0x07] = 0xd5;
    Wire.regs[0x6e][0x0b] = 0xa5;
    Wire.regs[0x6e][0x11] = 0xff;
    Wire.regs[0x6e][0x13] = 0xff;
    Wire.regs[0x6e][0x10] = 0xf7;
    Wire.regs[0x6e][0x14] = 0xff;
    Wire.regs[0x6e][0x16] = 0xff;
    Wire.regs[0x6e][0x49] = 0xd8;
    Wire.regs[0x6e][0x4a] = 0xa5;
    assert(lockDepth == 0);
}
static void battery(int vbat, int vin, uint8_t gpio) {
    Wire.regs[0x6e][0x22] = vbat;
    Wire.regs[0x6e][0x23] = vbat >> 8;
    Wire.regs[0x6e][0x24] = vin;
    Wire.regs[0x6e][0x25] = vin >> 8;
    Wire.regs[0x6e][0x12] = gpio;
}
static void contact(int count, int event, int x, int y) {
    Wire.regs[0x15][2] = count;
    Wire.regs[0x15][3] = (event << 6) | (x >> 8);
    Wire.regs[0x15][4] = x;
    Wire.regs[0x15][5] = y >> 8;
    Wire.regs[0x15][6] = y;
}
int main() {
    mutexFails = true;
    assert(!m5stack_stopwatch_init() && !wireStarted && Wire.begins == 0);
    mutexFails = false;
    i2c_bus_init();
    const int created = mutexCreates;
    i2c_bus_init();
    assert(mutexCreates == created);
    reset();
    assert(m5stack_stopwatch_init());
    assert(totalDelayMs - Wire.panelResetReleasedAt >= 120);
    assert(delays == std::vector<int>({10,10,10,200}));
    assert(Wire.regs[0x4f][5] == 0xfb); // audio OFF, USB/OLED/reset HIGH.
    assert(Wire.regs[0x4f][6] == 0xfc); // PA and motor OFF.
    assert(Wire.regs[0x4f][3] == 0x9d);
    assert(Wire.regs[0x4f][4] == 3);
    assert(Wire.regs[0x4f][0x13] == 0x62);
    assert(Wire.regs[0x4f][0x14] == 0xfc);
    assert(Wire.regs[0x4f][0x1c] == 0x4b);
    assert(Wire.regs[0x4f][0x1e] == 0x07);
    assert(Wire.regs[0x4f][0x22] == 0x42);
    assert(Wire.regs[0x4f][0x1b] == 0x55 && Wire.regs[0x4f][0x1d] == 0x66);
    assert(Wire.regs[0x4f][0x1f] == 0x77 && Wire.regs[0x4f][0x20] == 0x89);
    assert(Wire.regs[0x4f][0x21] == 0x88);
    assert(Wire.regs[0x4f][0x25] == 0xa5 && Wire.regs[0x4f][0x26] == 0xab);
    assert(Wire.regs[0x6e][6] == 0xf7); // boost OFF; other bits preserved.
    assert(Wire.regs[0x6e][0x07] == 0xf5);
    assert(Wire.regs[0x6e][0x49] == 0xdf && Wire.regs[0x6e][0x4a] == 0xa5);
    assert(Wire.regs[0x6e][0x0b] == 0xa5 && Wire.regs[0x6e][0x11] == 0xf7);
    assert(Wire.regs[0x6e][0x13] == 0xf7);
    assert(Wire.regs[0x6e][0x10] == 0xfb && Wire.regs[0x6e][0x14] == 0x0f);
    assert(Wire.regs[0x6e][0x16] == 0x0f);
    int railsEnabled = -1, firstExpanderAccess = -1;
    int chargeLatchLow = -1, chargeOutputEnabled = -1;
    for (size_t i = 0; i < Wire.operations.size(); ++i) {
        const auto [address, reg, value] = Wire.operations[i];
        if (address == 0x6e && reg == 6 && value == 0xf6 && railsEnabled < 0) railsEnabled = i;
        if (address == 0x6e && reg == 0x11 && value == 0xf7 && chargeLatchLow < 0) chargeLatchLow = i;
        if (address == 0x6e && reg == 0x10 && value == 0xfb) chargeOutputEnabled = i;
        if ((address == 0x4f || address == 0x6f) && firstExpanderAccess < 0)
            firstExpanderAccess = i;
    }
    assert(railsEnabled >= 0 && firstExpanderAccess > railsEnabled);
    assert(chargeLatchLow >= 0 && chargeOutputEnabled > chargeLatchLow);
    const auto successfulWrites = Wire.writes;
    const int count = Wire.transactions;
    assert(m5stack_stopwatch_init() && Wire.begins == 1 && Wire.transactions == count);
    for (auto mode : {StopWatchChargingMode::Off, StopWatchChargingMode::Low, StopWatchChargingMode::High}) {
        Wire.writes.clear();
        assert(m5stack_stopwatch_set_charging_mode(mode));
        assert(lockDepth == 0 && (Wire.regs[0x6e][6] & 0xfe) == 0xf6);
        assert(bool(Wire.regs[0x6e][6] & 1) == (mode != StopWatchChargingMode::Off));
        assert(Wire.writes.front() == std::make_tuple(0x6e, 6, 0xf6));
        if (mode != StopWatchChargingMode::Off) {
            assert(Wire.writes.size() == 3);
            assert(Wire.writes[1] == std::make_tuple(0x6e, 0x11, mode == StopWatchChargingMode::Low ? 0xff : 0xf7));
            assert(Wire.writes.back() == std::make_tuple(0x6e, 6, 0xf7));
        } else assert(Wire.writes.size() == 1);
    }
    const int beforeInvalidMode = Wire.transactions;
    assert(!m5stack_stopwatch_set_charging_mode(static_cast<StopWatchChargingMode>(3)));
    assert(Wire.transactions == beforeInvalidMode);
    for (int failure = 0; failure < 6; ++failure) {
        Wire.failureAt = Wire.transactions + failure;
        Wire.writes.clear();
        assert(!m5stack_stopwatch_set_charging_mode(StopWatchChargingMode::Low));
        assert(lockDepth == 0);
        if (failure >= 2) assert(!(Wire.regs[0x6e][6] & 1));
        for (auto [address, reg, value] : Wire.writes)
            assert(address != 0x6e || reg != 6 || !(value & 1));
    }
    Wire.failureAt = -1;
    const uint8_t power = Wire.regs[0x6e][6];
    assert(m5stack_stopwatch_set_status_led(false));
    assert(Wire.regs[0x6e][6] == (power & ~0x10));
    assert(m5stack_stopwatch_set_status_led(true));
    assert(Wire.regs[0x6e][6] == power);
    Wire.failureAt = Wire.transactions + 1;
    assert(!m5stack_stopwatch_set_status_led(false));
    assert(Wire.regs[0x6e][6] == power && lockDepth == 0);
    Wire.failureAt = -1;
    const uint8_t outputs = Wire.regs[0x4f][6];
    const auto elapsed = totalDelayMs;
    assert(!m5stack_stopwatch_vibrate(5001));
    assert(m5stack_stopwatch_vibrate(200));
    assert(totalDelayMs == elapsed && Wire.regs[0x4f][6] == (outputs | 1));
    clockMs = 199; m5stack_stopwatch_loop();
    assert(Wire.regs[0x4f][6] == (outputs | 1));
    assert(m5stack_stopwatch_vibrate(300));
    clockMs = 498; m5stack_stopwatch_loop();
    assert(motorRunning);
    clockMs = 499;
    Wire.failureAt = Wire.transactions + 1;
    m5stack_stopwatch_loop();
    assert(motorRunning && lockDepth == 0);
    Wire.failureAt = -1; m5stack_stopwatch_loop();
    assert(!motorRunning && Wire.regs[0x4f][6] == outputs);
    clockMs = UINT32_MAX - 99;
    assert(m5stack_stopwatch_vibrate(200));
    clockMs = 99; m5stack_stopwatch_loop(); assert(motorRunning);
    clockMs = 100; m5stack_stopwatch_loop(); assert(!motorRunning);
    assert(m5stack_stopwatch_vibrate(5000));
    assert(m5stack_stopwatch_vibrate(0));
    assert(!motorRunning && Wire.regs[0x4f][6] == outputs);
    Wire.failureAt = Wire.transactions + 1;
    assert(!m5stack_stopwatch_vibrate(200) && motorRunning);
    Wire.failureAt = -1; clockMs += 200;
    m5stack_stopwatch_loop();
    assert(!motorRunning && Wire.regs[0x4f][6] == outputs);
    lockFails = true;
    const uint16_t pattern[] = {20, 100, 20};
    assert(!m5stack_stopwatch_vibrate_pattern(pattern, 3));
    lockFails = false;
    assert(!m5stack_stopwatch_vibrate_pattern(nullptr, 3));
    assert(!m5stack_stopwatch_vibrate_pattern(pattern, 0));
    assert(!m5stack_stopwatch_vibrate_pattern(pattern, 2));
    const uint16_t zeroStep[] = {20, 0, 20};
    const uint16_t tooLong[] = {2500, 1, 2500};
    assert(!m5stack_stopwatch_vibrate_pattern(zeroStep, 3));
    const uint16_t stopPattern[] = {0};
    assert(m5stack_stopwatch_vibrate_pattern(stopPattern, 1));
    assert(!motorRunning && !motorStepCount);
    assert(!m5stack_stopwatch_vibrate_pattern(tooLong, 3));
    clockMs = 0;
    assert(m5stack_stopwatch_vibrate_pattern(pattern, 3));
    clockMs = 19; m5stack_stopwatch_loop(); assert(motorRunning);
    clockMs = 20; m5stack_stopwatch_loop(); assert(!motorRunning && motorStepCount == 3);
    clockMs = 119; m5stack_stopwatch_loop(); assert(!motorRunning);
    clockMs = 120; m5stack_stopwatch_loop(); assert(motorRunning);
    clockMs = 140; m5stack_stopwatch_loop(); assert(!motorRunning && !motorStepCount);
    assert(Wire.regs[0x4f][6] == outputs && totalDelayMs == elapsed);
    clockMs = UINT32_MAX - 9;
    assert(m5stack_stopwatch_vibrate_pattern(pattern, 3));
    clockMs = 9; m5stack_stopwatch_loop(); assert(motorRunning);
    clockMs = 10; m5stack_stopwatch_loop(); assert(!motorRunning);
    assert(m5stack_stopwatch_vibrate(200) && !motorStepCount);
    assert(m5stack_stopwatch_vibrate_pattern(pattern, 3));
    assert(m5stack_stopwatch_vibrate(0) && !motorStepCount && !motorRunning);
    assert(m5stack_stopwatch_vibrate_pattern(pattern, 3));
    clockMs += 20;
    Wire.failureAt = Wire.transactions + 1; m5stack_stopwatch_loop();
    assert(!motorStepCount && motorRunning);
    Wire.failureAt = -1; m5stack_stopwatch_loop();
    assert(!motorRunning && Wire.regs[0x4f][6] == outputs);
    assert(m5stack_stopwatch_vibrate_pattern(pattern, 3));
    clockMs += 20; m5stack_stopwatch_loop(); assert(!motorRunning && motorStepCount);
    clockMs += 100;
    Wire.failureAt = Wire.transactions + 1; m5stack_stopwatch_loop();
    assert(!motorStepCount && motorRunning);
    Wire.failureAt = -1; m5stack_stopwatch_loop(); assert(!motorRunning);
    assert(m5stack_stopwatch_vibrate_pattern(pattern, 3));
    clockMs += 20; m5stack_stopwatch_loop();
    assert(m5stack_stopwatch_vibrate_pattern(pattern, 3) && motorRunning && motorStepIndex == 0);
    assert(m5stack_stopwatch_vibrate(0));
    lockFails = true;
    assert(!m5stack_stopwatch_vibrate(200));
    assert(!m5stack_stopwatch_set_status_led(false));
    assert(!m5stack_stopwatch_request_shutdown(42));
    m5stack_stopwatch_loop();
    lockFails = false;
    ready = false;
    assert(!m5stack_stopwatch_vibrate(200));
    assert(!m5stack_stopwatch_set_status_led(false));
    assert(!m5stack_stopwatch_request_shutdown(42));
    ready = true;
    otaActive = true;
    assert(!m5stack_stopwatch_request_shutdown(42));
    otaActive = false;
    assert(!m5stack_stopwatch_request_shutdown(0));
    assert(m5stack_stopwatch_vibrate(5000));
    const int beforeShutdown = Wire.transactions;
    assert(m5stack_stopwatch_request_shutdown(42) && otaActive);
    assert(Wire.transactions == beforeShutdown);
    assert(!m5stack_stopwatch_request_shutdown(43));
    assert(!ota_activity_try_begin());
    assert(!m5stack_stopwatch_vibrate(200));
    assert(!m5stack_stopwatch_set_status_led(false));
    assert(!m5stack_stopwatch_set_charging_mode(StopWatchChargingMode::Off));
    assert(!m5stack_stopwatch_audio_power(true));
    assert(!m5stack_stopwatch_audio_mute(false));
    Wire.writes.clear();
    lockFails = true; m5stack_stopwatch_loop(); lockFails = false;
    assert(Wire.writes.empty() && otaActive && shutdownToken == 42);
    m5stack_stopwatch_loop();
    assert(audioStops == 1 && shutdownCommandSent && !motorRunning && !audioPowered);
    assert(Wire.writes.size() == 3);
    assert(Wire.writes[0] == std::make_tuple(0x4f, 6, outputs & ~3));
    assert(Wire.writes[1] == std::make_tuple(0x4f, 5, 0xfb));
    assert(Wire.writes[2] == std::make_tuple(0x6e, 0x0c, 0xa1));
    assert(failedShutdowns.empty() && otaActive);
    clockMs += 499; m5stack_stopwatch_loop();
    assert(failedShutdowns.empty() && otaActive);
    ++clockMs; m5stack_stopwatch_loop();
    assert(failedShutdowns == std::vector<uint32_t>({42}) && !otaActive && !shutdownToken);
    for (int failure = 0; failure < 5; ++failure) {
        assert(m5stack_stopwatch_request_shutdown(44));
        Wire.failureAt = Wire.transactions + failure;
        Wire.writes.clear(); m5stack_stopwatch_loop();
        assert(!shutdownToken && !otaActive && lockDepth == 0);
        assert(failedShutdowns.back() == 44);
        for (auto [address, reg, value] : Wire.writes)
            assert(address != 0x6e || reg != 0x0c);
        Wire.failureAt = -1;
    }
    // Confirm the power/reset waveform rather than only its final state.
    std::vector<int> resetLatch;
    for (auto [address, reg, value] : successfulWrites)
        if (address == 0x4f && reg == 5) resetLatch.push_back(value);
    assert(resetLatch == std::vector<int>({0xfb,0xfb,0xe3,0xfb}));
    assert(!m5stack_stopwatch_audio_mute(false)); // Cannot enable unpowered PA.
    assert(m5stack_stopwatch_audio_power(true));
    assert((Wire.regs[0x4f][5] & 4) && !(Wire.regs[0x4f][6] & 2));
    assert(m5stack_stopwatch_audio_mute(false) && (Wire.regs[0x4f][6] & 2));
    assert(m5stack_stopwatch_audio_power(false));
    assert(!(Wire.regs[0x4f][5] & 4) && !(Wire.regs[0x4f][6] & 3));
    assert(m5stack_stopwatch_audio_power(true));
    Wire.failureAt = Wire.transactions;
    assert(!m5stack_stopwatch_audio_power(false)); // Failed mute still cuts rail.
    assert(!(Wire.regs[0x4f][5] & 4));
    reset(); assert(m5stack_stopwatch_init());
    ES8311AudioDriver codec(stopwatch_audio_control());
    codec_io_failed = false;
    const int codecStart = Wire.transactions;
    assert(codec.initCodec(48000));
    const int codecOperations = Wire.transactions - codecStart;
    for (int fail = 0; fail < codecOperations; ++fail) {
        reset(); assert(m5stack_stopwatch_init());
        codec_io_failed = false; Wire.failureAt = Wire.transactions + fail;
        assert(!codec.initCodec(48000));
        assert(lockDepth == 0 && !(Wire.regs[0x4f][6] & 2));
    }
    assert(!codec.initCodec(44100)); // Reject unsupported clock coefficients.
    reset(); assert(m5stack_stopwatch_init());
    assert(codec.begin(48000));
    codec.setMuted(false); assert(Wire.regs[0x4f][6] & 2);
    Wire.failureAt = Wire.transactions;
    codec.setVolume(50); // Codec I/O failure must mute amplifier.
    assert(!(Wire.regs[0x4f][6] & 2));
    Wire.failureAt = Wire.transactions;
    codec.setMuted(false); // Amplifier I/O failure cuts audio rail.
    assert(!(Wire.regs[0x4f][5] & 4));
    codec.end();
    // Exercise the actual audio_init function and actual ES8311 begin,
    // volume, mute, cleanup methods against queue/task/I2S doubles.
    for (int failure = -1; failure < 8; ++failure) {
        reset(); assert(m5stack_stopwatch_init());
        lifecycleTesting = true; silentFrameWritten = false;
        audio_initialized = false; queueCreates = 0; queueFailureAt = -1;
        semaphoreFails = taskFails = channelFails = channelWriteFails = false;
        if (failure == 0) queueFailureAt = 0;
        if (failure == 1) queueFailureAt = 1;
        if (failure == 2) semaphoreFails = true;
        if (failure == 3) taskFails = true;
        if (failure == 4) channelFails = true;
        if (failure == 5) Wire.failCodecVolume = true;
        if (failure == 6) Wire.failAmpEnable = true;
        if (failure == 7) channelWriteFails = true;
        audio_init(30);
        if (failure == -1 || failure == 6) {
            assert(audio_initialized && liveChannels == 1 && liveQueues == 2);
            assert((Wire.regs[0x4f][5] & 4) && !(Wire.regs[0x4f][6] & 2));
            assert(output_muted && output_last_write_us == 0);
            int volumeIndex = -1, ampIndex = -1;
            for (size_t i = 0; i < Wire.writes.size(); ++i) {
                const auto [address, reg, value] = Wire.writes[i];
                if (address == 0x18 && reg == 0x32) volumeIndex = i;
                if (address == 0x4f && reg == 6 && (value & 2)) ampIndex = i;
            }
            assert(volumeIndex >= 0 && ampIndex == -1);
            const int afterInit = Wire.transactions;
            audio_init(30);
            assert(Wire.transactions == afterInit && liveChannels == 1);
            if (failure == 6) {
                assert(!output_driver->setMuted(false));
                assert(!(Wire.regs[0x4f][5] & 4) && !(Wire.regs[0x4f][6] & 2));
            }
            vQueueDelete(audio_queue); audio_queue = nullptr;
            audio_init_abort();
        } else {
            assert(!audio_initialized && liveChannels == 0 && liveQueues == 0);
            assert(!(Wire.regs[0x4f][5] & 4) && !(Wire.regs[0x4f][6] & 2));
        }
        assert(audio_queue == nullptr && music_work_queue == nullptr);
    }
    lifecycleTesting = false;
    for (int fail = 0; fail < count; ++fail) {
        reset(); Wire.failureAt = fail;
        if (fail == firstExpanderAccess) {
            assert(m5stack_stopwatch_init() && ready && lockDepth == 0);
            assert(delays[0] == 10 && delays[1] == 10);
            continue;
        }
        assert(!m5stack_stopwatch_init() && lockDepth == 0 && !ready);
        if (expander) {
            assert(!(Wire.regs[0x4f][5] & 0x9c) && !(Wire.regs[0x4f][6] & 3));
            assert(!(Wire.regs[0x4f][0x1c] & 0x80) &&
                   !(Wire.regs[0x4f][0x1e] & 0x80) &&
                   !(Wire.regs[0x4f][0x22] & 0x80));
        }
        assert(!m5stack_stopwatch_audio_power(true));
        const int failedCount = Wire.transactions;
        assert(m5stack_stopwatch_init() && Wire.transactions > failedCount && Wire.begins == 1);
    }
    for (unsigned long startup : {50UL, 190UL, 200UL, 210UL, 500UL, 1990UL, 2000UL}) {
        reset(); Wire.expanderStartupMs = startup;
        assert(m5stack_stopwatch_init() && ready && expander == 0x4f);
        assert(totalDelayMs == startup + 220 && lockDepth == 0);
    }
    reset(); Wire.expanderStartupMs = 4000;
    assert(!m5stack_stopwatch_init() && !ready && !expander && lockDepth == 0);
    assert(totalDelayMs == 3660 && Wire.frequency == 100000);
    assert(!(Wire.regs[0x6e][6] & 8));
    for (int frequency : {100000, 400000}) {
        reset(); Wire.expanderProbeFails = true; Wire.expanderReadHz = frequency;
        assert(!m5stack_stopwatch_init() && !ready && !expander && lockDepth == 0);
        assert(Wire.frequency == 100000);
        assert(Wire.clockChanges == std::vector<int>({100000,100000,400000,100000,100000,400000,100000}));
        assert(std::find(Wire.identityReads.begin(), Wire.identityReads.end(),
                         std::make_pair(0x4f, frequency)) != Wire.identityReads.end());
    }
    reset(); Wire.primary = false; Wire.fallback = true;
    Wire.expanderStartupMs = 50;
    assert(m5stack_stopwatch_init() && expander == 0x6f);
    assert(Wire.regs[0x6f][0x1c] == 0x4b && Wire.regs[0x6f][0x22] == 0x42);
    reset(); Wire.primary = Wire.fallback = false;
    assert(!m5stack_stopwatch_init() && !ready);
    assert(!m5stack_stopwatch_init() && Wire.begins == 1);
    Wire.primary = true;
    assert(m5stack_stopwatch_init() && Wire.begins == 1);
    reset(); Wire.shortRead = true;
    assert(!m5stack_stopwatch_init() && !ready && lockDepth == 0);
    reset(); Wire.shortWrite = true;
    assert(!m5stack_stopwatch_init() && !ready && lockDepth == 0);
    reset(); assert(m5stack_stopwatch_init());
    lockFails = true;
    const int beforeLock = Wire.transactions;
    uint16_t millivolts = 0; bool charging = false;
    assert(!m5stack_stopwatch_ready());
    assert(!m5stack_stopwatch_audio_power(true));
    assert(!m5stack_stopwatch_set_charging_mode(StopWatchChargingMode::High));
    assert(!m5stack_stopwatch_battery_read(millivolts, charging));
    assert(Wire.transactions == beforeLock && lockDepth == 0);
    lockFails = false;
    battery(3897, 5000, 0);
    assert(m5stack_stopwatch_battery_read(millivolts, charging) && millivolts == 3897 && charging);
    battery(3897, 5000, 4);
    assert(m5stack_stopwatch_battery_read(millivolts, charging) && !charging);
    battery(3897, 3999, 0);
    assert(m5stack_stopwatch_battery_read(millivolts, charging) && !charging);
    Wire.shortRead = true;
    assert(!m5stack_stopwatch_battery_read(millivolts, charging));
    Wire.shortRead = false;

    SensorRegistry registry;
    board_register_sensors(registry);
    assert(registry.callbacks.sample && registry.callbacks.field_count == 3);
    JsonObject doc;
    battery(4200, 5000, 0);
    registry.callbacks.sample(doc);
    assert(doc["battery_percentage"].value == 100 && doc["battery_charging"].value == 1);
    battery(2000, 5000, 0);
    registry.callbacks.sample(doc);
    assert(doc["battery_voltage"].null && doc["battery_percentage"].null && doc["battery_charging"].null);

    Wire_CST820B_TouchDriver driver(board_cst820b_config());
    driver.init();
    const int beforeTouch = Wire.transactions;
    assert(driver.readSample().status == TouchReadStatus::Unchanged && Wire.transactions == beforeTouch);
    irq = 0;
    for (int rotation = 0; rotation < 4; ++rotation) {
        driver.setRotation(rotation);
        contact(1, 0, 233, 0);
        const auto sample = driver.readSample();
        assert(sample.status == TouchReadStatus::Fresh && sample.pressed);
        const std::array<std::pair<int,int>,4> expected{{{232,0},{0,233},{233,465},{465,232}}};
        assert(sample.horizontal == expected[rotation].first && sample.vertical == expected[rotation].second);
    }
    driver.setRotation(0);
    contact(1, 0, 234, 233);
    auto center = driver.readSample();
    assert(center.horizontal == 233 && center.vertical == 233);
    contact(1, 0, 467, 465);
    auto edge = driver.readSample();
    assert(edge.horizontal == 465 && edge.vertical == 465);
    contact(1, 3, 0, 0);
    assert(driver.readSample().status == TouchReadStatus::Error);
    clockMs += 50;
    contact(2, 0, 0, 0);
    assert(driver.readSample().status == TouchReadStatus::Error);
    clockMs += 50;
    Wire.shortRead = true;
    assert(driver.readSample().status == TouchReadStatus::Error);
    Wire.shortRead = false;
    clockMs += 50;
    irq = 1; contact(0, 1, 0, 0);
    auto release = driver.readSample(); // Read release even though interrupt deasserted.
    assert(release.status == TouchReadStatus::Fresh && !release.pressed);
    contact(1, 0, 0, 0); cst820b_interrupt(); // Capture an IRQ pulse between polls.
    assert(driver.readSample().pressed);
    irq = 0; contact(1, 2, 4095, 4095); driver.setRotation(0);
    auto clamped = driver.readSample();
    assert(clamped.horizontal == 465 && clamped.vertical == 465);
    driver.setCalibration(10,10,10,10); // Invalid calibration must not divide by zero.
    driver.setCalibration(0,100,0,100);
    contact(1, 0, 100, 100);
    auto calibrated = driver.readSample();
    assert(calibrated.horizontal == 465 && calibrated.vertical == 465);
    assert(Wire.begins == 1);

    // An error during a released IRQ pulse must not leave the filter stuck
    // after IRQ deasserts. Only a successfully read release clears it.
    Wire_CST820B_TouchDriver idleRecovery(board_cst820b_config());
    idleRecovery.init();
    TouchSampleFilter filter;
    irq = 1; cst820b_interrupt(); Wire.shortRead = true;
    clockMs = 1000;
    const auto readError = idleRecovery.readSample();
    assert(readError.status == TouchReadStatus::Error);
    filter.update(readError, clockMs);
    const int erroredReads = Wire.transactions;
    clockMs = 1010;
    assert(idleRecovery.readSample().status == TouchReadStatus::Error);
    assert(Wire.transactions == erroredReads); // Bounded recovery backoff.
    clockMs = 1150;
    filter.update(idleRecovery.readSample(), clockMs); // Still a transport error.
    Wire.shortRead = false; contact(0,1,0,0);
    clockMs = 1200;
    const auto recovered = idleRecovery.readSample();
    assert(recovered.status == TouchReadStatus::Fresh && !recovered.pressed);
    filter.update(recovered, clockMs);
    contact(1,0,100,100); cst820b_interrupt(); clockMs = 1201;
    assert(filter.update(idleRecovery.readSample(), clockMs).pressed);

    Arduino_GFX_CO5300_Driver display(board_co5300_config());
    display.init();
    assert((display.isAvailable() && offsets == std::array<int,4>({6,0,6,0})));
    const std::vector<std::pair<int,int>> expectedCommands{
        {0x11,-1},{0xc4,0x80},{0x35,0x80},{0x44,0x1d2},{0x3a,0x55},
        {0x53,0x20},{0x20,-1},{0x36,0},{0x51,0},{0x29,-1}};
    assert(commands == expectedCommands);
    display.setBacklightBrightness(50); assert(panelBrightness == 127);
    display.setBacklight(false); assert(panelBrightness == 0);
    display.setBacklight(true); assert(panelBrightness == 127);
    const uint16_t pixels[] = {1,2,99,3,4,99,5,6,99};
    display.flushSrcStride = 6;
    for (int rotation = 0; rotation < 4; ++rotation) {
        display.setRotation(rotation);
        display.setAddrWindow(10,20,2,3);
        display.pushColors(const_cast<uint16_t*>(pixels),6);
        const std::array<std::array<int,4>,4> windows{{
            {10,20,4,4},{444,10,4,2},{454,442,4,4},{20,454,4,2}}};
        const std::array<std::vector<uint16_t>,4> values{{
            {0,1,2,0,0,3,4,0,0,5,6,0,0,0,0,0},{5,3,1,0,6,4,2,0},
            {0,0,0,0,0,6,5,0,0,4,3,0,0,2,1,0},{0,2,4,6,0,1,3,5}}};
        assert(rectangle == windows[rotation] && drawn == values[rotation]);
    }
    display.setRotation(0);
    display.flushSrcStride = 2;
    uint16_t replacement = 42;
    display.setAddrWindow(11,21,1,1);
    display.pushColors(&replacement,1);
    assert((rectangle == std::array<int,4>({12,20,2,2})));
    assert((drawn == std::vector<uint16_t>({2,0,42,0})));
    display.setAddrWindow(10,21,1,1);
    display.pushColors(&replacement,1);
    assert((drawn == std::vector<uint16_t>({0,1,0,42})));
    display.setAddrWindow(11,21,1,1);
    display.pushColors(&replacement,1);
    assert((drawn == std::vector<uint16_t>({2,0,42,0})));
    uint16_t neighbor = 77;
    display.setAddrWindow(12,21,1,1);
    display.pushColors(&neighbor,1);
    assert((drawn == std::vector<uint16_t>({2,0,42,77})));
    display.setAddrWindow(11,21,1,1);
    display.pushColors(&replacement,1);
    assert((drawn == std::vector<uint16_t>({2,0,42,77})));
    display.setAddrWindow(11,20,1,1);
    display.pushColors(&neighbor,1);
    assert((drawn == std::vector<uint16_t>({77,0,42,77})));
    display.setAddrWindow(11,21,1,1);
    display.pushColors(&replacement,1);
    assert((drawn == std::vector<uint16_t>({77,0,42,77})));
    display.flushSrcStride = DISPLAY_WIDTH * sizeof(uint16_t);
    std::vector<uint16_t> strip(LVGL_BUFFER_SIZE, 123);
    for (int rotation = 0; rotation < 4; ++rotation) {
        display.setRotation(rotation);
        display.setAddrWindow(0,0,DISPLAY_WIDTH,16);
        display.pushColors(strip.data(),strip.size());
        assert(drawn.size() <= LVGL_BUFFER_SIZE + 2 * DISPLAY_HEIGHT + 2 * (DISPLAY_WIDTH + 2) + 4);
    }
    display.setRotation(0);
    display.flushSrcStride = 2;
    display.setAddrWindow(465,465,1,1);
    display.pushColors(&replacement,1);
    assert((rectangle == std::array<int,4>({466,464,2,2})));
    assert((drawn == std::vector<uint16_t>({123,0,42,0})));
    drawn.clear();
    display.setAddrWindow(465,465,2,2);
    display.pushColors(const_cast<uint16_t*>(pixels),4);
    assert(drawn.empty());
    commands.clear(); delays.clear();
    display.setBacklightBrightness(0);
    assert(!display.needsTwoPhaseWake());
    display.displaySleep();
    drawn.clear();
    display.setAddrWindow(465,465,1,1);
    display.pushColors(&neighbor,1);
    assert(drawn.empty());
    display.displayWakeSleepOut();
    assert((delays == std::vector<int>({120,120,120,120})));
    assert((rectangle == std::array<int,4>({0,0,468,466})));
    assert(drawn.size() == 468 * 466);
    assert(drawn[465 * 468 + 466] == 77);
    display.displayWakeDisplayOn();
    assert((commands == std::vector<std::pair<int,int>>({{0x28,-1},{0x10,-1},{0x29,-1},{0x11,-1}})));
    display.setBacklightBrightness(88);
    assert(panelBrightness == 224);
    allocationFails = true;
    Arduino_GFX_CO5300_Driver failedDisplay(board_co5300_config());
    failedDisplay.init(); assert(!failedDisplay.isAvailable());
    allocationFails = false; gfxBegins = false;
    Arduino_GFX_CO5300_Driver failedBus(board_co5300_config());
    failedBus.init(); assert(!failedBus.isAvailable());
    assert(lockDepth == 0);
    for (bool warmBoot : {false, true}) {
        reset();
        if (warmBoot) {
            assert(m5stack_stopwatch_init());
            ready = wireStarted = false; // ESP32 restart retains peripheral state.
        }
        assert(m5stack_stopwatch_init());
        Arduino_DataBus panelBus;
        initialize_stopwatch_panel(&panelBus);
    }
}
'''

directory = ROOT / "build" / "stopwatch-hal-check"
directory.mkdir(parents=True, exist_ok=True)
try:
    source = directory / "check.cpp"
    executable = directory / "check"
    source.write_text(PRELUDE + BODY.replace("#pragma once", "") + TESTS)
    subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter", "-Wno-unused-function",
                    str(source), "-o", str(executable)], check=True)
    subprocess.run([str(executable)], check=True)
    print("PASS: StopWatch board composition and hardware regressions", flush=True)
    for boot_mode in (0, 1, 2):
        boot_test = TESTS[:TESTS.index("int main() {")] + r'''
int main() {
    reset();
    Wire.regs[0x6e][6] |= 1;
    assert(!m5stack_stopwatch_set_charging_mode(StopWatchChargingMode::High));
    assert(Wire.transactions == 0);
    assert(m5stack_stopwatch_init());
    assert((Wire.regs[0x6e][6] & 0xfe) == 0xf6);
    assert(bool(Wire.regs[0x6e][6] & 1) == (STOPWATCH_CHARGING_MODE != 0));
    assert(bool(Wire.regs[0x6e][0x11] & 8) == (STOPWATCH_CHARGING_MODE == 1));
    if (STOPWATCH_CHARGING_MODE == 0)
        for (auto [address, reg, value] : Wire.writes)
            assert(address != 0x6e || reg != 6 || !(value & 1));
    assert(m5stack_stopwatch_set_status_led(false));
    assert(!(Wire.regs[0x6e][6] & 0x10));
    assert(m5stack_stopwatch_vibrate(5000));
    ready = false;
    assert(m5stack_stopwatch_init());
    assert(Wire.begins == 1 && (Wire.regs[0x6e][6] & 0x10));
    assert(!(Wire.regs[0x4f][6] & 1) && !motorRunning);
}
'''
        source.write_text(PRELUDE.replace("#define STOPWATCH_CHARGING_MODE 2",
                                         f"#define STOPWATCH_CHARGING_MODE {boot_mode}") +
                          BODY.replace("#pragma once", "") + boot_test)
        subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                        "-Wno-unused-parameter", "-Wno-unused-function", str(source),
                        "-o", str(executable)], check=True)
        subprocess.run([str(executable)], check=True)
    for invalid_mode in (-1, 3, 1.5):
        header = production("src/boards/m5stack-stopwatch/stopwatch_hardware.h").replace("#pragma once", "")
        source.write_text(f"#include <cstdint>\n#define STOPWATCH_CHARGING_MODE {invalid_mode}\n" + header)
        result = subprocess.run(["c++", "-std=c++17", "-fsyntax-only", str(source)],
                                capture_output=True, text=True)
        assert result.returncode != 0 and "STOPWATCH_CHARGING_MODE must be" in result.stderr
    action_prelude = r'''
#include <ArduinoJson.h>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <vector>
#define HAS_DISPLAY 1
#define HAS_BUTTON 1
#define HAS_MQTT 0
#define CONFIG_ACTION_TYPE_MAX_LEN 16
#define STOPWATCH_CHARGING_MODE 2
#define ACTION_CONTINUATION_SLOTS 1
constexpr uint8_t STOPWATCH_VIBRATION_MAX_STEPS = 15;
struct ButtonAction { char type[16]; union { uint8_t device_class[96]; } payload; };
#ifdef BOARD_ACTION_SOURCE
static bool setterOk = true;
static int setterCalls = 0;
static StopWatchChargingMode requestedMode;
bool m5stack_stopwatch_set_charging_mode(StopWatchChargingMode mode) {
    ++setterCalls;
    requestedMode = mode;
    return setterOk && static_cast<uint8_t>(mode) <= 2;
}
static bool requestedLed;
static uint32_t requestedToken;
bool m5stack_stopwatch_set_status_led(bool enabled) {
    requestedLed = enabled; return setterOk;
}
static uint16_t requestedSteps[STOPWATCH_VIBRATION_MAX_STEPS];
static uint8_t requestedStepCount;
bool m5stack_stopwatch_vibrate_pattern(const uint16_t* durations, uint8_t count) {
    if (!count || count > STOPWATCH_VIBRATION_MAX_STEPS) return false;
    requestedStepCount = count;
    memcpy(requestedSteps, durations, count * sizeof(uint16_t));
    return setterOk;
}
bool m5stack_stopwatch_request_shutdown(uint32_t token) {
    requestedToken = token; return setterOk && token != 0;
}
#endif
'''
    action_tests = r'''
int main() {
#ifdef BOARD_ACTION_SOURCE
    assert(action_type_count() == 4 && action_type_is_supported("charging_mode"));
    for (const char* name : {"charging_mode", "status_led", "vibrate", "shutdown"}) {
        const auto* registered = action_type_find(name);
        assert(registered && !registered->display_lock_required);
        JsonDocument catalog;
        auto metadata = catalog.to<JsonObject>(); registered->describe(metadata);
        assert(strcmp(metadata["group"], "M5Stack StopWatch") == 0);
    }
    const auto* type = action_type_find("charging_mode");
    assert(type->execution == ACTION_EXECUTION_SYNC && !type->display_lock_required);
    JsonDocument catalog;
    auto metadata = catalog.to<JsonObject>();
    type->describe(metadata);
    assert(strcmp(metadata["label"], "Set charging mode") == 0);
    assert(metadata["commands"].size() == 3);
    assert(metadata["editor_fields"][0]["command_options"].as<bool>());
    for (const char* mode : {"off", "low", "high"}) {
        JsonDocument input, output;
        auto action = input.to<JsonObject>();
        action["mode"] = mode;
        assert(!action_type_validate(type, action));
        ButtonAction act{};
        type->parse(action, act);
        type->serialize(act, output.to<JsonObject>());
        assert(strcmp(output["mode"], mode) == 0);
        assert(type->dispatch(act, "test", 0) == ACTION_COMPLETE);
        assert(static_cast<uint8_t>(requestedMode) == act.payload.device_class[0]);
        setterOk = false;
        assert(type->dispatch(act, "test", 0) == ACTION_FAILED);
        setterOk = true;
    }
    for (const char* invalid : {"", "HIGH", "highx", "unknown", "[mqtt:x]"}) {
        JsonDocument input;
        auto action = input.to<JsonObject>();
        action["mode"] = invalid;
        assert(action_type_validate(type, action));
        ButtonAction act{};
        type->parse(action, act);
        assert(type->dispatch(act, "test", 0) == ACTION_FAILED);
    }
    JsonDocument input;
    assert(action_type_validate(type, input.to<JsonObject>()));
    input["mode"] = 1;
    assert(action_type_validate(type, input.as<JsonObjectConst>()));
    const auto* led = action_type_find("status_led");
    assert(led->execution == ACTION_EXECUTION_SYNC);
    for (const char* mode : {"on", "off"}) {
        input.clear(); input["mode"] = mode;
        assert(!action_type_validate(led, input.as<JsonObjectConst>()));
        ButtonAction act{}; led->parse(input.as<JsonObject>(), act);
        JsonDocument output; led->serialize(act, output.to<JsonObject>());
        assert(strcmp(output["mode"], mode) == 0);
        assert(led->dispatch(act, "test", 0) == ACTION_COMPLETE);
        assert(requestedLed == (strcmp(mode, "on") == 0));
        setterOk = false; assert(led->dispatch(act, "test", 0) == ACTION_FAILED); setterOk = true;
    }
    for (const char* mode : {"", "ON", "low", "high", "unknown", "[mqtt:x]"}) {
        input.clear(); input["mode"] = mode;
        assert(action_type_validate(led, input.as<JsonObjectConst>()));
        ButtonAction act{}; led->parse(input.as<JsonObject>(), act);
        assert(led->dispatch(act, "test", 0) == ACTION_FAILED);
    }
    input.clear(); assert(action_type_validate(led, input.to<JsonObject>()));
    input["mode"] = true; assert(action_type_validate(led, input.as<JsonObjectConst>()));
    const auto* vibrate = action_type_find("vibrate");
    assert(vibrate->execution == ACTION_EXECUTION_SYNC);
    JsonDocument vibrationCatalog;
    auto vibrationMetadata = vibrationCatalog.to<JsonObject>(); vibrate->describe(vibrationMetadata);
    assert(vibrationMetadata["editor_fields"].size() == 1);
    assert(vibrationMetadata["fields"].size() == 1);
    assert(strcmp(vibrationMetadata["editor_fields"][0]["name"], "pattern") == 0);
    assert(strcmp(vibrationMetadata["editor_fields"][0]["default"], "200") == 0);
    for (int duration : {-1, 0, 200, 5000, 5001}) {
        input.clear(); input["duration_ms"] = duration;
        assert(action_type_validate(vibrate, input.as<JsonObjectConst>()));
        ButtonAction act{}; vibrate->parse(input.as<JsonObject>(), act);
        assert(vibrate->dispatch(act, "test", 0) == ACTION_FAILED);
    }
    for (const char* invalid : {"1.5", "200", "true", "null", "[]", "{}", "4294967296"}) {
        input.clear(); deserializeJson(input, std::string("{\"pattern\":") + invalid + "}");
        assert(action_type_validate(vibrate, input.as<JsonObjectConst>()));
        ButtonAction act{}; vibrate->parse(input.as<JsonObject>(), act);
        assert(vibrate->dispatch(act, "test", 0) == ACTION_FAILED);
    }
    input.clear(); auto empty = input.to<JsonObject>();
    assert(!action_type_validate(vibrate, empty));
    ButtonAction act{}; vibrate->parse(empty, act);
    assert(vibrate->dispatch(act, "test", 0) == ACTION_COMPLETE && requestedStepCount == 1 && requestedSteps[0] == 200);
    const auto* shutdown = action_type_find("shutdown");
    for (const char* pattern : {"0", "200", "5000", "20,100,20", " 20 , 100 , 20 \t", "1,1,1,1,1,1,1,1,1,1,1,1,1,1,1"}) {
        input.clear(); input["pattern"] = pattern;
        assert(!action_type_validate(vibrate, input.as<JsonObjectConst>()));
        vibrate->parse(input.as<JsonObject>(), act);
        assert(vibrate->dispatch(act, "test", 0) == ACTION_COMPLETE);
        assert(requestedStepCount & 1);
        JsonDocument output; vibrate->serialize(act, output.to<JsonObject>());
        assert(output["pattern"].is<const char*>() && !output.containsKey("duration_ms"));
        assert(!action_type_validate(vibrate, output.as<JsonObjectConst>()));
        setterOk = false; assert(vibrate->dispatch(act, "test", 0) == ACTION_FAILED); setterOk = true;
    }
    for (const char* pattern : {"", "0,100,20", "20,0,20", "20,100,0", "20,100", "20,,20", "20,", "-20", "1.5", "v20 p100 v20", "5001", "2500,1,2500", "9999999999999999999", "1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1"}) {
        input.clear(); input["pattern"] = pattern;
        assert(action_type_validate(vibrate, input.as<JsonObjectConst>()));
        vibrate->parse(input.as<JsonObject>(), act);
        assert(vibrate->dispatch(act, "test", 0) == ACTION_FAILED);
    }
    input.clear(); input["pattern"] = "20,100,20"; input["duration_ms"] = 200;
    assert(action_type_validate(vibrate, input.as<JsonObjectConst>()));
    input.clear(); input["pattern"] = 200;
    assert(action_type_validate(vibrate, input.as<JsonObjectConst>()));
    assert(shutdown->execution == ACTION_EXECUTION_PAUSABLE);
    assert(!shutdown->parse && !shutdown->serialize);
    assert(shutdown->dispatch(act, "test", 0) == ACTION_FAILED);
    assert(shutdown->dispatch(act, "test", 42) == ACTION_PENDING && requestedToken == 42);
    setterOk = false; assert(shutdown->dispatch(act, "test", 42) == ACTION_FAILED); setterOk = true;
    static char names[22][16];
    std::vector<ActionTypeDef> fillers; fillers.reserve(22);
    for (int index = 0; index < 22; ++index) {
        snprintf(names[index], sizeof(names[index]), "filler%d", index);
        fillers.emplace_back(names[index], nullptr, nullptr, nullptr, nullptr, nullptr);
        action_type_register(&fillers.back());
    }
    assert(action_type_count() == 26);
#else
    assert(action_type_count() == 0);
#endif
}
'''
    module = directory / "board-action.inc"
    module.write_text(production("src/boards/m5stack-stopwatch/stopwatch_actions.cpp"))
    aggregator = (ROOT / "src/app/actions.cpp").read_text().replace(
        '#include "actions/action_modules.inc"', "")
    registry = production("src/app/action_registry.h") + production("src/app/action_registry.cpp")
    for overrides in (ROOT / "src/boards").glob("*/board_overrides.h"):
        if overrides.parent.name != "m5stack-stopwatch":
            assert "stopwatch_actions" not in overrides.read_text()
            assert "m5stack_stopwatch_loop" not in overrides.read_text()
            assert "STOPWATCH_CHARGING_MODE" not in overrides.read_text()
    for board_action in (False, True):
        capacity = re.search(r"#define ACTION_TYPE_REGISTRY_MAX_TYPES (\d+)",
                     (ROOT / "src/boards/m5stack-stopwatch/board_overrides.h").read_text()).group(1)
        define = (f'#define BOARD_ACTION_SOURCE "{module}"\n'
              f'#define ACTION_TYPE_REGISTRY_MAX_TYPES {capacity}\n') if board_action else ""
        header = production("src/boards/m5stack-stopwatch/stopwatch_hardware.h").replace("#pragma once", "")
        enum = re.search(r"enum class StopWatchChargingMode[^;]+;", header).group(0)
        source.write_text(define + "#include <cstdint>\n" + enum + action_prelude +
                          production("src/app/action_result.h") + registry.replace("#pragma once", "") +
                  aggregator + action_tests)
        subprocess.run(["c++", "-std=c++17", "-Wno-deprecated-declarations",
                        "-I", str(pathlib.Path.home() / "Arduino/libraries/ArduinoJson/src"),
                        str(source), "-o", str(executable)], check=True)
        subprocess.run([str(executable)], check=True)
        if not board_action:
            symbols = subprocess.check_output(["nm", "-C", str(executable)], text=True)
            strings = subprocess.check_output(["strings", str(executable)], text=True)
            assert "charging_mode" not in symbols
            assert "m5stack_stopwatch" not in symbols
            assert "charging_mode" not in strings
            assert "Set charging mode" not in strings
            assert "charging_mode mode must" not in strings
            for identifier in ("status_led", "vibrate", "shutdown", "M5Stack StopWatch"):
                assert identifier not in strings
    print("PASS: all boot charging modes, board action round trips, validation, dispatch, and generic exclusion", flush=True)
    loop_hook = re.search(r"void loop\(\)\s*\{(.*?)\s*time_service_loop\(\);",
                          (ROOT / "src/app/app.ino").read_text(), re.S).group(1)
    for enabled in (False, True):
        define = "#define BOARD_HARDWARE_LOOP m5stack_stopwatch_loop\n" if enabled else ""
        source.write_text(define + r'''
#include <cassert>
static int loopCalls = 0;
#ifdef BOARD_HARDWARE_LOOP
void m5stack_stopwatch_loop() { ++loopCalls; }
#endif
void main_loop() {
''' + loop_hook + r'''
}
int main() {
    main_loop();
#ifdef BOARD_HARDWARE_LOOP
    assert(loopCalls == 1);
#else
    assert(loopCalls == 0);
#endif
}
''')
        subprocess.run(["c++", "-std=c++17", str(source), "-o", str(executable)], check=True)
        subprocess.run([str(executable)], check=True)
        if not enabled:
            assert "m5stack_stopwatch" not in subprocess.check_output(["nm", "-C", str(executable)], text=True)
    print("PASS: board main-loop hook executes only when selected; generic binary excludes it", flush=True)
    brightness_code = re.search(
        r"static void apply_brightness\(uint8_t brightness\) \{.*?\n\}",
        (ROOT / "src/app/screen_saver_manager.cpp").read_text(), re.S
    ).group(0)
    brightness_doubles = r'''
#include <cassert>
#include <cstdint>
static int displayLockDepth = 0;
static bool unlockedWrite = false;
class DisplayDriver {
public:
    bool adjustable = true;
    uint8_t brightness = 0;
    bool on = false;
    bool hasBacklightControl() { return adjustable; }
    void setBacklightBrightness(uint8_t value) {
        unlockedWrite |= displayLockDepth != 1;
        brightness = value;
    }
    void setBacklight(bool value) {
        unlockedWrite |= displayLockDepth != 1;
        on = value;
    }
};
class DisplayManager {
public:
    DisplayDriver* driver = nullptr;
    DisplayDriver* getDriver() { return driver; }
    void lock() { assert(displayLockDepth == 0); ++displayLockDepth; }
    void unlock() { assert(displayLockDepth == 1); --displayLockDepth; }
};
static DisplayManager* displayManager = nullptr;
'''
    brightness_tests = r'''
int main() {
    apply_brightness(30);
    DisplayManager manager;
    displayManager = &manager;
    apply_brightness(30);
    DisplayDriver driver;
    manager.driver = &driver;
    apply_brightness(30);
    assert(driver.brightness == 30 && displayLockDepth == 0);
    driver.adjustable = false;
    apply_brightness(0);
    assert(!driver.on && displayLockDepth == 0);
    apply_brightness(30);
    assert(driver.on && displayLockDepth == 0);
    return unlockedWrite ? 1 : 0;
}
'''
    for unlocked in (False, True):
        code = brightness_code
        if unlocked:
            code = code.replace("displayManager->lock();", "")
            code = code.replace("displayManager->unlock();", "")
        source.write_text(brightness_doubles + code + brightness_tests)
        subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                        str(source), "-o", str(executable)], check=True)
        result = subprocess.run([str(executable)])
        assert result.returncode == (1 if unlocked else 0)
    print("PASS: screensaver brightness holds display lock; unlocked regression rejected", flush=True)
    for profile in ("es8311-gpio", "es8311-microphone", "pcm510xa-gpio"):
        prelude = PRELUDE
        if profile == "es8311-microphone":
            prelude = prelude.replace("#define HAS_AUDIO_INPUT false", "#define HAS_AUDIO_INPUT true")
            prelude = prelude.replace("#define HAS_ES7210_MIC false", "#define HAS_ES7210_MIC true")
        pcm = profile == "pcm510xa-gpio"
        if pcm:
            prelude = prelude.replace("#define AUDIO_OUTPUT_DRIVER AUDIO_OUTPUT_DRIVER_ES8311",
                                      "#define AUDIO_OUTPUT_DRIVER AUDIO_OUTPUT_DRIVER_PCM510XA")
        paths = ["src/app/audio_output_driver.h", "src/app/audio_input_driver.h",
                 "src/app/i2c_bus.cpp", "src/app/board_hardware.cpp",
                 "src/app/audio_output_drivers.cpp"]
        if pcm:
            paths += ["src/app/drivers/audio_gain.h", "src/app/drivers/pcm510xa_audio_driver.h",
                      "src/app/drivers/pcm510xa_audio_driver.cpp"]
        else:
            paths += ["src/app/drivers/es8311_audio_driver.h", "src/app/drivers/es8311_audio_driver.cpp"]
        body = "\n".join(production(path) for path in paths)
        source.write_text(prelude + body.replace("#pragma once", "") + LIFECYCLE_DOUBLES +
                          LIFECYCLE_CODE + GENERIC_TESTS)
        subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter", "-Wno-unused-function",
                        str(source), "-o", str(executable)], check=True)
        subprocess.run([str(executable)], check=True)
        print(f"PASS: {profile} lifecycle, retry, and cleanup", flush=True)
finally:
    shutil.rmtree(directory)
print("PASS: StopWatch power sequence, failure paths, locking, PMIC sensors, CST820B, CO5300 geometry/lifecycle")
