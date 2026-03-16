#include "health_binding.h"
#include "board_config.h"

#if HAS_DISPLAY

#include "binding_template.h"
#include "device_telemetry.h"
#include "log_manager.h"

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

#define TAG "HealthBind"

// ============================================================================
// Cached telemetry snapshot — refreshed at most every 2 s
// ============================================================================

#define HEALTH_BINDING_REFRESH_MS 2000

static uint32_t s_last_refresh_ms = 0;
static int s_cpu = -1;
static int16_t s_rssi = 0;
static bool s_rssi_valid = false;
static DeviceMemorySnapshot s_mem = {};

static void refresh_if_stale() {
    uint32_t now = millis();
    if (now - s_last_refresh_ms < HEALTH_BINDING_REFRESH_MS && s_last_refresh_ms != 0) return;
    s_last_refresh_ms = now;

    s_cpu = device_telemetry_get_cpu_usage();
    s_rssi = device_telemetry_get_cached_rssi(&s_rssi_valid);
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

// ============================================================================
// Key→value lookup
// ============================================================================

static bool lookup_value(const char* key, char* out, size_t out_len) {
    if (strcmp(key, "cpu") == 0) {
        if (s_cpu < 0) { strlcpy(out, "?", out_len); return true; }
        snprintf(out, out_len, "%d", s_cpu);
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
        snprintf(out, out_len, "%u", (unsigned)s_mem.psram_largest_free_block_bytes);
        return true;
    }
    if (strcmp(key, "wifi_connected") == 0) {
        strlcpy(out, WiFi.status() == WL_CONNECTED ? "ON" : "OFF", out_len);
        return true;
    }
    if (strcmp(key, "wifi_ssid") == 0) {
        if (WiFi.status() == WL_CONNECTED) {
            strlcpy(out, WiFi.SSID().c_str(), out_len);
        } else {
            strlcpy(out, "?", out_len);
        }
        return true;
    }
    if (strcmp(key, "ip") == 0) {
        strlcpy(out, WiFi.localIP().toString().c_str(), out_len);
        return true;
    }
    if (strcmp(key, "hostname") == 0) {
        const char* h = WiFi.getHostname();
        strlcpy(out, h ? h : "?", out_len);
        return true;
    }
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

    refresh_if_stale();

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

void health_binding_init() {
    if (!binding_template_register("health", health_binding_resolve, health_binding_collect)) {
        LOGE(TAG, "Failed to register health binding scheme");
    }
}

#else // !HAS_DISPLAY

void health_binding_init() {}

#endif
