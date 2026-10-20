#include "mqtt_sub_store.h"

#if HAS_MQTT

#include "binding_template.h"
#include "binding_sources.h"
#include "config_manager.h"
#include "log_manager.h"
#include "mqtt_manager.h"
#include "pad_config.h"
#if HAS_CAMERA
#include "camera_motion_actions.h"
#endif

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <esp_heap_caps.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

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

// user_data points to a TopicCollectorCtx (defined in subscribe_all)
struct TopicCollectorCtx {
    struct Entry { char topic[CONFIG_MQTT_TOPIC_MAX_LEN]; };
    Entry* list;
    int* count;
    int max;
};

void mqtt_sub_store_collect_topic(void* user_data, const char* topic) {
    if (!user_data || !topic || !topic[0]) return;
    TopicCollectorCtx* ctx = (TopicCollectorCtx*)user_data;
    for (int i = 0; i < *ctx->count; i++) {
        if (strcmp(ctx->list[i].topic, topic) == 0) return;
    }
    if (*ctx->count >= ctx->max) return;
    strlcpy(ctx->list[*ctx->count].topic, topic, CONFIG_MQTT_TOPIC_MAX_LEN);
    (*ctx->count)++;
}

bool mqtt_sub_store_active() { return g_entries != nullptr; }

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

#if HAS_DISPLAY
    binding_sources_visit(binding_template_collect_topics, &ctx);

    // Scan screen saver wake binding for MQTT topics
    {
        const DeviceConfig* dcfg = mqtt_manager.config();
        if (dcfg && strlen(dcfg->screen_saver_wake_binding) > 0) {
            binding_template_collect_topics(dcfg->screen_saver_wake_binding, &ctx);
        }
    }
#if CAMERA_MOTION_ACTIONS_ENABLED
    camera_motion_actions_collect_binding_topics(&ctx);
#endif
#endif // HAS_DISPLAY

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
// Lazy single-topic subscribe (live preview of not-yet-saved bindings)
// ============================================================================

bool mqtt_sub_store_ensure_subscribed(const char* topic) {
    if (!g_entries || !g_mutex || !topic || !topic[0]) return false;
    bool need_sub = false;
    if (xSemaphoreTake(g_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        int slot = -1;
        for (int i = 0; i < MQTT_SUB_STORE_MAX_ENTRIES; i++) {
            if (g_entries[i].in_use && strcmp(g_entries[i].topic, topic) == 0) { slot = i; break; }
        }
        if (slot < 0) {
            for (int i = 0; i < MQTT_SUB_STORE_MAX_ENTRIES; i++) {
                if (!g_entries[i].in_use) { slot = i; break; }
            }
            if (slot >= 0) {
                strlcpy(g_entries[slot].topic, topic, CONFIG_MQTT_TOPIC_MAX_LEN);
                g_entries[slot].value[0] = '\0';
                g_entries[slot].dirty = false;
                g_entries[slot].truncated = false;
                g_entries[slot].in_use = true;
                need_sub = true;
            }
        }
        xSemaphoreGive(g_mutex);
    }
    if (need_sub) mqtt_manager.subscribe(topic);
    return need_sub;
}

void mqtt_sub_store_ensure_binding_subscribed(const char* templ) {
    if (!templ || !templ[0] || !g_entries || !g_mutex) return;
    if (!strstr(templ, "[")) return;  // no binding token -> nothing to do
    TopicCollectorCtx::Entry topics[8];
    int n = 0;
    TopicCollectorCtx ctx; ctx.list = topics; ctx.count = &n; ctx.max = 8;
    binding_template_collect_topics(templ, &ctx);
    for (int i = 0; i < n; i++) mqtt_sub_store_ensure_subscribed(topics[i].topic);
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

#endif // HAS_MQTT
