#include "platform_stubs.h"

#include <WiFi.h>
#include "audio.h"
#include "audio_input.h"
#include "device_classes/voice_assistant/voice.h"
#include "music_analysis.h"
#include "net_activity.h"
#include "timer_engine.h"
#include "log_manager.h"

#include <cstdio>
#include <cstring>

unsigned long millis() { return 10000; }

void log_write(LogLevel, const char*, const char*, ...) {}

HostEsp ESP;
HostWiFi WiFi;
DeviceConfig device_config = {};
HostLittleFS LittleFS;
static DisplayManager g_display_manager;
DisplayManager* displayManager = &g_display_manager;

const char* HostEsp::getChipModel() const { return "host"; }
int HostEsp::getChipRevision() const { return 1; }
int HostEsp::getChipCores() const { return 2; }
int HostEsp::getCpuFreqMHz() const { return 240; }
uint32_t HostEsp::getFlashChipSize() const { return 4 * 1024 * 1024; }

const char* HostString::c_str() const { return "host"; }
HostString HostIpAddress::toString() const { return {}; }
HostString HostWiFi::macAddress() const { return {}; }
int HostWiFi::status() const { return 0; }
HostString HostWiFi::SSID() const { return {}; }
HostIpAddress HostWiFi::localIP() const { return {}; }
const char* HostWiFi::getHostname() const { return "host"; }

uint32_t heap_caps_get_total_size(uint32_t) { return 0; }
void configTime(long, int, const char*) {}
esp_reset_reason_t esp_reset_reason() { return ESP_RST_POWERON; }
int16_t device_telemetry_get_cached_rssi(bool* valid) {
    if (valid) *valid = false;
    return 0;
}
void device_telemetry_get_cpu_usage_snapshot(int* aggregate, int* per_core_values, uint8_t max_cores) {
    if (aggregate) *aggregate = -1;
    for (uint8_t index = 0; index < max_cores; ++index) per_core_values[index] = -1;
}
DeviceMemorySnapshot device_telemetry_get_memory_snapshot() { return {}; }
uint8_t display_manager_get_backlight_brightness() { return 0; }
void display_manager_set_backlight_brightness(uint8_t) {}
lv_obj_t* lv_screen_active() { return nullptr; }
void lv_obj_invalidate(lv_obj_t*) {}
bool health_table_build(bool, HealthTableLookupFn, char*, size_t) { return false; }

#if HAS_AUDIO
uint8_t audio_get_volume() { return 0; }
void audio_beep(const char*, uint8_t) {}
#endif

#if IS_DARKROOM_TIMER
static bool g_relay_on = false;
bool relay_is_on() { return g_relay_on; }
void relay_request(bool on) { g_relay_on = on; }
float tsl2591_read_lux() { return -1.0f; }
DeviceConfig* web_portal_get_current_config() { return &device_config; }
#endif

#if IS_COFFEE_SCALE
#include "device_classes/coffee_scale/brew/brew_manager.h"

float scale_get_weight() { return 18.0f; }
float scale_get_flow_rate() { return 2.5f; }
float scale_get_calibration_factor() { return 1.0f; }
long scale_get_offset() { return 0; }
bool scale_is_available() { return true; }
float scale_get_cal_weight() { return 100.0f; }
const char* scale_get_status() { return "idle"; }

static const BrewStage kFixtureStage = {
    "Pour", "Pour steadily", "Next", STAGE_MANUAL, EFFECT_NONE, EFFECT_NONE,
    0.0f, 36.0f, 2.5f, 30000, {}, {}, {}, 0.0f, 0, {}, {}, {}, {}, {},
};

const char* brew_get_stage_name() { return "Pour"; }
uint32_t brew_get_timer_ms() { return 12000; }
float brew_get_weight() { return 18.0f; }
float brew_get_flow_rate() { return 2.5f; }
bool brew_is_active() { return true; }
const char* brew_get_template_name() { return "fixture"; }
float brew_get_dose_weight() { return 18.0f; }
float brew_get_water_weight() { return 36.0f; }
float brew_get_stage_weight_target() { return 36.0f; }
float brew_get_stage_weight_remaining() { return 18.0f; }
float brew_get_stage_flow_target() { return 2.5f; }
uint32_t brew_get_stage_time_target_ms() { return 30000; }
uint32_t brew_get_stage_time_remaining_ms() { return 18000; }
uint32_t brew_get_stage_time_current_ms() { return 12000; }
const char* brew_get_display_name() { return "Fixture Brew"; }
const char* brew_get_instruction() { return "Pour steadily"; }
const char* brew_get_next_label() { return "Next"; }
const char* brew_get_advance_state() { return "action"; }
void brew_format_stage_status(char* out, size_t out_len) { std::snprintf(out, out_len, "18/36 g"); }
uint8_t brew_get_capture_count() { return 0; }
const BrewCapture* brew_get_capture(uint8_t) { return nullptr; }
BrewPhase brew_get_phase() { return BREW_ACTIVE; }
uint8_t brew_get_stage_count() { return 1; }
uint8_t brew_get_stage_index() { return 0; }
const BrewStage* brew_get_stage(uint8_t index) { return index == 0 ? &kFixtureStage : nullptr; }
int brew_format_timer(const char*, char* out, size_t out_len) { return std::snprintf(out, out_len, "00:12"); }
uint8_t brew_template_count() { return 1; }
const BrewTemplate* brew_template_get(uint8_t index) {
    static const BrewTemplate fixture = {
        "fixture", "Fixture", "Fixture template", "Start", "Done", "", "", nullptr, 1, false,
    };
    return index == 0 ? &fixture : nullptr;
}
#endif

#if IS_SHUTTER_TESTER
#include "device_classes/shutter_tester/shutter_capture.h"
#include "device_classes/shutter_tester/shutter_measure.h"

void shutter_capture_get_caps(ShutterCaptureCaps* out) {
    if (!out) return;
    *out = {};
    out->sensor_count = 1;
    out->preset_id_str = "fixture";
    out->preset_name = "Fixture";
}
bool shutter_capture_is_available() { return true; }
bool shutter_capture_is_calibrating() { return false; }
bool shutter_capture_is_running() { return false; }
bool shutter_measure_get_latest(ShutterMeasurement*) { return false; }
uint8_t shutter_measure_get_history(ShutterMeasurement*, uint8_t) { return 0; }
uint32_t shutter_measure_get_count() { return 0; }
void shutter_measure_get_target(char* out, size_t out_len, bool* locked) {
    if (out && out_len) out[0] = '\0';
    if (locked) *locked = false;
}
bool shutter_session_is_active() { return false; }
uint32_t shutter_session_get_count() { return 0; }
uint32_t shutter_session_get_id() { return 0; }
void shutter_session_get_type(char* out, size_t out_len) { if (out && out_len) out[0] = '\0'; }
void shutter_session_guide_get_target(char* out, size_t out_len) { if (out && out_len) out[0] = '\0'; }
void shutter_session_guide_get_step(char* out, size_t out_len) { if (out && out_len) out[0] = '\0'; }
void shutter_session_guide_get_steps(char* out, size_t out_len) { if (out && out_len) out[0] = '\0'; }
void shutter_session_guide_get_shot(char* out, size_t out_len) { if (out && out_len) out[0] = '\0'; }
void shutter_session_guide_get_shots(char* out, size_t out_len) { if (out && out_len) out[0] = '\0'; }
void shutter_session_guide_get_taking(char* out, size_t out_len) { if (out && out_len) out[0] = '\0'; }
void shutter_session_guide_get_total(char* out, size_t out_len) { if (out && out_len) out[0] = '\0'; }
void shutter_session_guide_get_name(char* out, size_t out_len) { if (out && out_len) out[0] = '\0'; }
void shutter_session_guide_get_id(char* out, size_t out_len) { if (out && out_len) out[0] = '\0'; }
bool shutter_align_binding_resolve(const char*, char* out, size_t out_len) {
    if (out && out_len) out[0] = '\0';
    return false;
}
#endif

#if HAS_AUDIO && HAS_SOUND_PLAYER
void audio_get_music_info(AudioMusicInfo* out) {
    if (out) *out = {};
}
void music_analysis_get_snapshot(MusicAnalysisSnapshot* out) {
    if (out) *out = {};
}
#endif

#if HAS_AUDIO_INPUT
void audio_input_meter_request() {}
bool audio_input_meter_get_snapshot(AudioInputMeterSnapshot* out) {
    if (out) *out = {};
    return false;
}
#endif

#if IS_VOICE_ASSISTANT
void voice_get_snapshot(VoiceSnapshot* out) {
    if (out) *out = {};
}
const char* voice_status_name(VoiceStatus) { return "idle"; }
#endif

#if HAS_BLE_HID
const char* ble_hid_status() { return "disabled"; }
const char* ble_hid_state() { return "disabled"; }
const char* ble_hid_name() { return ""; }
bool ble_hid_is_pairing() { return false; }
bool ble_hid_is_bonded() { return false; }
bool ble_hid_is_encrypted() { return false; }
const char* ble_hid_peer_addr() { return ""; }
const char* ble_hid_peer_id_addr() { return ""; }
#endif

void timer_engine_init() {}
uint32_t timer_get_target_seconds(uint8_t) { return 0; }
TimerState timer_get_state(uint8_t) { return TIMER_STOPPED; }
TimerMode timer_get_mode(uint8_t) { return TIMER_MODE_UP; }
bool timer_is_expired(uint8_t) { return false; }
int timer_format(uint8_t, const char*, char* out, size_t out_len) {
    if (out_len) std::snprintf(out, out_len, "0");
    return 1;
}

uint32_t net_activity_age_ms(net_channel_t) { return NET_ACTIVITY_NEVER; }
uint32_t net_activity_age_any_ms() { return NET_ACTIVITY_NEVER; }
uint32_t net_activity_age_mqtt_ms() { return NET_ACTIVITY_NEVER; }