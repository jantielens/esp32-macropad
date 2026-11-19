#include "timer_config.h"

#if HAS_DISPLAY

#include "action_list.h"
#include "action_parse.h"
#include "action_registry.h"
#include "action_storage.h"
#include "action_validate.h"
#include "config_psram.h"
#include "fs_health.h"
#include "json_key_validation.h"
#include "log_manager.h"
#include "psram_json_allocator.h"
#include "timer_engine.h"

#include <ArduinoJson.h>
#include <freertos/semphr.h>
#include "storage.h"
#include <string.h>
#include <string>

#define TAG "TimerCfg"

static const char* TIMER_CONFIG_PATH = "/config/timers.json";

// RAM cache (defaults if file missing). Heap-allocated
// (PSRAM preferred, SRAM fallback) via config_psram_alloc().
static TimerConfig* g_config = nullptr;
static ActionStorage g_actions = {};
static bool g_loaded = false;
static SemaphoreHandle_t g_config_mutex = nullptr;
static SemaphoreHandle_t g_save_mutex = nullptr;
static bool g_config_ready = false;
static bool g_config_reset = false;

static inline void config_lock() {
    xSemaphoreTake(g_config_mutex, portMAX_DELAY);
}

static inline void config_unlock() {
    xSemaphoreGive(g_config_mutex);
}

static void apply_defaults(TimerConfig* cfg) {
    if (!cfg) return;
    memset(cfg, 0, sizeof(TimerConfig));
}

static const char* validate_action_envelope(JsonObjectConst action) {
    if (action.containsKey("type") || !action["op"].is<const char*>() ||
        !action["op"].as<const char*>()[0]) return "legacy action is not supported";
    return action_validate_json(action);
}

static bool parse_timer_document(JsonObject root, TimerConfig* candidate, ActionStorage& storage);

static bool load_from_flash(TimerConfig* cfg, ActionStorage& storage) {
    if (!cfg) return false;
    apply_defaults(cfg);

    if (!Storage.exists(TIMER_CONFIG_PATH)) {
        LOGD(TAG, "No timer config file, using defaults");
        return true;
    }

    File f = Storage.open(TIMER_CONFIG_PATH, "r");
    if (!f) {
        LOGW(TAG, "Failed to open timer config");
        return false;
    }

    size_t file_size = f.size();
    if (file_size == 0 || file_size > 4096) {
        LOGW(TAG, "Invalid timer config size: %u", (unsigned)file_size);
        f.close();
        return false;
    }

    if (json_stream_scan_keys(f) != JsonKeyScanResult::Valid) {
        LOGW(TAG, "Timer config contains an invalid JSON NUL or could not be read");
        f.close();
        return false;
    }
    BasicJsonDocument<PsramJsonAllocator> doc(4096);
    DeserializationError err = deserializeJson(doc, f);
    f.close();

    if (err || doc.overflowed() || !doc.is<JsonObject>()) {
        LOGE(TAG, "Invalid timer config JSON: %s", err.c_str());
        return false;
    }
    if (!parse_timer_document(doc.as<JsonObject>(), cfg, storage)) {
        if (storage.failed) return false;
        action_storage_clear(storage);
        apply_defaults(cfg);
        g_config_reset = true;
        LOGW(TAG, "Unsupported timer actions cleared in memory; save to replace stored config");
    }
    return true;
}

void timer_config_init() {
    g_config_ready = false;
    g_config_reset = false;
    g_loaded = false;
    if (!g_config_mutex) g_config_mutex = xSemaphoreCreateMutex();
    if (!g_config_mutex) {
        LOGE(TAG, "Failed to allocate timer config mutex");
        return;
    }
    if (!g_save_mutex) g_save_mutex = xSemaphoreCreateMutex();
    if (!g_save_mutex) {
        LOGE(TAG, "Failed to allocate timer save mutex");
        return;
    }
    action_storage_clear(g_actions);
    free(g_config);
    g_config = (TimerConfig*)config_psram_alloc(sizeof(TimerConfig), "timer_config");
    if (!g_config) {
        LOGE(TAG, "Failed to allocate timer config");
        return;  // feature disabled
    }
    if (!load_from_flash(g_config, g_actions)) {
        action_storage_clear(g_actions);
        apply_defaults(g_config);
        LOGE(TAG, "Timer configuration unavailable; saved settings are protected");
        return;
    }
    g_loaded = true;
    g_config_ready = true;
}

bool timer_config_snapshot_expiry(uint8_t id, TimerExpirySnapshot* out) {
    if (!g_config_ready || !out || id < 1 || id > TIMER_COUNT) return false;
    *out = TimerExpirySnapshot{};
    config_lock();
    bool copied = true;
    if (g_config && g_loaded) {
        const TimerSettings& settings = g_config->timers[id - 1];
        out->count = settings.expire_action_count;
        copied = action_storage_copy_actions(out->storage, out->actions,
                                             settings.expire_actions, out->count);
    }
    config_unlock();
    if (!copied) *out = TimerExpirySnapshot{};
    return copied;
}

bool timer_config_to_json(JsonObject root) {
    if (!g_config_ready) return false;
    config_lock();
    root["action_format_version"] = 1;
    root["config_reset"] = g_config_reset;
    for (uint8_t i = 0; i < TIMER_COUNT; i++) {
        char key[4];
        snprintf(key, sizeof(key), "%u", i + 1);
        JsonObject timer = root.createNestedObject(key);
        JsonArray actions = timer.createNestedArray("expire_actions");
        if (!g_config_ready || !g_config || !g_loaded) continue;
        const TimerSettings& settings = g_config->timers[i];
        for (uint8_t action_index = 0;
             action_index < settings.expire_action_count; action_index++) {
            action_to_json(settings.expire_actions[action_index],
                           actions.createNestedObject());
        }
    }
    config_unlock();
    return true;
}

bool timer_config_exists() {
    return Storage.exists(TIMER_CONFIG_PATH);
}

static bool parse_strict_write(const uint8_t* json, size_t len,
                               TimerConfig* candidate, ActionStorage& storage) {
    if (!json || !candidate || len == 0 || len > 4096) return false;
    if (!json_keys_valid(json, len)) {
        LOGW(TAG, "Timer config JSON contains an invalid NUL");
        return false;
    }
    apply_defaults(candidate);

    BasicJsonDocument<PsramJsonAllocator> doc(4096);
    if (deserializeJson(doc, json, len) || doc.overflowed() || !doc.is<JsonObject>()) {
        LOGW(TAG, "Write rejected: root must be an object");
        return false;
    }

    return parse_timer_document(doc.as<JsonObject>(), candidate, storage);
}

static bool parse_timer_document(JsonObject root, TimerConfig* candidate, ActionStorage& storage) {
    if (!root["action_format_version"].is<int>() || root["action_format_version"].as<int>() != 1) {
        LOGW(TAG, "Write rejected: incompatible timer actions document");
        return false;
    }
    ActionStorageScope scope(&storage);
    for (JsonPair slot_pair : root) {
        const JsonString json_key = slot_pair.key();
        const char* key = json_key.c_str();
        if (!key || json_key.size() != strlen(key)) {
            LOGW(TAG, "Write rejected: invalid timer key");
            return false;
        }
        if (!strcmp(key, "action_format_version")) continue;
        if (json_key.size() != 1 || key[0] < '1' || key[0] > '3') {
            LOGW(TAG, "Write rejected: invalid timer key '%s'", key);
            return false;
        }
        if (!slot_pair.value().is<JsonObject>()) {
            LOGW(TAG, "Write rejected: timer %s must be an object", key);
            return false;
        }

        JsonObject slot = slot_pair.value().as<JsonObject>();
        for (JsonPair field : slot) {
            const JsonString field_key = field.key();
            if (field_key.size() != strlen(field_key.c_str()) ||
                strcmp(field_key.c_str(), "expire_actions") != 0) {
                LOGW(TAG, "Write rejected: unknown timer %s field '%s'",
                     key, field.key().c_str());
                return false;
            }
        }

        if (!slot.containsKey("expire_actions")) continue;
        if (!slot["expire_actions"].is<JsonArray>()) {
            LOGW(TAG, "Write rejected: timer %s expire_actions must be an array", key);
            return false;
        }
        JsonArray actions = slot["expire_actions"].as<JsonArray>();
        if (actions.size() > TIMER_MAX_EXPIRE_ACTIONS) {
            LOGW(TAG, "Write rejected: timer %s has too many expire actions", key);
            return false;
        }

        TimerSettings& settings = candidate->timers[key[0] - '1'];
        for (JsonVariant action_value : actions) {
            if (!action_value.is<JsonObject>()) {
                LOGW(TAG, "Write rejected: timer %s action must be an object", key);
                return false;
            }
            JsonObject action_object = action_value.as<JsonObject>();
            const char* error = validate_action_envelope(action_object);
            if (error) {
                LOGW(TAG, "Write rejected: timer %s action invalid: %s", key, error);
                return false;
            }
            action_parse(action_object,
                         settings.expire_actions[settings.expire_action_count]);
            if (storage.failed || !settings.expire_actions[settings.expire_action_count].type[0]) {
                LOGW(TAG, "Write rejected: timer %s action fields are too long", key);
                return false;
            }
            settings.expire_action_count++;
        }
    }
    return true;
}

static void config_to_json(const TimerConfig& config, JsonObject root) {
    root["action_format_version"] = 1;
    for (uint8_t i = 0; i < TIMER_COUNT; i++) {
        char key[4];
        snprintf(key, sizeof(key), "%u", i + 1);
        JsonArray actions = root.createNestedObject(key)
            .createNestedArray("expire_actions");
        const TimerSettings& settings = config.timers[i];
        for (uint8_t action_index = 0;
             action_index < settings.expire_action_count; action_index++) {
            action_to_json(settings.expire_actions[action_index],
                           actions.createNestedObject());
        }
    }
}

bool timer_config_save_raw(const uint8_t* json, size_t len) {
    const StorageMutationGuard guard;
    if (!guard) return false;
    if (!g_config_ready) return false;
    xSemaphoreTake(g_save_mutex, portMAX_DELAY);
    TimerConfig* candidate = (TimerConfig*)config_psram_alloc(
        sizeof(TimerConfig), "timer_config_candidate");
    if (!candidate) {
        xSemaphoreGive(g_save_mutex);
        return false;
    }
    ActionStorage candidate_actions = {};
    if (!parse_strict_write(json, len, candidate, candidate_actions)) {
        action_storage_clear(candidate_actions);
        free(candidate);
        xSemaphoreGive(g_save_mutex);
        return false;
    }

    BasicJsonDocument<PsramJsonAllocator> normalized(4096);
    config_to_json(*candidate, normalized.to<JsonObject>());

    std::string payload;
    serializeJson(normalized, payload);
    if (normalized.overflowed() || payload.size() > 4096 ||
        !storage_write_atomic(TIMER_CONFIG_PATH,
            reinterpret_cast<const uint8_t*>(payload.data()), payload.size())) {
        LOGE(TAG, "Atomic timer config write failed");
        action_storage_clear(candidate_actions);
        free(candidate);
        xSemaphoreGive(g_save_mutex);
        return false;
    }

    config_lock();
    if (g_config) {
        action_storage_clear(g_actions);
        memcpy(g_config, candidate, sizeof(TimerConfig));
        action_storage_move(g_actions, candidate_actions);
        g_loaded = true;
        g_config_reset = false;
    }
    config_unlock();
    action_storage_clear(candidate_actions);
    free(candidate);
    storage_publish_usage(false);
    xSemaphoreGive(g_save_mutex);

    LOGI(TAG, "Saved (%u bytes)", (unsigned)payload.size());
    return true;
}

#endif // HAS_DISPLAY
