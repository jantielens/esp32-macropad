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
    size_t value_len;
    uint8_t inspection_refs;
    bool dirty;      // set by store_set, cleared by store_get
    bool in_use;
    bool truncated;  // payload exceeded MAX_VALUE_LEN on ingestion
    bool received;
    bool saved;
    bool preview;
};

static SubStoreEntry* g_entries = nullptr;
static SemaphoreHandle_t g_mutex = nullptr;
static SemaphoreHandle_t g_interest_mutex = nullptr;

static bool lock_interests() {
    return g_interest_mutex &&
           xSemaphoreTake(g_interest_mutex, pdMS_TO_TICKS(100)) == pdTRUE;
}

static void unlock_interests() {
    if (g_interest_mutex) xSemaphoreGive(g_interest_mutex);
}

static bool entry_has_interest(const SubStoreEntry& entry) {
    return entry.saved || entry.preview || entry.inspection_refs != 0;
}

static void reset_entry(SubStoreEntry& entry, const char* topic) {
    memset(&entry, 0, sizeof(entry));
    strlcpy(entry.topic, topic, sizeof(entry.topic));
    entry.in_use = true;
}

static void clear_entry(SubStoreEntry& entry) {
    memset(&entry, 0, sizeof(entry));
}

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

bool mqtt_sub_store_active() {
    return g_entries != nullptr && g_mutex != nullptr && g_interest_mutex != nullptr;
}

// ============================================================================
// Init
// ============================================================================

void mqtt_sub_store_init() {
    if (!g_entries) {
        g_entries = (SubStoreEntry*)heap_caps_calloc(
            MQTT_SUB_STORE_MAX_ENTRIES, sizeof(SubStoreEntry),
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!g_entries) {
            g_entries = (SubStoreEntry*)calloc(MQTT_SUB_STORE_MAX_ENTRIES, sizeof(SubStoreEntry));
        }
    }
    if (!g_entries) {
        LOGE(TAG, "OOM: failed to allocate store");
        return;
    }

    if (!g_mutex) g_mutex = xSemaphoreCreateMutex();
    if (!g_interest_mutex) g_interest_mutex = xSemaphoreCreateMutex();
    if (!g_mutex || !g_interest_mutex) {
        LOGE(TAG, "Failed to create store mutexes");
        return;
    }
    LOGI(TAG, "Initialized (%d entries, %u bytes)", MQTT_SUB_STORE_MAX_ENTRIES,
         (unsigned)(MQTT_SUB_STORE_MAX_ENTRIES * sizeof(SubStoreEntry)));
}

// ============================================================================
// Subscribe all — scan pad configs for unique topics
// ============================================================================

void mqtt_sub_store_subscribe_all() {
#if HAS_DISPLAY
    if (!g_entries || !g_mutex || !lock_interests()) return;

    // Collect unique topics from all pad page label bindings and state/widget bindings.
    static const int MAX_UNIQUE = MQTT_SUB_STORE_MAX_ENTRIES;
    int unique_count = 0;

    // Keep all full-store snapshots off the 8 KiB Arduino loop stack.
    typedef TopicCollectorCtx::Entry TopicEntry;
    const size_t workspace_bytes = 3 * MAX_UNIQUE * sizeof(TopicEntry);
    const uint32_t workspace_caps = MALLOC_CAP_8BIT |
        (HAS_PSRAM ? MALLOC_CAP_SPIRAM : MALLOC_CAP_INTERNAL);
    TopicEntry* unique = (TopicEntry*)heap_caps_malloc(
        workspace_bytes, workspace_caps);
    if (!unique) {
        LOGE(TAG, "OOM for topic dedup list");
        unlock_interests();
        return;
    }

    TopicCollectorCtx ctx{unique, &unique_count, MAX_UNIQUE};
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
    TopicEntry* old_saved = unique + MAX_UNIQUE;
    TopicEntry* old_preview = old_saved + MAX_UNIQUE;
    int old_saved_count = 0;
    int old_preview_count = 0;

    // Reconcile saved and lazy-preview interests without disturbing active
    // inspection handles or holding the store lock across manager calls.
    if (xSemaphoreTake(g_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        for (int i = 0; i < MQTT_SUB_STORE_MAX_ENTRIES; i++) {
            SubStoreEntry& entry = g_entries[i];
            if (!entry.in_use) continue;
            if (entry.saved) strlcpy(old_saved[old_saved_count++].topic, entry.topic, CONFIG_MQTT_TOPIC_MAX_LEN);
            if (entry.preview) strlcpy(old_preview[old_preview_count++].topic, entry.topic, CONFIG_MQTT_TOPIC_MAX_LEN);
            entry.saved = false;
            entry.preview = false;
            entry.in_use = entry_has_interest(entry);
        }

        for (int t = 0; t < unique_count; t++) {
            int slot = -1;
            for (int i = 0; i < MQTT_SUB_STORE_MAX_ENTRIES; i++) {
                if (g_entries[i].topic[0] && strcmp(g_entries[i].topic, unique[t].topic) == 0) {
                    slot = i;
                    break;
                }
            }
            if (slot < 0) {
                for (int i = 0; i < MQTT_SUB_STORE_MAX_ENTRIES; i++) {
                    if (!g_entries[i].in_use) {
                        slot = i;
                        reset_entry(g_entries[i], unique[t].topic);
                        break;
                    }
                }
            }
            if (slot < 0) {
                LOGW(TAG, "Store full, skipping topic: %s", unique[t].topic);
                continue;
            }
            g_entries[slot].saved = true;
            g_entries[slot].in_use = true;
        }
        for (int i = 0; i < MQTT_SUB_STORE_MAX_ENTRIES; i++) {
            if (!entry_has_interest(g_entries[i])) clear_entry(g_entries[i]);
        }
        xSemaphoreGive(g_mutex);
    } else {
        LOGW(TAG, "Could not lock store while reconciling saved topics");
        free(unique);
        unlock_interests();
        return;
    }

    for (int t = 0; t < old_saved_count; t++) {
        mqtt_manager.setSubscriptionOwner(old_saved[t].topic, MqttSubscriptionOwner::SavedBindings, false);
    }
    for (int t = 0; t < old_preview_count; t++) {
        mqtt_manager.setSubscriptionOwner(old_preview[t].topic, MqttSubscriptionOwner::Preview, false);
    }
    for (int t = 0; t < unique_count; t++) {
        mqtt_manager.setSubscriptionOwner(unique[t].topic, MqttSubscriptionOwner::SavedBindings, true);
    }

    LOGI(TAG, "Reconciled %d unique topics", unique_count);
    free(unique);
    unlock_interests();
#endif // HAS_DISPLAY
}

// ============================================================================
// Resubscribe — called on MQTT reconnect
// ============================================================================

void mqtt_sub_store_resubscribe() {
    if (!g_entries || !g_mutex || !lock_interests()) return;

    int count = 0;
    for (int i = 0; i < MQTT_SUB_STORE_MAX_ENTRIES; i++) {
        char topic[CONFIG_MQTT_TOPIC_MAX_LEN] = {};
        bool saved = false;
        bool preview = false;
        bool inspection = false;
        if (xSemaphoreTake(g_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
            LOGW(TAG, "Could not lock store while restoring subscriptions");
            unlock_interests();
            return;
        }
        const SubStoreEntry& entry = g_entries[i];
        if (entry.in_use && entry.topic[0]) {
            strlcpy(topic, entry.topic, sizeof(topic));
            saved = entry.saved;
            preview = entry.preview;
            inspection = entry.inspection_refs != 0;
        }
        xSemaphoreGive(g_mutex);

        if (!topic[0]) continue;
        // The interest mutex keeps ownership stable while the payload lock is released.
        if (saved) mqtt_manager.setSubscriptionOwner(topic, MqttSubscriptionOwner::SavedBindings, true);
        if (preview) mqtt_manager.setSubscriptionOwner(topic, MqttSubscriptionOwner::Preview, true);
        if (inspection) mqtt_manager.setSubscriptionOwner(topic, MqttSubscriptionOwner::Inspection, true);
        count++;
    }

    if (count > 0) {
        LOGI(TAG, "Re-subscribed to %d topics", count);
    }
    unlock_interests();
}

// ============================================================================
// Lazy single-topic subscribe (live preview of not-yet-saved bindings)
// ============================================================================

bool mqtt_sub_store_ensure_subscribed(const char* topic) {
    if (!g_entries || !g_mutex || !topic || !topic[0] || !lock_interests()) return false;
    bool add_preview = false;
    bool new_entry = false;
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
                reset_entry(g_entries[slot], topic);
                new_entry = true;
            }
        }
        if (slot >= 0 && !g_entries[slot].preview) {
            g_entries[slot].preview = true;
            add_preview = true;
        }
        xSemaphoreGive(g_mutex);
    } else {
        unlock_interests();
        return false;
    }
    if (add_preview) mqtt_manager.setSubscriptionOwner(topic, MqttSubscriptionOwner::Preview, true);
    unlock_interests();
    return new_entry;
}

bool mqtt_sub_store_inspection_acquire(const char* topic, bool* cached_sample) {
    if (!g_entries || !g_mutex || !topic || !topic[0] || !lock_interests()) return false;
    bool first_interest = false;
    if (cached_sample) *cached_sample = false;
    if (xSemaphoreTake(g_mutex, pdMS_TO_TICKS(50)) != pdTRUE) {
        unlock_interests();
        return false;
    }

    int slot = -1;
    for (int i = 0; i < MQTT_SUB_STORE_MAX_ENTRIES; i++) {
        if (g_entries[i].in_use && strcmp(g_entries[i].topic, topic) == 0) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        for (int i = 0; i < MQTT_SUB_STORE_MAX_ENTRIES; i++) {
            if (!g_entries[i].in_use) {
                slot = i;
                reset_entry(g_entries[i], topic);
                break;
            }
        }
    }
    if (slot < 0 || g_entries[slot].inspection_refs == UINT8_MAX) {
        xSemaphoreGive(g_mutex);
        unlock_interests();
        return false;
    }
    if (cached_sample) *cached_sample = g_entries[slot].received;
    first_interest = g_entries[slot].inspection_refs++ == 0;
    xSemaphoreGive(g_mutex);

    if (first_interest &&
        !mqtt_manager.setSubscriptionOwner(topic, MqttSubscriptionOwner::Inspection, true)) {
        if (xSemaphoreTake(g_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            for (int i = 0; i < MQTT_SUB_STORE_MAX_ENTRIES; i++) {
                SubStoreEntry& entry = g_entries[i];
                if (!entry.in_use || strcmp(entry.topic, topic) != 0) continue;
                if (entry.inspection_refs > 0) --entry.inspection_refs;
                if (!entry_has_interest(entry)) clear_entry(entry);
                break;
            }
            xSemaphoreGive(g_mutex);
        }
        unlock_interests();
        return false;
    }
    unlock_interests();
    return true;
}

bool mqtt_sub_store_inspection_release(const char* topic) {
    if (!g_entries || !g_mutex || !topic || !topic[0] || !lock_interests()) return false;
    if (xSemaphoreTake(g_mutex, pdMS_TO_TICKS(50)) != pdTRUE) {
        unlock_interests();
        return false;
    }
    bool found = false;

    for (int i = 0; i < MQTT_SUB_STORE_MAX_ENTRIES; i++) {
        SubStoreEntry& entry = g_entries[i];
        if (!entry.in_use || strcmp(entry.topic, topic) != 0) continue;
        found = true;
        if (entry.inspection_refs > 1) {
            --entry.inspection_refs;
            xSemaphoreGive(g_mutex);
            unlock_interests();
            return true;
        }
        found = entry.inspection_refs == 1;
        break;
    }
    xSemaphoreGive(g_mutex);

    if (!found) {
        unlock_interests();
        return true;
    }

    // Keep the final local reference until the manager has accepted removal.
    // This makes a retry after registry-lock contention idempotent.
    if (!mqtt_manager.setSubscriptionOwner(topic, MqttSubscriptionOwner::Inspection, false)) {
        unlock_interests();
        return false;
    }

    if (xSemaphoreTake(g_mutex, pdMS_TO_TICKS(50)) != pdTRUE) {
        unlock_interests();
        return false;
    }
    for (int i = 0; i < MQTT_SUB_STORE_MAX_ENTRIES; i++) {
        SubStoreEntry& entry = g_entries[i];
        if (!entry.in_use || strcmp(entry.topic, topic) != 0) continue;
        if (entry.inspection_refs == 1) --entry.inspection_refs;
        if (!entry_has_interest(entry)) clear_entry(entry);
        break;
    }
    xSemaphoreGive(g_mutex);
    unlock_interests();
    return true;
}

bool mqtt_sub_store_inspection_snapshot(const char* topic, uint8_t* payload,
                                        size_t payload_capacity, size_t* payload_len,
                                        bool* received, bool* truncated) {
    if (payload_len) *payload_len = 0;
    if (received) *received = false;
    if (truncated) *truncated = false;
    if (!g_entries || !g_mutex || !topic || !payload || payload_capacity == 0) return false;

    bool found = false;
    if (xSemaphoreTake(g_mutex, pdMS_TO_TICKS(50)) != pdTRUE) return false;
    for (int i = 0; i < MQTT_SUB_STORE_MAX_ENTRIES; i++) {
        const SubStoreEntry& entry = g_entries[i];
        if (!entry.in_use || strcmp(entry.topic, topic) != 0) continue;
        if (entry.value_len >= payload_capacity) break;
        if (entry.value_len) memcpy(payload, entry.value, entry.value_len);
        payload[entry.value_len] = '\0';
        if (payload_len) *payload_len = entry.value_len;
        if (received) *received = entry.received;
        if (truncated) *truncated = entry.truncated;
        found = true;
        break;
    }
    xSemaphoreGive(g_mutex);
    return found;
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
                if (copy_len) memcpy(g_entries[i].value, payload, copy_len);
                g_entries[i].value[copy_len] = '\0';
                g_entries[i].value_len = copy_len;
                g_entries[i].dirty = true;
                g_entries[i].truncated = was_truncated;
                g_entries[i].received = true;
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
