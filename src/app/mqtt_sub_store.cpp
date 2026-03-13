#include "mqtt_sub_store.h"

#if HAS_MQTT

#include "binding_template.h"
#include "config_manager.h"
#include "log_manager.h"
#include "mqtt_manager.h"
#include "pad_binding.h"
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
// Parse MQTT binding params: "topic;path;format"
// Splits on first two ';' — everything after the second is format (may contain ';').
// ============================================================================

static void parse_mqtt_params(const char* params,
                              char* topic, size_t topic_len,
                              char* path, size_t path_len,
                              char* fmt, size_t fmt_len) {
    topic[0] = '\0';
    path[0] = '\0';
    fmt[0] = '\0';
    if (!params || !params[0]) return;

    // Find first ';'
    const char* s1 = strchr(params, ';');
    if (!s1) {
        // Only topic
        strlcpy(topic, params, topic_len);
        return;
    }
    size_t tlen = (size_t)(s1 - params);
    if (tlen >= topic_len) tlen = topic_len - 1;
    memcpy(topic, params, tlen);
    topic[tlen] = '\0';

    // Find second ';'
    const char* s2 = strchr(s1 + 1, ';');
    if (!s2) {
        // topic;path
        strlcpy(path, s1 + 1, path_len);
        return;
    }
    size_t plen = (size_t)(s2 - (s1 + 1));
    if (plen >= path_len) plen = path_len - 1;
    memcpy(path, s1 + 1, plen);
    path[plen] = '\0';

    // Everything after second ';' is format (may contain ';')
    strlcpy(fmt, s2 + 1, fmt_len);
}

// ============================================================================
// MQTT binding scheme resolver — called by binding_template_resolve()
// ============================================================================

static bool mqtt_binding_resolve(const char* params, char* out, size_t out_len) {
    char topic[CONFIG_MQTT_TOPIC_MAX_LEN];
    char path[CONFIG_JSON_PATH_MAX_LEN];
    char fmt[CONFIG_FORMAT_MAX_LEN];

    parse_mqtt_params(params, topic, sizeof(topic), path, sizeof(path), fmt, sizeof(fmt));

    if (!topic[0]) {
        strlcpy(out, "ERR:no topic", out_len);
        return false;
    }

    static char payload[MQTT_SUB_STORE_MAX_VALUE_LEN];
    bool truncated = false;
    if (!mqtt_sub_store_get(topic, payload, sizeof(payload), nullptr, &truncated)) {
        return false; // Not yet received — caller shows placeholder
    }

    // Extract value from JSON payload
    const char* jp = (path[0]) ? path : ".";
    char extracted[128];
    if (!mqtt_sub_store_extract_json(payload, jp, extracted, sizeof(extracted))) {
        strlcpy(extracted, truncated ? "ERR:too big" : payload, sizeof(extracted));
    }

    // Apply format string (best-effort)
    if (fmt[0]) {
        mqtt_sub_store_format_value(extracted, fmt, out, out_len);
    } else {
        strlcpy(out, extracted, out_len);
    }
    return true;
}

// ============================================================================
// MQTT binding scheme topic collector — called by binding_template_collect_topics()
// ============================================================================

// user_data points to a TopicCollectorCtx (defined in subscribe_all)
struct TopicCollectorCtx {
    struct Entry { char topic[CONFIG_MQTT_TOPIC_MAX_LEN]; };
    Entry* list;
    int* count;
    int max;
};

static void mqtt_binding_collect(const char* params, void* user_data) {
    char topic[CONFIG_MQTT_TOPIC_MAX_LEN];
    char path[CONFIG_JSON_PATH_MAX_LEN];
    char fmt[CONFIG_FORMAT_MAX_LEN];

    parse_mqtt_params(params, topic, sizeof(topic), path, sizeof(path), fmt, sizeof(fmt));

    if (!topic[0] || !user_data) return;

    TopicCollectorCtx* ctx = (TopicCollectorCtx*)user_data;
    // Deduplicate
    for (int i = 0; i < *ctx->count; i++) {
        if (strcmp(ctx->list[i].topic, topic) == 0) return;
    }
    if (*ctx->count >= ctx->max) return;
    strlcpy(ctx->list[*ctx->count].topic, topic, CONFIG_MQTT_TOPIC_MAX_LEN);
    (*ctx->count)++;
}

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

    // Register "mqtt" scheme with the binding template engine
    binding_template_register("mqtt", mqtt_binding_resolve, mqtt_binding_collect);
}

// ============================================================================
// Subscribe all — scan pad configs for unique topics
// ============================================================================

void mqtt_sub_store_subscribe_all() {
    if (!g_entries || !g_mutex) return;

    // Collect unique topics from all pad page label bindings and state/widget bindings.
    static const int MAX_UNIQUE = MQTT_SUB_STORE_MAX_ENTRIES;
    int unique_count = 0;

    // Allocate temp list in PSRAM
    typedef TopicCollectorCtx::Entry TopicEntry;
    TopicEntry* unique = (TopicEntry*)heap_caps_malloc(
        MAX_UNIQUE * sizeof(TopicEntry), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!unique) {
        unique = (TopicEntry*)malloc(MAX_UNIQUE * sizeof(TopicEntry));
    }
    if (!unique) {
        LOGE(TAG, "OOM for topic dedup list");
        return;
    }

    // Context for binding_template_collect_topics
    TopicCollectorCtx ctx;
    ctx.list = unique;
    ctx.count = &unique_count;
    ctx.max = MAX_UNIQUE;

    // Helper lambda to add a plain topic string (for widget data)
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
    PadConfig* cfg = (PadConfig*)heap_caps_malloc(
        sizeof(PadConfig), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!cfg) cfg = (PadConfig*)malloc(sizeof(PadConfig));
    if (!cfg) {
        free(unique);
        return;
    }

    for (uint8_t page = 0; page < MAX_PADS; page++) {
        if (!pad_config_load(page, cfg)) continue;
        // Set page context so [pad:] collector can resolve named bindings
        pad_binding_set_page(cfg);
        // Scan page-level background color for binding tokens
        binding_template_collect_topics(cfg->bg_color, &ctx);
        for (uint8_t b = 0; b < cfg->button_count; b++) {
            const ScreenButtonConfig& btn = cfg->buttons[b];
            // Scan label text for [mqtt:...] binding tokens
            binding_template_collect_topics(btn.label_top, &ctx);
            binding_template_collect_topics(btn.label_center, &ctx);
            binding_template_collect_topics(btn.label_bottom, &ctx);
            // Scan color fields for binding tokens
            binding_template_collect_topics(btn.bg_color, &ctx);
            binding_template_collect_topics(btn.fg_color, &ctx);
            binding_template_collect_topics(btn.border_color, &ctx);
            for (uint8_t i = 0; i < MAX_WIDGET_BINDINGS; i++) {
                binding_template_collect_topics(btn.widget.data_binding[i], &ctx);
            }
        }
    }
    pad_binding_set_page(nullptr);

    free(cfg);

    // Scan screen saver wake binding for MQTT topics
    {
        const DeviceConfig* dcfg = mqtt_manager.config();
        if (dcfg && strlen(dcfg->screen_saver_wake_binding) > 0) {
            binding_template_collect_topics(dcfg->screen_saver_wake_binding, &ctx);
        }
    }

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
