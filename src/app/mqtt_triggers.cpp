#include "mqtt_triggers.h"

#if MQTT_TRIGGERS_ENABLED

#include "action_list.h"
#include "action_parse.h"
#include "action_registry.h"
#include "action_storage.h"
#include "action_validate.h"
#include "config_psram.h"
#include "json_key_validation.h"
#include "log_manager.h"
#include "mqtt_manager.h"
#include "psram_json_allocator.h"
#include "storage.h"

#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>
#include <freertos/semphr.h>
#include <stdlib.h>
#include <string.h>

#define TAG "MqttTrig"

static const char* MQTT_TRIGGERS_PATH = "/config/mqtt_triggers.json";

// The pending bitmask uses one bit per trigger index, so the trigger count is
// capped at 32 by the bitmask width.
static_assert(MAX_MQTT_TRIGGERS <= 32, "MAX_MQTT_TRIGGERS must be <= 32 (pending bitmask width)");

// RAM cache (PSRAM preferred, SRAM fallback). nullptr if allocation failed
// (feature then degrades to a no-op).
static MqttTriggerConfig* g_triggers = nullptr;
static ActionStorage g_action_storage = {};
static bool g_config_valid = false;
struct MqttTriggerStorage {
    MqttTriggerConfig triggers[MAX_MQTT_TRIGGERS];
    char registered_topics[MAX_MQTT_TRIGGERS][CONFIG_MQTT_TOPIC_MAX_LEN];
};
static char (*g_registered_topics)[CONFIG_MQTT_TOPIC_MAX_LEN] = nullptr;

// Mutex guarding the g_triggers array. The save path (load_from_flash) runs on
// the async web-server task and mutates the cache, while the MQTT message
// callback and the loop drain read it on the main task. A FreeRTOS mutex (not a
// spinlock) protects cache replacement and serialization. Save holds it
// through the atomic write so concurrent saves cannot reorder file and cache.
// Action dispatch stays outside the lock.
static SemaphoreHandle_t g_lock = nullptr;

static inline void triggers_lock() { if (g_lock) xSemaphoreTake(g_lock, portMAX_DELAY); }
static inline void triggers_unlock() { if (g_lock) xSemaphoreGive(g_lock); }

// ---------------------------------------------------------------------------
// Cross-task pending state (MQTT callback / network task → main loop).
//
// g_pending_mask and g_mux deliberately stay in internal DRAM (static globals,
// NOT heap/PSRAM): the spinlock critical section must be fast and the bitmask
// must be reachable without a PSRAM cache fill. Bit position = trigger index.
// The callback ORs a bit under the spinlock; the loop snapshots+clears under
// the spinlock and dispatches from a stack copy (see mqtt_triggers_loop).
// ---------------------------------------------------------------------------
static portMUX_TYPE g_mux = portMUX_INITIALIZER_UNLOCKED;
static volatile uint32_t g_pending_mask = 0;
static uint32_t g_generation = 0;

// ---------------------------------------------------------------------------
// Config load / save
//
// NOTE (subscription limit): mqtt_sub_store tracks up to 64 topics. A fully
// configured device (mqtt_audio ~7 + screen 1 + notify 1 + up to 64 pad-binding
// topics + up to MAX_MQTT_TRIGGERS triggers) can approach the broker /
// PubSubClient subscription limit. This is a documented edge case, not enforced
// at runtime — excess subscriptions simply fail and are logged by MqttManager.
// ---------------------------------------------------------------------------
static bool parse_config(JsonDocument& doc, MqttTriggerConfig* configs,
                         ActionStorage& storage, bool validate_bindings) {
    if (!doc.is<JsonObject>() || !doc["action_format_version"].is<int>() ||
        doc["action_format_version"].as<int>() != 1 || !doc["triggers"].is<JsonArray>()) {
        LOGW(TAG, "Incompatible MQTT triggers document; recreate it");
        return false;
    }
    JsonArray triggers = doc["triggers"].as<JsonArray>();
    if (triggers.size() > MAX_MQTT_TRIGGERS) return false;
    ActionStorageScope scope(&storage);
    for (size_t i = 0; i < triggers.size(); ++i) {
        if (!triggers[i].is<JsonObject>()) return false;
        JsonObject t = triggers[i].as<JsonObject>();
        if ((t.containsKey("topic") && !t["topic"].is<const char*>()) ||
            (t.containsKey("value") && !t["value"].is<const char*>())) return false;
        const char* topic = t["topic"] | "";
        const char* value = t["value"] | "";
        if (strlen(topic) >= sizeof(configs[i].topic) ||
            strlen(value) >= sizeof(configs[i].value) ||
            strchr(topic, '#') || strchr(topic, '+')) return false;
        strlcpy(configs[i].topic, topic, sizeof(configs[i].topic));
        strlcpy(configs[i].value, value, sizeof(configs[i].value));
        if (!t.containsKey("actions")) continue;
        if (!t["actions"].is<JsonArray>()) return false;
        JsonArray actions = t["actions"].as<JsonArray>();
        if (actions.size() > MAX_BUTTON_ACTIONS) return false;
        for (JsonVariant item : actions) {
            if (!item.is<JsonObject>()) return false;
            JsonObject action = item.as<JsonObject>();
            const char* error = validate_bindings ? action_validate_json(action)
                                                  : action_operation_validate_json(action, false);
            const char* operation = action["op"] | "";
            if (!error && (action.containsKey("type") || !operation[0]))
                error = "legacy action is not supported";
            if (error) {
                LOGW(TAG, "Invalid trigger action: %s", error);
                return false;
            }
            ButtonAction& parsed = configs[i].actions[configs[i].action_count];
            action_parse(action, parsed);
            if (storage.failed || !parsed.type[0]) return false;
            ++configs[i].action_count;
        }
    }
    return true;
}

static bool load_from_flash() {
    if (!g_triggers || !Storage.exists(MQTT_TRIGGERS_PATH)) return false;
    File f = Storage.open(MQTT_TRIGGERS_PATH, "r");
    if (!f) return false;
    const size_t size = f.size();
    BasicJsonDocument<PsramJsonAllocator> doc(MQTT_TRIGGERS_JSON_CAP);
    const bool valid = size && size <= MQTT_TRIGGERS_JSON_CAP &&
                       json_stream_scan_keys(f) == JsonKeyScanResult::Valid &&
                       !deserializeJson(doc, f) && !doc.overflowed();
    f.close();
    if (!valid) return false;
    auto* replacement = static_cast<MqttTriggerConfig*>(
        config_psram_alloc(MAX_MQTT_TRIGGERS * sizeof(MqttTriggerConfig), "mqtt_trigger_load"));
    if (!replacement) return false;
    ActionStorage storage = {};
    const bool parsed = parse_config(doc, replacement, storage, false);
    if (parsed) {
        triggers_lock();
        action_storage_clear(g_action_storage);
        memcpy(g_triggers, replacement, MAX_MQTT_TRIGGERS * sizeof(MqttTriggerConfig));
        action_storage_move(g_action_storage, storage);
        g_config_valid = true;
        portENTER_CRITICAL(&g_mux);
        g_pending_mask = 0;
        ++g_generation;
        portEXIT_CRITICAL(&g_mux);
        triggers_unlock();
    }
    action_storage_clear(storage);
    free(replacement);
    return parsed;
}

void mqtt_triggers_init() {
    if (!g_lock) g_lock = xSemaphoreCreateMutex();
    if (!g_lock) {
        LOGE(TAG, "Failed to create trigger mutex — feature disabled");
        return;
    }
    if (!g_triggers) {
        auto* storage = static_cast<MqttTriggerStorage*>(
            config_psram_alloc(sizeof(MqttTriggerStorage), "mqtt_triggers"));
        if (!storage) {
            LOGE(TAG, "Failed to allocate trigger config — feature disabled");
            return;
        }
        g_registered_topics = storage->registered_topics;
        g_triggers = storage->triggers;
    }
    storage_mount();  // idempotent — ensures FS is mounted on headless boards too
    if (!Storage.exists(MQTT_TRIGGERS_PATH)) {
        triggers_lock();
        g_config_valid = true;
        triggers_unlock();
    } else if (!load_from_flash()) {
        LOGW(TAG, "MQTT triggers unavailable or incompatible");
    }
}

bool mqtt_triggers_to_json(JsonArray triggers) {
    if (!g_triggers || !g_lock || triggers.isNull()) return false;
    triggers_lock();
    if (!g_config_valid) {
        triggers_unlock();
        return false;
    }
    for (uint8_t i = 0; i < MAX_MQTT_TRIGGERS; ++i) {
        const MqttTriggerConfig& cfg = g_triggers[i];
        if (!cfg.topic[0]) continue;
        JsonObject entry = triggers.createNestedObject();
        entry["topic"] = JsonString(cfg.topic, strlen(cfg.topic), JsonString::Copied);
        entry["value"] = JsonString(cfg.value, strlen(cfg.value), JsonString::Copied);
        JsonArray actions = entry.createNestedArray("actions");
        for (uint8_t a = 0; a < cfg.action_count; ++a)
            action_to_json(cfg.actions[a], actions.createNestedObject());
    }
    triggers_unlock();
    return true;
}

bool mqtt_triggers_save_raw(const uint8_t* json, size_t len) {
    if (!g_triggers || !g_lock || !json || !len || len > MQTT_TRIGGERS_JSON_CAP) return false;
    if (!json_keys_valid(json, len)) {
        LOGW(TAG, "MQTT triggers JSON contains an invalid NUL");
        return false;
    }
    BasicJsonDocument<PsramJsonAllocator> doc(MQTT_TRIGGERS_JSON_CAP);
    if (deserializeJson(doc, json, len) || doc.overflowed()) return false;
    auto* replacement = static_cast<MqttTriggerConfig*>(
        config_psram_alloc(MAX_MQTT_TRIGGERS * sizeof(MqttTriggerConfig), "mqtt_trigger_save"));
    if (!replacement) return false;
    ActionStorage storage = {};
    if (!parse_config(doc, replacement, storage, true)) {
        action_storage_clear(storage);
        free(replacement);
        return false;
    }
    triggers_lock();
    const bool saved = storage_write_atomic(MQTT_TRIGGERS_PATH, json, len);
    if (saved) {
        action_storage_clear(g_action_storage);
        memcpy(g_triggers, replacement, MAX_MQTT_TRIGGERS * sizeof(MqttTriggerConfig));
        action_storage_move(g_action_storage, storage);
        g_config_valid = true;
        portENTER_CRITICAL(&g_mux);
        g_pending_mask = 0;
        ++g_generation;
        portEXIT_CRITICAL(&g_mux);
    }
    triggers_unlock();
    action_storage_clear(storage);
    free(replacement);
    if (!saved) return false;
    mqtt_triggers_on_connected();
    storage_publish_usage(false);

    LOGI(TAG, "Saved (%u bytes)", (unsigned)len);
    return true;
}

// ---------------------------------------------------------------------------
// MQTT lifecycle
// ---------------------------------------------------------------------------
void mqtt_triggers_on_connected() {
    if (!g_triggers) return;
    char current[MAX_MQTT_TRIGGERS][CONFIG_MQTT_TOPIC_MAX_LEN] = {};
    char previous[MAX_MQTT_TRIGGERS][CONFIG_MQTT_TOPIC_MAX_LEN] = {};
    triggers_lock();
    for (uint8_t i = 0; i < MAX_MQTT_TRIGGERS; i++) {
        strlcpy(current[i], g_triggers[i].topic, sizeof(current[i]));
        strlcpy(previous[i], g_registered_topics[i], sizeof(previous[i]));
        strlcpy(g_registered_topics[i], current[i], sizeof(g_registered_topics[i]));
    }
    triggers_unlock();

    for (uint8_t i = 0; i < MAX_MQTT_TRIGGERS; i++) {
        if (previous[i][0]) {
            bool retained = false;
            for (uint8_t j = 0; j < MAX_MQTT_TRIGGERS; j++) {
                if (current[j][0] && strcmp(previous[i], current[j]) == 0) {
                    retained = true;
                    break;
                }
            }
            if (!retained) {
                mqtt_manager.setSubscriptionOwner(previous[i], MqttSubscriptionOwner::Triggers, false);
            }
        }
        if (current[i][0]) {
            mqtt_manager.setSubscriptionOwner(current[i], MqttSubscriptionOwner::Triggers, true);
        }
    }
}

// MQTT message callback — runs on the network task. Keep fast: no dispatch, no
// blocking. The g_triggers reads (topic/value compares) touch PSRAM cross-task,
// which is safe (read-only, bounded by MAX_MQTT_TRIGGERS). The spinlock guards
// ONLY the pending bitmask write.
void mqtt_triggers_on_message(const char* topic, const uint8_t* payload, unsigned int length) {
    if (!topic || !g_triggers) return;

    // Null-terminated copy of the raw payload for exact value matching. Payloads
    // larger than the buffer are truncated (PubSubClient caps at
    // MQTT_MAX_PACKET_SIZE; silent truncation is acceptable).
    char buf[256];
    size_t n = length < sizeof(buf) - 1 ? length : sizeof(buf) - 1;
    if (payload && n) memcpy(buf, payload, n);
    buf[n] = '\0';

    // Fan-out: every trigger whose topic (and optional value filter) matches is
    // flagged. Multiple matches on the same topic all dispatch (in index order).
    // The cache lock guards against a concurrent save (load_from_flash) on the
    // web-server task repopulating g_triggers mid-scan.
    triggers_lock();
    for (uint8_t i = 0; i < MAX_MQTT_TRIGGERS; i++) {
        if (!g_triggers[i].topic[0]) continue;
        if (strcmp(topic, g_triggers[i].topic) != 0) continue;
        // Empty value = match any payload; otherwise exact raw-payload match.
        if (g_triggers[i].value[0] && strcmp(buf, g_triggers[i].value) != 0) continue;

        portENTER_CRITICAL(&g_mux);
        g_pending_mask |= (1u << i);
        portEXIT_CRITICAL(&g_mux);
    }
    triggers_unlock();
}

// Main-loop drain (main task). Snapshot+clear the pending mask under the
// spinlock, then for each set bit copy the actions and their owned arguments
// under the cache mutex, and dispatch after releasing it. A saved replacement
// cancels matches not yet copied from the old configuration.
//
// Coalescing: repeated fires of the SAME trigger within one drain window collapse
// to a single dispatch (one dispatch per trigger per drain cycle). Distinct
// triggers are never dropped.
void mqtt_triggers_loop() {
    uint32_t pending;
    uint32_t generation;
    portENTER_CRITICAL(&g_mux);
    pending = g_pending_mask;
    g_pending_mask = 0;
    generation = g_generation;
    portEXIT_CRITICAL(&g_mux);

    if (!pending || !g_triggers) return;

    for (uint8_t i = 0; i < MAX_MQTT_TRIGGERS; i++) {
        if (!(pending & (1u << i))) continue;

        // Snapshot owned arguments as well as action structs before dispatch.
        auto* local = static_cast<ButtonAction*>(
            action_storage_allocate(MAX_BUTTON_ACTIONS * sizeof(ButtonAction)));
        if (!local) continue;
        ActionStorage storage = {};
        triggers_lock();
        if (generation != g_generation) {
            triggers_unlock();
            action_storage_free(local);
            break;
        }
        uint8_t count = g_triggers[i].action_count;
        if (count > MAX_BUTTON_ACTIONS) count = MAX_BUTTON_ACTIONS;
        const bool copied = action_storage_copy_actions(storage, local, g_triggers[i].actions, count);
        triggers_unlock();

        if (copied) action_list_dispatch(local, count, "MQTT Trigger");
        else LOGE(TAG, "Cannot copy MQTT trigger actions");
        action_storage_clear(storage);
        action_storage_free(local);
    }
}

#endif  // MQTT_TRIGGERS_ENABLED
