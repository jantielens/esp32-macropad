#include "health_binding.h"
#include "board_config.h"

#if HAS_DISPLAY

#include "binding_template.h"
#include "device_telemetry.h"
#include "display_manager.h"
#include "health_table_builder.h"
#include "log_manager.h"

#if HAS_AUDIO
#include "audio.h"
#endif

#if HAS_BLE_HID
#include "config_manager.h"
#endif

#if HAS_BLE_HID
#include "ble_hid.h"
extern DeviceConfig device_config;
#endif

#include <Arduino.h>
#include <WiFi.h>
#include <string.h>
#include <stdio.h>
#include <esp_heap_caps.h>

#include "version.h"
#include "project_branding.h"

#define TAG "HealthBind"

// ============================================================================
// Static system info — populated once at init, never changes
// ============================================================================

static struct {
    bool     initialized;
    char     chip[24];          // e.g. "ESP32-S3"
    int      chip_rev;
    int      chip_cores;
    int      cpu_freq;          // MHz
    uint32_t flash_size;        // bytes
    uint32_t heap_total;        // total heap (internal + PSRAM)
    uint32_t internal_total;    // internal RAM total
    uint32_t psram_total;       // PSRAM total (0 if absent)
    char     mac[18];           // "AA:BB:CC:DD:EE:FF"
} s_static = {};

static void ensure_static_initialized() {
    if (s_static.initialized) return;
    s_static.initialized = true;

    strlcpy(s_static.chip, ESP.getChipModel(), sizeof(s_static.chip));
    s_static.chip_rev     = ESP.getChipRevision();
    s_static.chip_cores   = ESP.getChipCores();
    s_static.cpu_freq     = ESP.getCpuFreqMHz();
    s_static.flash_size   = ESP.getFlashChipSize();
    s_static.heap_total     = (uint32_t)heap_caps_get_total_size(MALLOC_CAP_8BIT);
    s_static.internal_total = (uint32_t)heap_caps_get_total_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    s_static.psram_total    = (uint32_t)heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    strlcpy(s_static.mac, WiFi.macAddress().c_str(), sizeof(s_static.mac));
}

// ============================================================================
// Cached telemetry snapshot — refreshed at most every 2 s
// ============================================================================

#define HEALTH_BINDING_REFRESH_MS 2000

static uint32_t s_last_refresh_ms = 0;
static uint32_t s_last_mem_refresh_ms = 0;
static int s_cpu = -1;
static constexpr uint8_t kHealthCpuCoreCount = 2;
static int s_cpu_cores[kHealthCpuCoreCount] = {-1, -1};
static int16_t s_rssi = 0;
static bool s_rssi_valid = false;
static DeviceMemorySnapshot s_mem = {};
static bool s_wifi_connected = false;
static char s_wifi_ssid[33] = "";   // max SSID length is 32 + null
static char s_ip[16] = "";          // "255.255.255.255" + null

static void refresh_if_stale() {
    uint32_t now = millis();
    if (now - s_last_refresh_ms < HEALTH_BINDING_REFRESH_MS && s_last_refresh_ms != 0) return;
    s_last_refresh_ms = now;

    device_telemetry_get_cpu_usage_snapshot(&s_cpu, s_cpu_cores, kHealthCpuCoreCount);
    s_rssi = device_telemetry_get_cached_rssi(&s_rssi_valid);

    s_wifi_connected = (WiFi.status() == WL_CONNECTED);
    if (s_wifi_connected) {
        strlcpy(s_wifi_ssid, WiFi.SSID().c_str(), sizeof(s_wifi_ssid));
        strlcpy(s_ip, WiFi.localIP().toString().c_str(), sizeof(s_ip));
    } else {
        s_wifi_ssid[0] = '\0';
        s_ip[0] = '\0';
    }
}

static void refresh_memory_if_stale() {
    uint32_t now = millis();
    if (now - s_last_mem_refresh_ms < HEALTH_BINDING_REFRESH_MS && s_last_mem_refresh_ms != 0) return;
    s_last_mem_refresh_ms = now;
    s_mem = device_telemetry_get_memory_snapshot();
}

// ============================================================================
// Parse params: "key" or "key;format"
// ============================================================================

static void parse_health_params(const char* params,
                                char* key, size_t key_len,
                                char* fmt, size_t fmt_len) {
    key[0] = '\0';
    fmt[0] = '\0';
    if (!params || !params[0]) return;

    const char* sep = strchr(params, ';');
    if (!sep) {
        strlcpy(key, params, key_len);
        return;
    }
    size_t klen = (size_t)(sep - params);
    if (klen >= key_len) klen = key_len - 1;
    memcpy(key, params, klen);
    key[klen] = '\0';
    strlcpy(fmt, sep + 1, fmt_len);
}

static bool health_key_requires_memory_snapshot(const char* key) {
    return strcmp(key, "heap_free") == 0 ||
           strcmp(key, "heap_min") == 0 ||
           strcmp(key, "heap_largest") == 0 ||
           strcmp(key, "heap_internal") == 0 ||
           strcmp(key, "heap_internal_used") == 0 ||
           strcmp(key, "psram_free") == 0 ||
           strcmp(key, "psram_min") == 0 ||
#if TELEMETRY_ALLOW_PSRAM_POOL_WALK
           strcmp(key, "psram_largest") == 0 ||
#endif
           strcmp(key, "psram_used") == 0 ||
           strcmp(key, "table") == 0 ||
           strcmp(key, "extended_table") == 0;
}

// ============================================================================
// Key→value lookup
// ============================================================================

static int health_cpu_core_index(const char* key) {
    static constexpr char kCpuCorePrefix[] = "cpu_core_";
    constexpr size_t kSuffixIndex = sizeof(kCpuCorePrefix) - 1;
    if (strncmp(key, kCpuCorePrefix, kSuffixIndex) != 0 ||
        key[kSuffixIndex + 1] != '\0' ||
        key[kSuffixIndex] < '0' || key[kSuffixIndex] >= '0' + kHealthCpuCoreCount) {
        return -1;
    }
    return key[kSuffixIndex] - '0';
}

static bool lookup_value(const char* key, char* out, size_t out_len) {
    if (strcmp(key, "cpu") == 0) {
        if (s_cpu < 0) { strlcpy(out, "?", out_len); return true; }
        snprintf(out, out_len, "%d", s_cpu);
        return true;
    }
    const int core = health_cpu_core_index(key);
    if (core >= 0) {
        const int usage = s_cpu_cores[core];
        if (usage < 0) { strlcpy(out, "?", out_len); return true; }
        snprintf(out, out_len, "%d", usage);
        return true;
    }
    if (strcmp(key, "rssi") == 0) {
        if (!s_rssi_valid) { strlcpy(out, "?", out_len); return true; }
        snprintf(out, out_len, "%d", (int)s_rssi);
        return true;
    }
    if (strcmp(key, "uptime") == 0) {
        snprintf(out, out_len, "%lu", (unsigned long)(millis() / 1000));
        return true;
    }
    if (strcmp(key, "heap_free") == 0) {
        snprintf(out, out_len, "%u", (unsigned)s_mem.heap_free_bytes);
        return true;
    }
    if (strcmp(key, "heap_min") == 0) {
        snprintf(out, out_len, "%u", (unsigned)s_mem.heap_min_free_bytes);
        return true;
    }
    if (strcmp(key, "heap_largest") == 0) {
        snprintf(out, out_len, "%u", (unsigned)s_mem.heap_largest_free_block_bytes);
        return true;
    }
    if (strcmp(key, "heap_internal") == 0) {
        snprintf(out, out_len, "%u", (unsigned)s_mem.heap_internal_free_bytes);
        return true;
    }
    if (strcmp(key, "psram_free") == 0) {
        snprintf(out, out_len, "%u", (unsigned)s_mem.psram_free_bytes);
        return true;
    }
    if (strcmp(key, "psram_min") == 0) {
        snprintf(out, out_len, "%u", (unsigned)s_mem.psram_min_free_bytes);
        return true;
    }
    if (strcmp(key, "psram_largest") == 0) {
#if TELEMETRY_ALLOW_PSRAM_POOL_WALK
        snprintf(out, out_len, "%u", (unsigned)s_mem.psram_largest_free_block_bytes);
        return true;
#else
        return false;
#endif
    }
    // --- Memory totals & used (static after boot) ---
    if (strcmp(key, "heap_total") == 0) {
        snprintf(out, out_len, "%u", (unsigned)s_static.heap_total);
        return true;
    }
    if (strcmp(key, "heap_internal_total") == 0) {
        snprintf(out, out_len, "%u", (unsigned)s_static.internal_total);
        return true;
    }
    if (strcmp(key, "heap_internal_used") == 0) {
        uint32_t used = s_static.internal_total > s_mem.heap_internal_free_bytes
                      ? s_static.internal_total - (uint32_t)s_mem.heap_internal_free_bytes : 0;
        snprintf(out, out_len, "%u", (unsigned)used);
        return true;
    }
    if (strcmp(key, "psram_total") == 0) {
        snprintf(out, out_len, "%u", (unsigned)s_static.psram_total);
        return true;
    }
    if (strcmp(key, "psram_used") == 0) {
        uint32_t used = s_static.psram_total > s_mem.psram_free_bytes
                      ? s_static.psram_total - (uint32_t)s_mem.psram_free_bytes : 0;
        snprintf(out, out_len, "%u", (unsigned)used);
        return true;
    }
    // --- Static device info (cached once at init) ---
    if (strcmp(key, "chip") == 0) {
        strlcpy(out, s_static.chip, out_len);
        return true;
    }
    if (strcmp(key, "chip_rev") == 0) {
        snprintf(out, out_len, "%d", s_static.chip_rev);
        return true;
    }
    if (strcmp(key, "chip_cores") == 0) {
        snprintf(out, out_len, "%d", s_static.chip_cores);
        return true;
    }
    if (strcmp(key, "cpu_freq") == 0) {
        snprintf(out, out_len, "%d", s_static.cpu_freq);
        return true;
    }
    if (strcmp(key, "flash_size") == 0) {
        snprintf(out, out_len, "%u", (unsigned)s_static.flash_size);
        return true;
    }
    if (strcmp(key, "firmware") == 0) {
        strlcpy(out, FIRMWARE_VERSION, out_len);
        return true;
    }
    if (strcmp(key, "board") == 0) {
#ifdef BUILD_BOARD_NAME
        strlcpy(out, BUILD_BOARD_NAME, out_len);
#else
        strlcpy(out, "unknown", out_len);
#endif
        return true;
    }
    if (strcmp(key, "mac") == 0) {
        strlcpy(out, s_static.mac, out_len);
        return true;
    }
    if (strcmp(key, "reset_reason") == 0) {
        esp_reset_reason_t r = esp_reset_reason();
        const char* s = "Unknown";
        switch (r) {
            case ESP_RST_POWERON:   s = "Power On"; break;
            case ESP_RST_SW:        s = "Software"; break;
            case ESP_RST_PANIC:     s = "Panic"; break;
            case ESP_RST_INT_WDT:   s = "Interrupt WDT"; break;
            case ESP_RST_TASK_WDT:  s = "Task WDT"; break;
            case ESP_RST_WDT:       s = "WDT"; break;
            case ESP_RST_DEEPSLEEP: s = "Deep Sleep"; break;
            case ESP_RST_BROWNOUT:  s = "Brownout"; break;
            case ESP_RST_SDIO:      s = "SDIO"; break;
            default: break;
        }
        strlcpy(out, s, out_len);
        return true;
    }
    if (strcmp(key, "wifi_connected") == 0) {
        strlcpy(out, s_wifi_connected ? "ON" : "OFF", out_len);
        return true;
    }
    if (strcmp(key, "wifi_ssid") == 0) {
        strlcpy(out, s_wifi_connected ? s_wifi_ssid : "?", out_len);
        return true;
    }
    if (strcmp(key, "ip") == 0) {
        strlcpy(out, s_ip[0] ? s_ip : "0.0.0.0", out_len);
        return true;
    }
    if (strcmp(key, "hostname") == 0) {
        const char* h = WiFi.getHostname();
        strlcpy(out, h ? h : "?", out_len);
        return true;
    }
    // --- Display & audio ---
    if (strcmp(key, "brightness") == 0) {
        snprintf(out, out_len, "%d", (int)display_manager_get_backlight_brightness());
        return true;
    }
#if HAS_AUDIO
    if (strcmp(key, "volume") == 0) {
        snprintf(out, out_len, "%d", (int)audio_get_volume());
        return true;
    }
#endif
#if HAS_BLE_HID
    if (strcmp(key, "ble_status") == 0) {
        strlcpy(out, device_config.ble_enabled ? ble_hid_status() : "disabled", out_len);
        return true;
    }
    if (strcmp(key, "ble_state") == 0) {
        strlcpy(out, device_config.ble_enabled ? ble_hid_state() : "disabled", out_len);
        return true;
    }
    if (strcmp(key, "ble_name") == 0) {
        if (!device_config.ble_enabled) {
            strlcpy(out, "?", out_len);
            return true;
        }
        const char* name = ble_hid_name();
        strlcpy(out, (name && name[0]) ? name : "?", out_len);
        return true;
    }
    if (strcmp(key, "ble_pairing") == 0) {
        strlcpy(out, ble_hid_is_pairing() ? "ON" : "OFF", out_len);
        return true;
    }
    if (strcmp(key, "ble_bonded") == 0) {
        strlcpy(out, ble_hid_is_bonded() ? "ON" : "OFF", out_len);
        return true;
    }
    if (strcmp(key, "ble_encrypted") == 0) {
        strlcpy(out, ble_hid_is_encrypted() ? "ON" : "OFF", out_len);
        return true;
    }
    if (strcmp(key, "ble_peer_addr") == 0) {
        const char* a = ble_hid_peer_addr();
        strlcpy(out, (a && a[0]) ? a : "?", out_len);
        return true;
    }
    if (strcmp(key, "ble_peer_id_addr") == 0) {
        const char* a = ble_hid_peer_id_addr();
        strlcpy(out, (a && a[0]) ? a : "?", out_len);
        return true;
    }
#endif
    return false;
}

// ============================================================================
// Scheme resolver — called by binding_template_resolve()
// ============================================================================

static bool health_binding_resolve(const char* params, char* out, size_t out_len) {
    char key[32];
    char fmt[32];
    parse_health_params(params, key, sizeof(key), fmt, sizeof(fmt));

    if (!key[0]) {
        strlcpy(out, "ERR:no key", out_len);
        return false;
    }

    ensure_static_initialized();
    refresh_if_stale();
    if (health_key_requires_memory_snapshot(key)) {
        refresh_memory_if_stale();
    }

    if (strcmp(key, "table") == 0 || strcmp(key, "extended_table") == 0) {
        bool extended = (strcmp(key, "extended_table") == 0);
        return health_table_build(extended, lookup_value, out, out_len);
    }

    char raw[64];
    if (!lookup_value(key, raw, sizeof(raw))) {
        strlcpy(out, "ERR:bad key", out_len);
        return false;
    }

    if (fmt[0]) {
        // Best-effort printf format: try int first, fall back to string
        char* end = nullptr;
        long lval = strtol(raw, &end, 10);
        if (end && *end == '\0') {
            snprintf(out, out_len, fmt, lval);
        } else {
            snprintf(out, out_len, fmt, raw);
        }
    } else {
        strlcpy(out, raw, out_len);
    }
    return true;
}

// ============================================================================
// No-op topic collector — health data is local, no subscriptions needed
// ============================================================================

static void health_binding_collect(const char* params, void* user_data) {
    (void)params;
    (void)user_data;
}

// ============================================================================
// Init — register the "health" scheme
// ============================================================================

// Canonical [health:key] table, kept beside the resolver ladder so the MCP
// capability manifest can enumerate keys AND their meaning without a
// hand-duplicated copy. Gated on HAS_MCP — only the manifest consumes it.
#if HAS_MCP
#include <ArduinoJson.h>

struct HealthKeyDef { const char* key; const char* desc; };
static const HealthKeyDef HEALTH_KEYS[] = {
    {"cpu",                 "CPU usage % (0-100)"},
    {"cpu_core_0",          "Core 0 CPU usage % (0-100; ? if unavailable)"},
    {"cpu_core_1",          "Core 1 CPU usage % (0-100; ? if unavailable)"},
    {"rssi",                "WiFi signal strength, dBm"},
    {"uptime",              "seconds since boot"},
    {"chip",                "SoC model, e.g. ESP32-S3"},
    {"chip_rev",            "silicon revision number"},
    {"chip_cores",          "CPU core count"},
    {"cpu_freq",            "CPU clock, MHz"},
    {"flash_size",          "flash chip size, bytes"},
    {"firmware",            "firmware version string"},
    {"board",              "board name used at build time"},
    {"mac",                 "WiFi MAC address"},
    {"reset_reason",        "last reset cause"},
    {"heap_total",          "total heap, bytes"},
    {"heap_free",           "free heap, bytes"},
    {"heap_min",            "heap low-water mark, bytes"},
    {"heap_largest",        "largest free heap block, bytes"},
    {"heap_internal_total", "total internal RAM, bytes"},
    {"heap_internal",       "free internal RAM, bytes"},
    {"heap_internal_used",  "used internal RAM, bytes"},
    {"psram_total",         "total PSRAM, bytes (0 if absent)"},
    {"psram_free",          "free PSRAM, bytes"},
    {"psram_used",          "used PSRAM, bytes"},
    {"psram_min",           "PSRAM low-water mark, bytes"},
#if TELEMETRY_ALLOW_PSRAM_POOL_WALK
    {"psram_largest",       "largest free PSRAM block, bytes"},
#else
    {"psram_largest",       "unavailable on MIPI-DSI boards; use a pipe fallback"},
#endif
    {"wifi_connected",      "ON/OFF WiFi connection state"},
    {"wifi_ssid",           "connected network name"},
    {"ip",                  "device IP address"},
    {"hostname",            "device hostname"},
    {"brightness",          "backlight brightness 0-100"},
    {"volume",              "audio volume 0-100"},
    {"table",               "structured table payload (for the table widget)"},
    {"extended_table",      "structured table payload, extended schema"},
    {"ble_status",          "compact BLE status (disabled/ready/pairing/connected/error)"},
    {"ble_name",            "current BLE keyboard name"},
    {"ble_state",           "detailed BLE state"},
    {"ble_pairing",         "ON/OFF BLE pairing-mode active"},
    {"ble_bonded",          "ON/OFF current connection bonded"},
    {"ble_encrypted",       "ON/OFF current connection encrypted"},
    {"ble_peer_addr",       "connected peer Bluetooth address"},
    {"ble_peer_id_addr",    "connected peer identity address"},
};

uint8_t health_binding_key_count() {
    return (uint8_t)(sizeof(HEALTH_KEYS) / sizeof(HEALTH_KEYS[0]));
}

const char* health_binding_key_at(uint8_t index) {
    return (index < health_binding_key_count()) ? HEALTH_KEYS[index].key : nullptr;
}

const char* health_binding_key_desc_at(uint8_t index) {
    return (index < health_binding_key_count()) ? HEALTH_KEYS[index].desc : nullptr;
}

// Self-description for the MCP capability manifest (lives with the scheme).
static void health_scheme_describe(void* out) {
    JsonObject& o = *static_cast<JsonObject*>(out);
    o["syntax"]  = "[health:key;format]";
    o["example"] = "[health:heap_free;%d]";
    o["keys"]    = "see health_keys[] for the full {name, desc} list";
}

// Validate a [health:KEY] token's params (the key, already stripped of any
// ;format / |fallback by the caller). Lives with the scheme so the key set is
// the single source for both the manifest and authoring validation.
static char s_health_verr[80];
static const char* health_scheme_validate(const char* params) {
    if (!params || !params[0]) return nullptr;
    for (uint8_t i = 0; i < health_binding_key_count(); ++i) {
        const char* hk = health_binding_key_at(i);
        if (hk && strcmp(hk, params) == 0) return nullptr;
    }
    snprintf(s_health_verr, sizeof(s_health_verr),
             "unknown health key '%s' — use a key from capabilities.health_keys", params);
    return s_health_verr;
}
#else
uint8_t health_binding_key_count() { return 0; }
const char* health_binding_key_at(uint8_t index) { (void)index; return nullptr; }
const char* health_binding_key_desc_at(uint8_t index) { (void)index; return nullptr; }
#endif // HAS_MCP

void health_binding_init() {
    if (!binding_template_register("health", health_binding_resolve, health_binding_collect)) {
        LOGE(TAG, "Failed to register health binding scheme");
    }
#if HAS_MCP
    binding_template_set_scheme_describe("health", health_scheme_describe);
    binding_template_set_scheme_validate("health", health_scheme_validate);
#endif
}

#else // !HAS_DISPLAY

uint8_t health_binding_key_count() { return 0; }
const char* health_binding_key_at(uint8_t index) { (void)index; return nullptr; }
const char* health_binding_key_desc_at(uint8_t index) { (void)index; return nullptr; }
void health_binding_init() {}

#endif
