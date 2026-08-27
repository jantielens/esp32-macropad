#pragma once

#include <cstddef>
#include <cstdint>
#include <freertos/portmacro.h>
#include <LittleFS.h>
#include <esp_heap_caps.h>

class String {};

#define BOARD_CONFIG_H
#define HAS_DISPLAY 1
#define HAS_MCP 0
#define HAS_PSRAM 1
#define TELEMETRY_ALLOW_PSRAM_POOL_WALK 0

#if defined(BINDING_SCHEMA_PROFILE_FULL)
#define HAS_AUDIO 1
#define HAS_SOUND_PLAYER 1
#define HAS_MUSIC_ANALYSIS 1
#define HAS_AUDIO_INPUT 1
#define HAS_BLE_HID 1
#define IS_VOICE_ASSISTANT 0
#elif defined(BINDING_SCHEMA_PROFILE_VOICE)
#define HAS_AUDIO 0
#define HAS_SOUND_PLAYER 0
#define HAS_MUSIC_ANALYSIS 0
#define HAS_AUDIO_INPUT 0
#define HAS_BLE_HID 0
#define IS_VOICE_ASSISTANT 1
#elif defined(BINDING_SCHEMA_PROFILE_DARKROOM)
#define HAS_AUDIO 1
#define HAS_SOUND_PLAYER 0
#define HAS_MUSIC_ANALYSIS 0
#define HAS_AUDIO_INPUT 0
#define HAS_BLE_HID 0
#define IS_VOICE_ASSISTANT 0
#define IS_DARKROOM_TIMER 1
#elif defined(BINDING_SCHEMA_PROFILE_COFFEE)
#define HAS_AUDIO 0
#define HAS_SOUND_PLAYER 0
#define HAS_MUSIC_ANALYSIS 0
#define HAS_AUDIO_INPUT 0
#define HAS_BLE_HID 0
#define IS_VOICE_ASSISTANT 0
#define IS_COFFEE_SCALE 1
#define HAS_SCALE 1
#elif defined(BINDING_SCHEMA_PROFILE_SHUTTER)
#define HAS_AUDIO 0
#define HAS_SOUND_PLAYER 0
#define HAS_MUSIC_ANALYSIS 0
#define HAS_AUDIO_INPUT 0
#define HAS_BLE_HID 0
#define IS_VOICE_ASSISTANT 0
#define IS_SHUTTER_TESTER 1
#define SHUTTER_SENSOR_MAX 9
#define SHUTTER_HISTORY_SIZE 8
#else
#define HAS_AUDIO 0
#define HAS_SOUND_PLAYER 0
#define HAS_MUSIC_ANALYSIS 0
#define HAS_AUDIO_INPUT 0
#define HAS_BLE_HID 0
#define IS_VOICE_ASSISTANT 0
#endif

#define DEVICE_TELEMETRY_H
struct DeviceMemorySnapshot {
    size_t heap_free_bytes;
    size_t heap_min_free_bytes;
    size_t heap_largest_free_block_bytes;
    size_t heap_internal_free_bytes;
    size_t heap_internal_min_free_bytes;
    size_t psram_free_bytes;
    size_t psram_min_free_bytes;
    size_t psram_largest_free_block_bytes;
};
int16_t device_telemetry_get_cached_rssi(bool* valid);
void device_telemetry_get_cpu_usage_snapshot(int* aggregate, int* per_core_values, uint8_t max_cores);
DeviceMemorySnapshot device_telemetry_get_memory_snapshot();

#define DISPLAY_MANAGER_H
class DisplayDriver {
public:
    void displaySleep() {}
    void displayWake() {}
};
class DisplayManager {
public:
    DisplayDriver* getDriver() { return &driver; }
    void lock() {}
    void unlock() {}
private:
    DisplayDriver driver;
};
extern DisplayManager* displayManager;
struct lv_obj_t {};
lv_obj_t* lv_screen_active();
void lv_obj_invalidate(lv_obj_t* object);
uint8_t display_manager_get_backlight_brightness();
void display_manager_set_backlight_brightness(uint8_t brightness);

#define HEALTH_TABLE_BUILDER_H
typedef bool (*HealthTableLookupFn)(const char* key, char* out, size_t out_len);
bool health_table_build(bool extended, HealthTableLookupFn lookup, char* out, size_t out_len);

#define CONFIG_MANAGER_H
struct DeviceConfig {
    bool ble_enabled;
    char tap_beep[1];
};
extern DeviceConfig device_config;

enum esp_reset_reason_t : uint8_t {
    ESP_RST_POWERON,
    ESP_RST_SW,
    ESP_RST_PANIC,
    ESP_RST_INT_WDT,
    ESP_RST_TASK_WDT,
    ESP_RST_WDT,
    ESP_RST_DEEPSLEEP,
    ESP_RST_BROWNOUT,
    ESP_RST_SDIO,
};
esp_reset_reason_t esp_reset_reason();

class HostEsp {
public:
    const char* getChipModel() const;
    int getChipRevision() const;
    int getChipCores() const;
    int getCpuFreqMHz() const;
    uint32_t getFlashChipSize() const;
};
extern HostEsp ESP;

uint32_t heap_caps_get_total_size(uint32_t caps);
void configTime(long gmt_offset_sec, int daylight_offset_sec, const char* server);