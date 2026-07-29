#include "timer_config.h"

#if HAS_DISPLAY

#include "action_list.h"
#include "action_parse.h"
#include "config_psram.h"
#include "fs_health.h"
#include "log_manager.h"
#include "psram_json_allocator.h"
#include "timer_engine.h"

#include <ArduinoJson.h>
#include <freertos/semphr.h>
#include "storage.h"
#include <string.h>

#define TAG "TimerCfg"

static const char* TIMER_CONFIG_PATH = "/config/timers.json";

// RAM cache (always valid — defaults if file missing). Heap-allocated
// (PSRAM preferred, SRAM fallback) via config_psram_alloc().
static TimerConfig* g_config = nullptr;
static bool g_loaded = false;
static SemaphoreHandle_t g_config_mutex = nullptr;
static SemaphoreHandle_t g_save_mutex = nullptr;
static bool g_config_ready = false;

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

static bool load_from_flash(TimerConfig* cfg) {
    if (!cfg) return false;
    apply_defaults(cfg);

    if (!Storage.exists(TIMER_CONFIG_PATH)) {
        LOGD(TAG, "No timer config file, using defaults");
        return false;
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

    BasicJsonDocument<PsramJsonAllocator> doc(3072);
    DeserializationError err = deserializeJson(doc, f);
    f.close();

    if (err) {
        LOGE(TAG, "JSON parse error: %s", err.c_str());
        return false;
    }

    // Parse per-timer settings: keys "1", "2", "3"
    for (uint8_t i = 0; i < TIMER_COUNT; i++) {
        char key[4];
        snprintf(key, sizeof(key), "%u", i + 1);
        JsonVariant tv = doc[key];
        if (!tv.is<JsonObject>()) continue;
        JsonObject tobj = tv.as<JsonObject>();

        JsonVariant actions_value = tobj["expire_actions"];
        if (!actions_value.is<JsonArray>()) continue;

        TimerSettings& ts = cfg->timers[i];
        for (JsonVariant action_value : actions_value.as<JsonArray>()) {
            if (!action_value.is<JsonObject>()) continue;
            ButtonAction action = {};
            action_parse(action_value.as<JsonObject>(), action);
            if (!action.type[0]) continue;
            if (ts.expire_action_count >= TIMER_MAX_EXPIRE_ACTIONS) {
                LOGW(TAG, "Timer %u: extra expire action ignored", i + 1);
                continue;
            }
            ts.expire_actions[ts.expire_action_count++] = action;
        }

        LOGI(TAG, "Timer %u: expire_actions=%u", i + 1,
             ts.expire_action_count);
    }

    return true;
}

void timer_config_init() {
    g_config_ready = false;
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
    g_config = (TimerConfig*)config_psram_alloc(sizeof(TimerConfig), "timer_config");
    if (!g_config) {
        LOGE(TAG, "Failed to allocate timer config");
        return;  // feature disabled
    }
    load_from_flash(g_config);
    g_loaded = true;
    g_config_ready = true;
}

bool timer_config_snapshot_expiry(uint8_t id, TimerExpirySnapshot* out) {
    if (!g_config_ready || !out || id < 1 || id > TIMER_COUNT) return false;
    memset(out, 0, sizeof(*out));
    config_lock();
    if (g_config && g_loaded) {
        const TimerSettings& settings = g_config->timers[id - 1];
        out->count = settings.expire_action_count;
        if (out->count > 0) {
            memcpy(out->actions, settings.expire_actions,
                   out->count * sizeof(ButtonAction));
        }
    }
    config_unlock();
    return true;
}

void timer_config_to_json(JsonObject root) {
    if (g_config_ready) config_lock();
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
    if (g_config_ready) config_unlock();
}

bool timer_config_exists() {
    return Storage.exists(TIMER_CONFIG_PATH);
}

static bool parse_strict_write(const uint8_t* json, size_t len,
                               TimerConfig* candidate) {
    if (!json || !candidate || len == 0 || len > 4096) return false;
    apply_defaults(candidate);

    BasicJsonDocument<PsramJsonAllocator> doc(4096);
    if (deserializeJson(doc, json, len) || !doc.is<JsonObject>()) {
        LOGW(TAG, "Write rejected: root must be an object");
        return false;
    }

    JsonObject root = doc.as<JsonObject>();
    for (JsonPair slot_pair : root) {
        const char* key = slot_pair.key().c_str();
        if (!key || key[1] != '\0' || key[0] < '1' || key[0] > '3') {
            LOGW(TAG, "Write rejected: invalid timer key '%s'", key ? key : "");
            return false;
        }
        if (!slot_pair.value().is<JsonObject>()) {
            LOGW(TAG, "Write rejected: timer %s must be an object", key);
            return false;
        }

        JsonObject slot = slot_pair.value().as<JsonObject>();
        for (JsonPair field : slot) {
            if (strcmp(field.key().c_str(), "expire_actions") != 0) {
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
            const char* type = action_object["type"] | "";
            if (!type[0]) {
                LOGW(TAG, "Write rejected: timer %s action requires type", key);
                return false;
            }
            action_parse(action_object,
                         settings.expire_actions[settings.expire_action_count]);
            if (!settings.expire_actions[settings.expire_action_count].type[0]) {
                LOGW(TAG, "Write rejected: timer %s action fields are too long", key);
                return false;
            }
            settings.expire_action_count++;
        }
    }
    return true;
}

static void config_to_json(const TimerConfig& config, JsonObject root) {
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
    if (!g_config_ready) return false;
    xSemaphoreTake(g_save_mutex, portMAX_DELAY);
    TimerConfig* candidate = (TimerConfig*)config_psram_alloc(
        sizeof(TimerConfig), "timer_config_candidate");
    if (!candidate) {
        xSemaphoreGive(g_save_mutex);
        return false;
    }
    if (!parse_strict_write(json, len, candidate)) {
        free(candidate);
        xSemaphoreGive(g_save_mutex);
        return false;
    }

    BasicJsonDocument<PsramJsonAllocator> normalized(4096);
    config_to_json(*candidate, normalized.to<JsonObject>());

    File f = Storage.open(TIMER_CONFIG_PATH, "w");
    if (!f) {
        LOGE(TAG, "Failed to open for write");
        free(candidate);
        xSemaphoreGive(g_save_mutex);
        return false;
    }

    size_t expected = measureJson(normalized);
    size_t written = serializeJson(normalized, f);
    f.close();

    if (written != expected) {
        LOGE(TAG, "Write failed (%u of %u)", (unsigned)written,
             (unsigned)expected);
        free(candidate);
        xSemaphoreGive(g_save_mutex);
        return false;
    }

    config_lock();
    if (g_config) {
        memcpy(g_config, candidate, sizeof(TimerConfig));
        g_loaded = true;
    }
    config_unlock();
    free(candidate);
    storage_publish_usage(false);
    xSemaphoreGive(g_save_mutex);

    LOGI(TAG, "Saved (%u bytes)", (unsigned)written);
    return true;
}

#endif // HAS_DISPLAY
