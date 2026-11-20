#include "swipe_config.h"

#if HAS_DISPLAY

#include "action_parse.h"
#include "action_registry.h"
#include "action_storage.h"
#include "action_validate.h"
#include "config_psram.h"
#include "fs_health.h"
#include "json_key_validation.h"
#include "log_manager.h"
#include "psram_json_allocator.h"
#include "storage.h"

#include <ArduinoJson.h>
#include <freertos/semphr.h>
#include <stdlib.h>
#include <string.h>

#define TAG "SwipeCfg"

static const char* SWIPE_CONFIG_PATH = "/config/swipe_actions.json";
static SwipeConfig* g_config = nullptr;
static ActionStorage g_storage = {};
static SemaphoreHandle_t g_lock = nullptr;
static bool g_config_valid = false;
static bool g_config_reset = false;

static ButtonAction* direction_action(SwipeConfig& cfg, SwipeDirection direction) {
    switch (direction) {
        case SWIPE_LEFT: return &cfg.swipe_left;
        case SWIPE_RIGHT: return &cfg.swipe_right;
        case SWIPE_UP: return &cfg.swipe_up;
        case SWIPE_DOWN: return &cfg.swipe_down;
        default: return nullptr;
    }
}

static bool parse_config(JsonDocument& doc, SwipeConfig& cfg, ActionStorage& storage,
                         bool validate_bindings) {
    if (!doc.is<JsonObject>() || !doc["action_format_version"].is<int>() ||
        doc["action_format_version"].as<int>() != 1) {
        LOGW(TAG, "Incompatible swipe actions document; recreate it");
        return false;
    }
    const char* names[] = {"swipe_left", "swipe_right", "swipe_up", "swipe_down"};
    ActionStorageScope scope(&storage);
    for (unsigned index = 0; index < 4; ++index) {
        JsonVariant value = doc[names[index]];
        if (value.isNull() && !doc.containsKey(names[index])) continue;
        if (!value.is<JsonObject>()) return false;
        JsonObject action = value.as<JsonObject>();
        if (action.size() == 0) continue;
        const char* error = validate_bindings ? action_validate_json(action)
                                              : action_operation_validate_json(action, false);
        const char* operation = action["op"] | "";
        if (!error && (action.containsKey("type") || !operation[0]))
            error = "legacy action is not supported";
        if (error) {
            LOGW(TAG, "Invalid %s: %s", names[index], error);
            return false;
        }
        ButtonAction* target = direction_action(cfg, static_cast<SwipeDirection>(index));
        action_parse(action, *target);
        if (storage.failed || !target->type[0]) return false;
    }
    return true;
}

static bool apply_defaults(SwipeConfig& cfg, ActionStorage& storage) {
    JsonDocument doc;
    JsonObject action = doc.to<JsonObject>();
    action["op"] = "screen.back";
    action.createNestedObject("args");
    ActionStorageScope scope(&storage);
    action_parse(action, cfg.swipe_right);
    return !storage.failed && cfg.swipe_right.type[0];
}

// Called with g_lock held after initialization.
static bool load_from_flash(SwipeConfig& cfg, ActionStorage& storage, bool& config_reset) {
    config_reset = false;
    if (!Storage.exists(SWIPE_CONFIG_PATH)) return apply_defaults(cfg, storage);
    File f = Storage.open(SWIPE_CONFIG_PATH, "r");
    if (!f) return false;
    const size_t size = f.size();
    if (!size || size > 4096) {
        f.close();
        return false;
    }
    if (json_stream_scan_keys(f) != JsonKeyScanResult::Valid) {
        LOGW(TAG, "Swipe config contains an invalid JSON NUL or could not be read");
        f.close();
        return false;
    }
    BasicJsonDocument<PsramJsonAllocator> doc(4096);
    const DeserializationError error = deserializeJson(doc, f);
    f.close();
    if (error || doc.overflowed()) return false;
    if (!parse_config(doc, cfg, storage, false)) {
        if (storage.failed) return false;
        action_storage_clear(storage);
        memset(&cfg, 0, sizeof(cfg));
        config_reset = true;
        LOGW(TAG, "Unsupported swipe actions cleared in memory; save to replace stored config");
    }
    return true;
}

void swipe_config_init() {
    if (!g_lock) g_lock = xSemaphoreCreateMutex();
    if (!g_lock) {
        LOGE(TAG, "Failed to allocate swipe config lock");
        return;
    }
    g_config = static_cast<SwipeConfig*>(config_psram_alloc(sizeof(SwipeConfig), "swipe_config"));
    if (!g_config) {
        LOGE(TAG, "Failed to allocate swipe config");
        return;
    }
    g_config_valid = load_from_flash(*g_config, g_storage, g_config_reset);
    if (!g_config_valid) {
        LOGW(TAG, "Swipe config unavailable; using navigation defaults");
        action_storage_clear(g_storage);
        memset(g_config, 0, sizeof(*g_config));
        if (!apply_defaults(*g_config, g_storage))
            LOGE(TAG, "Failed to allocate default swipe action");
    }
}

bool swipe_config_to_json(JsonObject object) {
    if (!g_config || !g_lock || object.isNull() ||
        xSemaphoreTake(g_lock, portMAX_DELAY) != pdTRUE) return false;
    if (!g_config_valid) {
        xSemaphoreGive(g_lock);
        return false;
    }
    object["action_format_version"] = 1;
    object["config_reset"] = g_config_reset;
    const char* names[] = {"swipe_left", "swipe_right", "swipe_up", "swipe_down"};
    for (unsigned index = 0; index < 4; ++index) {
        JsonObject action = object.createNestedObject(names[index]);
        action_to_json(*direction_action(*g_config, static_cast<SwipeDirection>(index)), action);
    }
    xSemaphoreGive(g_lock);
    return true;
}

bool swipe_config_copy_action(SwipeDirection direction, ButtonAction& action, ActionStorage& storage) {
    if (!g_config || !g_lock || xSemaphoreTake(g_lock, portMAX_DELAY) != pdTRUE) return false;
    const ButtonAction* source = direction_action(*g_config, direction);
    const bool copied = source && action_storage_copy_actions(storage, &action, source, 1);
    xSemaphoreGive(g_lock);
    return copied;
}

bool swipe_config_save_raw(const uint8_t* json, size_t len) {
    if (!g_config || !g_lock || !json || !len || len > 4096) {
        LOGW(TAG, "Swipe config save unavailable or invalid size");
        return false;
    }
    if (!json_keys_valid(json, len)) {
        LOGW(TAG, "Swipe config JSON contains an invalid NUL");
        return false;
    }
    BasicJsonDocument<PsramJsonAllocator> doc(4096);
    const DeserializationError error = deserializeJson(doc, json, len);
    if (error || doc.overflowed()) {
        LOGW(TAG, "Invalid swipe config JSON: %s", error ? error.c_str() : "document overflow");
        return false;
    }
    auto* replacement = static_cast<SwipeConfig*>(config_psram_alloc(sizeof(SwipeConfig), "swipe_save"));
    if (!replacement) {
        LOGE(TAG, "Failed to allocate swipe config for save");
        return false;
    }
    ActionStorage storage = {};
    bool saved = false;
    if (!parse_config(doc, *replacement, storage, true)) {
        LOGW(TAG, "Invalid swipe config or action allocation failed");
    } else if (xSemaphoreTake(g_lock, portMAX_DELAY) != pdTRUE) {
        LOGE(TAG, "Failed to lock swipe config for save");
    } else {
        saved = storage_write_atomic(SWIPE_CONFIG_PATH, json, len);
        if (saved) {
            action_storage_clear(g_storage);
            *g_config = *replacement;
            action_storage_move(g_storage, storage);
            g_config_valid = true;
            g_config_reset = false;
        } else {
            LOGE(TAG, "Atomic swipe config write failed");
        }
        xSemaphoreGive(g_lock);
    }
    action_storage_clear(storage);
    free(replacement);
    if (saved) storage_publish_usage(false);
    return saved;
}

void swipe_config_reload() {
    if (!g_config || !g_lock) return;
    auto* replacement = static_cast<SwipeConfig*>(config_psram_alloc(sizeof(SwipeConfig), "swipe_reload"));
    if (!replacement) return;
    ActionStorage storage = {};
    if (xSemaphoreTake(g_lock, portMAX_DELAY) == pdTRUE) {
        bool config_reset = false;
        if (load_from_flash(*replacement, storage, config_reset)) {
            action_storage_clear(g_storage);
            *g_config = *replacement;
            action_storage_move(g_storage, storage);
            g_config_valid = true;
            g_config_reset = config_reset;
        } else {
            LOGW(TAG, "Failed to reload swipe config");
        }
        xSemaphoreGive(g_lock);
    }
    action_storage_clear(storage);
    free(replacement);
}

#endif // HAS_DISPLAY
