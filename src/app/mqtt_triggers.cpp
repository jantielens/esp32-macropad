#include "mqtt_triggers.h"

#if MQTT_TRIGGERS_ENABLED

#include "action_list.h"
#include "config_psram.h"
#include "log_manager.h"
#include "mqtt_manager.h"
#include "psram_json_allocator.h"
#include "storage.h"

#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>
#include <freertos/semphr.h>
#include <string.h>

#define TAG "MqttTrig"

static const char* MQTT_TRIGGERS_PATH = "/config/mqtt_triggers.json";

// The pending bitmask uses one bit per trigger index, so the trigger count is
// capped at 32 by the bitmask width.
static_assert(MAX_MQTT_TRIGGERS <= 32, "MAX_MQTT_TRIGGERS must be <= 32 (pending bitmask width)");

// RAM cache (PSRAM preferred, SRAM fallback). nullptr if allocation failed
// (feature then degrades to a no-op).
static MqttTriggerConfig* g_triggers = nullptr;

// Mutex guarding the g_triggers array. The save path (load_from_flash) runs on
// the async web-server task and mutates the cache, while the MQTT message
// callback and the loop drain read it on the main task. A FreeRTOS mutex (not a
// spinlock) is used so it can be held across the brief cache repopulation; slow
// work (file I/O on save, action dispatch on drain) stays outside the lock.
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

// ---------------------------------------------------------------------------
// Config load / save
//
// NOTE (subscription limit): mqtt_sub_store tracks up to 64 topics. A fully
// configured device (mqtt_audio ~7 + screen 1 + notify 1 + up to 64 pad-binding
// topics + up to MAX_MQTT_TRIGGERS triggers) can approach the broker /
// PubSubClient subscription limit. This is a documented edge case, not enforced
// at runtime — excess subscriptions simply fail and are logged by MqttManager.
// ---------------------------------------------------------------------------
static void apply_defaults() {
    if (!g_triggers) return;
    memset(g_triggers, 0, (size_t)MAX_MQTT_TRIGGERS * sizeof(MqttTriggerConfig));
}

static bool load_from_flash() {
    if (!g_triggers) return false;

    // Read and parse the file OUTSIDE the cache lock (file I/O + JSON parse are
    // the slow part). The parsed document is then applied to the shared cache
    // under the lock, keeping the locked region down to a few memcpy-speed ops.
    bool have_file = false;
    BasicJsonDocument<PsramJsonAllocator> doc(MQTT_TRIGGERS_JSON_CAP);

    if (!Storage.exists(MQTT_TRIGGERS_PATH)) {
        LOGD(TAG, "No triggers file, using defaults");
    } else {
        File f = Storage.open(MQTT_TRIGGERS_PATH, "r");
        if (!f) {
            LOGW(TAG, "Failed to open triggers config");
        } else {
            size_t file_size = f.size();
            if (file_size == 0 || file_size > MQTT_TRIGGERS_JSON_CAP) {
                LOGW(TAG, "Invalid triggers size: %u", (unsigned)file_size);
            } else {
                DeserializationError err = deserializeJson(doc, f);
                if (err) {
                    LOGE(TAG, "JSON parse error: %s", err.c_str());
                } else {
                    have_file = true;
                }
            }
            f.close();
        }
    }

    JsonArray triggers = have_file ? doc["triggers"].as<JsonArray>() : JsonArray();

    // Apply to the shared cache under the lock (fast, no I/O).
    triggers_lock();
    apply_defaults();
    uint8_t n = 0;
    if (!triggers.isNull()) {
        for (size_t i = 0; i < triggers.size() && i < MAX_MQTT_TRIGGERS; i++) {
            JsonObject t = triggers[i].as<JsonObject>();
            if (t.isNull()) continue;
            const char* topic = t["topic"] | "";
            const char* value = t["value"] | "";
            strlcpy(g_triggers[i].topic, topic, sizeof(g_triggers[i].topic));
            strlcpy(g_triggers[i].value, value, sizeof(g_triggers[i].value));
            g_triggers[i].action_count =
                action_list_parse(t["actions"], g_triggers[i].actions, MAX_BUTTON_ACTIONS);
            if (g_triggers[i].topic[0]) n++;
        }
    }
    triggers_unlock();

    LOGI(TAG, "Loaded %u trigger(s)", n);
    return have_file && !triggers.isNull();
}

void mqtt_triggers_init() {
    if (!g_lock) g_lock = xSemaphoreCreateMutex();
    g_triggers = (MqttTriggerConfig*)config_psram_alloc(
        (size_t)MAX_MQTT_TRIGGERS * sizeof(MqttTriggerConfig), "mqtt_triggers");
    if (!g_triggers) {
        LOGE(TAG, "Failed to allocate trigger config — feature disabled");
        return;
    }
    storage_mount();  // idempotent — ensures FS is mounted on headless boards too
    load_from_flash();
}

const MqttTriggerConfig* mqtt_triggers_get(uint8_t index) {
    if (index >= MAX_MQTT_TRIGGERS) return nullptr;
    if (!g_triggers) return nullptr;
    return &g_triggers[index];
}

bool mqtt_triggers_save_raw(const uint8_t* json, size_t len) {
    if (!json || len == 0) return false;

    File f = Storage.open(MQTT_TRIGGERS_PATH, "w");
    if (!f) {
        LOGE(TAG, "Failed to open for write");
        return false;
    }

    size_t written = f.write(json, len);
    f.close();

    if (written != len) {
        LOGE(TAG, "Write failed (%u of %u)", (unsigned)written, (unsigned)len);
        return false;
    }

    // Reload the RAM cache and (re)subscribe so changes take effect immediately.
    load_from_flash();
    if (mqtt_manager.connected()) {
        mqtt_triggers_on_connected();
    }
    storage_publish_usage(false);

    LOGI(TAG, "Saved (%u bytes)", (unsigned)len);
    return true;
}

// ---------------------------------------------------------------------------
// MQTT lifecycle
// ---------------------------------------------------------------------------
void mqtt_triggers_on_connected() {
    if (!g_triggers) return;
    // Subscribe every configured topic on each (re)connect. mqtt_sub_store's
    // subscribe_all() only restores pad-binding topics, so triggers must
    // re-subscribe explicitly here (mirrors mqtt_audio_on_connected()).
    for (uint8_t i = 0; i < MAX_MQTT_TRIGGERS; i++) {
        if (g_triggers[i].topic[0]) {
            mqtt_manager.subscribe(g_triggers[i].topic);
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
// spinlock, then for each set bit copy the action structs from g_triggers (which
// may be PSRAM-resident) into a stack array under the cache mutex, and dispatch
// from that copy after releasing the mutex. This keeps the locked region tiny and
// avoids holding the lock during slow action I/O (HTTP, relay, MQTT publish).
//
// Coalescing: repeated fires of the SAME trigger within one drain window collapse
// to a single dispatch (one dispatch per trigger per drain cycle). Distinct
// triggers are never dropped.
void mqtt_triggers_loop() {
    uint32_t pending;
    portENTER_CRITICAL(&g_mux);
    pending = g_pending_mask;
    g_pending_mask = 0;
    portEXIT_CRITICAL(&g_mux);

    if (!pending || !g_triggers) return;

    for (uint8_t i = 0; i < MAX_MQTT_TRIGGERS; i++) {
        if (!(pending & (1u << i))) continue;

        // Copy the action structs into a stack array under the cache lock (guards
        // against a concurrent save reloading g_triggers), then dispatch from the
        // copy AFTER releasing the lock so slow action I/O never holds it.
        ButtonAction local[MAX_BUTTON_ACTIONS];
        triggers_lock();
        uint8_t count = g_triggers[i].action_count;
        if (count > MAX_BUTTON_ACTIONS) count = MAX_BUTTON_ACTIONS;
        memcpy(local, g_triggers[i].actions, (size_t)count * sizeof(ButtonAction));
        triggers_unlock();

        action_list_dispatch(local, count, "MQTT Trigger");
    }
}

#endif  // MQTT_TRIGGERS_ENABLED
