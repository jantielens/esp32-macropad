#include "mqtt_sub_store.h"

#if HAS_MQTT

#include "log_manager.h"
#include "mqtt_manager.h"
#include "pad_config.h"

#include <ArduinoJson.h>
#include "psram_json_allocator.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <esp_heap_caps.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

#define TAG "SubStore"

// ============================================================================
// Store entry
// ============================================================================

struct SubStoreEntry {
    char topic[CONFIG_MQTT_TOPIC_MAX_LEN];
    char value[MQTT_SUB_STORE_MAX_VALUE_LEN];
    bool dirty;      // set by store_set, cleared by store_get
    bool in_use;
    bool truncated;  // payload exceeded MAX_VALUE_LEN on ingestion
};

static SubStoreEntry* g_entries = nullptr;
static SemaphoreHandle_t g_mutex = nullptr;

// ============================================================================
// Init
// ============================================================================

void mqtt_sub_store_init() {
    if (g_entries) return; // already initialized

    g_entries = (SubStoreEntry*)heap_caps_calloc(
        MQTT_SUB_STORE_MAX_ENTRIES, sizeof(SubStoreEntry),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!g_entries) {
        g_entries = (SubStoreEntry*)calloc(MQTT_SUB_STORE_MAX_ENTRIES, sizeof(SubStoreEntry));
    }
    if (!g_entries) {
        LOGE(TAG, "OOM: failed to allocate store");
        return;
    }

    g_mutex = xSemaphoreCreateMutex();
    LOGI(TAG, "Initialized (%d entries, %u bytes)", MQTT_SUB_STORE_MAX_ENTRIES,
         (unsigned)(MQTT_SUB_STORE_MAX_ENTRIES * sizeof(SubStoreEntry)));
}

// ============================================================================
// Subscribe all — scan pad configs for unique topics
// ============================================================================

void mqtt_sub_store_subscribe_all() {
    if (!g_entries || !g_mutex) return;

    // Collect unique topics from all pad page label bindings
    // Temporary topic list (stack allocated, small)
    struct TopicEntry { char topic[CONFIG_MQTT_TOPIC_MAX_LEN]; };

    // Use a reasonable max — 64 buttons * 3 labels * 8 pages = 1536, but most are empty.
    // We deduplicate on the fly, so 64 unique topics is generous.
    static const int MAX_UNIQUE = MQTT_SUB_STORE_MAX_ENTRIES;
    int unique_count = 0;

    // Allocate temp list in PSRAM
    TopicEntry* unique = (TopicEntry*)heap_caps_malloc(
        MAX_UNIQUE * sizeof(TopicEntry), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!unique) {
        unique = (TopicEntry*)malloc(MAX_UNIQUE * sizeof(TopicEntry));
    }
    if (!unique) {
        LOGE(TAG, "OOM for topic dedup list");
        return;
    }

    // Helper lambda to add topic if not already in list
    auto add_unique = [&](const char* topic) {
        if (!topic[0]) return;
        for (int i = 0; i < unique_count; i++) {
            if (strcmp(unique[i].topic, topic) == 0) return;
        }
        if (unique_count >= MAX_UNIQUE) return;
        strlcpy(unique[unique_count].topic, topic, CONFIG_MQTT_TOPIC_MAX_LEN);
        unique_count++;
    };

    // Temp config buffer
    PadPageConfig* cfg = (PadPageConfig*)heap_caps_malloc(
        sizeof(PadPageConfig), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!cfg) cfg = (PadPageConfig*)malloc(sizeof(PadPageConfig));
    if (!cfg) {
        free(unique);
        return;
    }

    for (uint8_t page = 0; page < MAX_PAD_PAGES; page++) {
        if (!pad_config_load(page, cfg)) continue;
        for (uint8_t b = 0; b < cfg->button_count; b++) {
            const ScreenButtonConfig& btn = cfg->buttons[b];
            add_unique(btn.label_top_bind.mqtt_topic);
            add_unique(btn.label_center_bind.mqtt_topic);
            add_unique(btn.label_bottom_bind.mqtt_topic);
            add_unique(btn.state_bind.mqtt_topic);
            add_unique(btn.widget.data_topic);
        }
    }

    free(cfg);

    // Update store entries and subscribe
    if (xSemaphoreTake(g_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        // Mark all entries as unused, then re-add unique topics
        for (int i = 0; i < MQTT_SUB_STORE_MAX_ENTRIES; i++) {
            g_entries[i].in_use = false;
        }

        for (int t = 0; t < unique_count; t++) {
            // Find or allocate a slot
            int slot = -1;
            for (int i = 0; i < MQTT_SUB_STORE_MAX_ENTRIES; i++) {
                if (!g_entries[i].in_use) {
                    slot = i;
                    break;
                }
            }
            if (slot < 0) {
                LOGW(TAG, "Store full, skipping topic: %s", unique[t].topic);
                break;
            }

            // Preserve existing value if topic was already tracked
            bool existed = (strcmp(g_entries[slot].topic, unique[t].topic) == 0);
            if (!existed) {
                strlcpy(g_entries[slot].topic, unique[t].topic, CONFIG_MQTT_TOPIC_MAX_LEN);
                g_entries[slot].value[0] = '\0';
                g_entries[slot].dirty = false;
            }
            g_entries[slot].in_use = true;
        }
        xSemaphoreGive(g_mutex);
    }

    // Subscribe to each unique topic via MQTT manager
    for (int t = 0; t < unique_count; t++) {
        mqtt_manager.subscribe(unique[t].topic);
    }

    LOGI(TAG, "Subscribed to %d unique topics", unique_count);
    free(unique);
}

// ============================================================================
// Resubscribe — called on MQTT reconnect
// ============================================================================

void mqtt_sub_store_resubscribe() {
    if (!g_entries || !g_mutex) return;

    int count = 0;
    if (xSemaphoreTake(g_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        for (int i = 0; i < MQTT_SUB_STORE_MAX_ENTRIES; i++) {
            if (g_entries[i].in_use && g_entries[i].topic[0]) {
                mqtt_manager.subscribe(g_entries[i].topic);
                count++;
            }
        }
        xSemaphoreGive(g_mutex);
    }

    if (count > 0) {
        LOGI(TAG, "Re-subscribed to %d topics", count);
    }
}

// ============================================================================
// Set — called from MQTT message callback (network thread)
// ============================================================================

void mqtt_sub_store_set(const char* topic, const uint8_t* payload, unsigned int len) {
    if (!g_entries || !g_mutex || !topic) return;

    if (xSemaphoreTake(g_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        for (int i = 0; i < MQTT_SUB_STORE_MAX_ENTRIES; i++) {
            if (g_entries[i].in_use && strcmp(g_entries[i].topic, topic) == 0) {
                bool was_truncated = (len >= MQTT_SUB_STORE_MAX_VALUE_LEN);
                size_t copy_len = len;
                if (copy_len >= MQTT_SUB_STORE_MAX_VALUE_LEN) {
                    copy_len = MQTT_SUB_STORE_MAX_VALUE_LEN - 1;
                }
                memcpy(g_entries[i].value, payload, copy_len);
                g_entries[i].value[copy_len] = '\0';
                g_entries[i].dirty = true;
                g_entries[i].truncated = was_truncated;
                if (was_truncated) {
                    LOGW(TAG, "Payload truncated for %s (%u > %d)",
                         topic, (unsigned)len, MQTT_SUB_STORE_MAX_VALUE_LEN - 1);
                }
                break;
            }
        }
        xSemaphoreGive(g_mutex);
    }
}

// ============================================================================
// Get — called from LVGL task
// ============================================================================

bool mqtt_sub_store_get(const char* topic, char* buf, size_t buf_len, bool* changed, bool* truncated) {
    if (!g_entries || !g_mutex || !topic || !buf || buf_len == 0) {
        if (buf && buf_len > 0) buf[0] = '\0';
        if (changed) *changed = false;
        if (truncated) *truncated = false;
        return false;
    }

    bool found = false;
    if (xSemaphoreTake(g_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        for (int i = 0; i < MQTT_SUB_STORE_MAX_ENTRIES; i++) {
            if (g_entries[i].in_use && strcmp(g_entries[i].topic, topic) == 0) {
                strlcpy(buf, g_entries[i].value, buf_len);
                if (changed) *changed = g_entries[i].dirty;
                if (truncated) *truncated = g_entries[i].truncated;
                // Dirty flag is NOT cleared here — caller must invoke
                // mqtt_sub_store_clear_dirty() after all consumers have polled.
                found = true;
                break;
            }
        }
        xSemaphoreGive(g_mutex);
    }

    if (!found) {
        buf[0] = '\0';
        if (changed) *changed = false;
        if (truncated) *truncated = false;
    }
    return found;
}

// ============================================================================
// Clear all dirty flags — call once per poll cycle
// ============================================================================

void mqtt_sub_store_clear_dirty() {
    if (!g_entries || !g_mutex) return;
    if (xSemaphoreTake(g_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        for (int i = 0; i < MQTT_SUB_STORE_MAX_ENTRIES; i++) {
            if (g_entries[i].in_use) g_entries[i].dirty = false;
        }
        xSemaphoreGive(g_mutex);
    }
}

// ============================================================================
// JSON path extraction
// ============================================================================

bool mqtt_sub_store_extract_json(const char* payload, const char* path, char* out, size_t out_len) {
    if (!payload || !path || !out || out_len == 0) return false;

    // "." means raw value
    if (path[0] == '.' && path[1] == '\0') {
        strlcpy(out, payload, out_len);
        return true;
    }

    // Parse JSON (PSRAM-backed document for large payloads)
    BasicJsonDocument<PsramJsonAllocator> doc(2048);
    DeserializationError err = deserializeJson(doc, payload);
    if (err) {
        // Not valid JSON — return raw payload
        strlcpy(out, payload, out_len);
        return false;
    }

    // Walk dot-separated path: "a.b.c" → doc["a"]["b"]["c"]
    JsonVariant v = doc.as<JsonVariant>();
    char path_buf[CONFIG_JSON_PATH_MAX_LEN];
    strlcpy(path_buf, path, sizeof(path_buf));

    char* save = nullptr;
    char* token = strtok_r(path_buf, ".", &save);
    while (token && !v.isNull()) {
        if (v.is<JsonObject>()) {
            v = v[token];
        } else {
            v = JsonVariant(); // path doesn't match
        }
        token = strtok_r(nullptr, ".", &save);
    }

    if (v.isNull()) {
        out[0] = '\0';
        return false;
    }

    // Convert to string
    if (v.is<const char*>()) {
        strlcpy(out, v.as<const char*>(), out_len);
    } else if (v.is<float>() || v.is<double>()) {
        snprintf(out, out_len, "%g", v.as<double>());
    } else if (v.is<bool>()) {
        strlcpy(out, v.as<bool>() ? "true" : "false", out_len);
    } else {
        // Serialize whatever it is
        serializeJson(v, out, out_len);
    }

    return true;
}

// ============================================================================
// Format value (best effort — never crashes)
// ============================================================================

void mqtt_sub_store_format_value(const char* raw_value, const char* format, char* out, size_t out_len) {
    if (!raw_value || !out || out_len == 0) return;

    // No format string — copy raw value
    if (!format || format[0] == '\0') {
        strlcpy(out, raw_value, out_len);
        return;
    }

    // Safety: scan format for exactly one % conversion specifier.
    // If more than one or none found, fall back to raw value.
    int pct_count = 0;
    bool has_star = false;
    for (const char* p = format; *p; p++) {
        if (*p == '%') {
            if (*(p + 1) == '%') { p++; continue; } // escaped %%
            pct_count++;
            // Scan conversion specifier
            p++;
            while (*p && strchr("-+ #0", *p)) p++; // flags
            if (*p == '*') { has_star = true; break; }
            while (*p >= '0' && *p <= '9') p++; // width
            if (*p == '.') {
                p++;
                while (*p >= '0' && *p <= '9') p++; // precision
            }
            // length modifiers — skip for safety
            // conversion char
            if (!*p) break;
        }
    }

    if (pct_count != 1 || has_star) {
        // Bad format — just copy raw
        strlcpy(out, raw_value, out_len);
        return;
    }

    // Detect the conversion character to decide how to parse the value
    char conv = '\0';
    for (const char* p = format; *p; p++) {
        if (*p == '%') {
            if (*(p + 1) == '%') { p++; continue; }
            p++;
            while (*p && strchr("-+ #0", *p)) p++;
            while (*p >= '0' && *p <= '9') p++;
            if (*p == '.') { p++; while (*p >= '0' && *p <= '9') p++; }
            // Skip length modifiers (l, ll, h, etc.)
            while (*p && strchr("hlLzjt", *p)) p++;
            conv = *p;
            break;
        }
    }

    if (conv == 's') {
        // String formatting
        snprintf(out, out_len, format, raw_value);
    } else if (conv == 'd' || conv == 'i' || conv == 'u' || conv == 'x' || conv == 'X' || conv == 'o') {
        // Integer formatting
        char* end = nullptr;
        long val = strtol(raw_value, &end, 10);
        if (end == raw_value) {
            // Parse failed — try as float→int
            double dval = strtod(raw_value, &end);
            if (end == raw_value) {
                strlcpy(out, raw_value, out_len);
                return;
            }
            val = (long)dval;
        }
        snprintf(out, out_len, format, val);
    } else if (conv == 'f' || conv == 'F' || conv == 'e' || conv == 'E' ||
               conv == 'g' || conv == 'G' || conv == 'a' || conv == 'A') {
        // Float formatting
        char* end = nullptr;
        double val = strtod(raw_value, &end);
        if (end == raw_value) {
            strlcpy(out, raw_value, out_len);
            return;
        }
        if (!isfinite(val)) {
            strlcpy(out, raw_value, out_len);
            return;
        }
        snprintf(out, out_len, format, val);
    } else {
        // Unknown conversion — fall back to raw
        strlcpy(out, raw_value, out_len);
    }
}

#endif // HAS_MQTT
