#include "hw_button_config.h"

#if HAS_BUTTON

#include "action_parse.h"
#include "action_registry.h"
#include "action_storage.h"
#include "action_validate.h"
#include "config_psram.h"
#include "log_manager.h"
#include "psram_json_allocator.h"
#include "storage.h"

#include <ArduinoJson.h>
#include <freertos/semphr.h>
#include <stdlib.h>
#include <string.h>

#define TAG "HwBtnCfg"

static const char* HW_BUTTONS_PATH = "/config/hw_buttons.json";
static HwButtonConfig* g_config = nullptr;
static ActionStorage g_storage = {};
static SemaphoreHandle_t g_lock = nullptr;

static bool parse_list(JsonVariant value, ButtonAction* actions, uint8_t& count,
                       ActionStorage& storage, bool validate_bindings) {
    if (!value.is<JsonArray>()) return false;
    JsonArray list = value.as<JsonArray>();
    if (list.size() > MAX_BUTTON_ACTIONS) return false;
    memset(actions, 0, MAX_BUTTON_ACTIONS * sizeof(ButtonAction));
    count = 0;
    ActionStorageScope scope(&storage);
    for (JsonVariant item : list) {
        if (!item.is<JsonObject>()) return false;
        JsonObject action = item.as<JsonObject>();
        const char* type = action["type"] | "";
        if (action.containsKey("type") &&
            (action.containsKey("args") ||
             (action.containsKey("op") && strcmp(type, ACTION_TYPE_VISUAL_ALERT)))) {
            LOGW(TAG, "Invalid button action: mixed action envelope");
            return false;
        }
        const char* error = validate_bindings ? action_validate_json(action)
            : (!action.containsKey("type") && action.containsKey("op")
                ? action_operation_validate_json(action, false)
                : (!action["type"].is<const char*>() ? "missing action type" : nullptr));
        if (!error && type[0] && strcmp(type, "none") && !action_type_is_supported(type))
            error = "action unavailable";
        if (error) {
            LOGW(TAG, "Invalid button action: %s", error);
            return false;
        }
        if (!strcmp(type, "none")) continue;
        action_parse(action, actions[count]);
        if (storage.failed || !actions[count].type[0]) return false;
        ++count;
    }
    return true;
}

static bool parse_config(JsonDocument& doc, HwButtonConfig* config,
                         ActionStorage& storage, bool validate_bindings) {
    if (!doc.is<JsonObject>() || !doc["buttons"].is<JsonArray>()) return false;
    JsonArray buttons = doc["buttons"].as<JsonArray>();
    if (buttons.size() > NUM_HW_BUTTONS) return false;
    for (size_t i = 0; i < buttons.size(); ++i) {
        if (!buttons[i].is<JsonObject>()) return false;
        JsonObject button = buttons[i].as<JsonObject>();
        if (button.containsKey("tap_actions") &&
            !parse_list(button["tap_actions"], config[i].tap_actions, config[i].tap_count,
                        storage, validate_bindings)) return false;
        if (button.containsKey("hold_actions") &&
            !parse_list(button["hold_actions"], config[i].hold_actions, config[i].hold_count,
                        storage, validate_bindings)) return false;
    }
    return true;
}

static bool apply_defaults(HwButtonConfig* config, ActionStorage& storage) {
#ifdef HW_BUTTON_DEFAULTS_JSON
    BasicJsonDocument<PsramJsonAllocator> doc(4096);
    if (deserializeJson(doc, HW_BUTTON_DEFAULTS_JSON) || doc.overflowed() ||
        !parse_config(doc, config, storage, false)) {
        LOGE(TAG, "Invalid board hardware button defaults");
        return false;
    }
#else
    (void)config;
    (void)storage;
#endif
    return true;
}

static void replace_config(HwButtonConfig* replacement, ActionStorage& storage) {
    action_storage_clear(g_storage);
    memcpy(g_config, replacement, NUM_HW_BUTTONS * sizeof(HwButtonConfig));
    action_storage_move(g_storage, storage);
}

void hw_button_config_init() {
    if (!g_lock) g_lock = xSemaphoreCreateMutex();
    if (!g_lock) {
        LOGE(TAG, "Failed to create button config mutex");
        return;
    }
    if (!g_config) g_config = static_cast<HwButtonConfig*>(
        config_psram_alloc(NUM_HW_BUTTONS * sizeof(HwButtonConfig), "hw_buttons"));
    if (!g_config) return;
    auto* replacement = static_cast<HwButtonConfig*>(
        config_psram_alloc(NUM_HW_BUTTONS * sizeof(HwButtonConfig), "hw_buttons_load"));
    if (!replacement) return;
    ActionStorage storage = {};
    if (!apply_defaults(replacement, storage)) {
        action_storage_clear(storage);
        free(replacement);
        return;
    }
    storage_mount();
    if (Storage.exists(HW_BUTTONS_PATH)) {
        File f = Storage.open(HW_BUTTONS_PATH, "r");
        bool valid = false;
        if (f) {
            const size_t size = f.size();
            BasicJsonDocument<PsramJsonAllocator> doc(4096);
            valid = size && size <= 4096 && !deserializeJson(doc, f) && !doc.overflowed() &&
                    parse_config(doc, replacement, storage, false);
            f.close();
        }
        if (!valid) {
            LOGW(TAG, "Invalid button config; using board defaults");
            action_storage_clear(storage);
            memset(replacement, 0, NUM_HW_BUTTONS * sizeof(HwButtonConfig));
            if (!apply_defaults(replacement, storage)) {
                action_storage_clear(storage);
                free(replacement);
                return;
            }
        }
    }
    if (xSemaphoreTake(g_lock, portMAX_DELAY) == pdTRUE) {
        replace_config(replacement, storage);
        xSemaphoreGive(g_lock);
    }
    action_storage_clear(storage);
    free(replacement);
}

bool hw_button_config_snapshot(uint8_t index, bool hold, ButtonAction* actions,
                               uint8_t* count, ActionStorage& storage) {
    if (!g_config || !g_lock || index >= NUM_HW_BUTTONS || !actions || !count) return false;
    if (xSemaphoreTake(g_lock, portMAX_DELAY) != pdTRUE) return false;
    const HwButtonConfig& config = g_config[index];
    *count = hold ? config.hold_count : config.tap_count;
    const bool copied = action_storage_copy_actions(storage, actions,
        hold ? config.hold_actions : config.tap_actions, *count);
    xSemaphoreGive(g_lock);
    return copied;
}

bool hw_button_config_to_json(JsonArray buttons) {
    if (!g_config || !g_lock || buttons.isNull()) return false;
    if (xSemaphoreTake(g_lock, portMAX_DELAY) != pdTRUE) return false;
    for (uint8_t index = 0; index < NUM_HW_BUTTONS; ++index) {
        const HwButtonConfig& config = g_config[index];
        JsonObject button = buttons.createNestedObject();
        button["label"] = HW_BUTTON_DEFS[index].label;
        button["pin"] = HW_BUTTON_DEFS[index].pin;
        JsonArray tap = button.createNestedArray("tap_actions");
        JsonArray hold = button.createNestedArray("hold_actions");
        for (uint8_t i = 0; i < config.tap_count; ++i)
            action_to_json(config.tap_actions[i], tap.createNestedObject());
        for (uint8_t i = 0; i < config.hold_count; ++i)
            action_to_json(config.hold_actions[i], hold.createNestedObject());
    }
    xSemaphoreGive(g_lock);
    return true;
}

bool hw_button_config_save_raw(const uint8_t* json, size_t len) {
    if (!g_config || !g_lock || !json || !len || len > 4096) return false;
    BasicJsonDocument<PsramJsonAllocator> doc(4096);
    if (deserializeJson(doc, json, len) || doc.overflowed()) return false;
    auto* replacement = static_cast<HwButtonConfig*>(
        config_psram_alloc(NUM_HW_BUTTONS * sizeof(HwButtonConfig), "hw_buttons_save"));
    if (!replacement) return false;
    ActionStorage storage = {};
    if (!apply_defaults(replacement, storage) ||
        !parse_config(doc, replacement, storage, true)) {
        action_storage_clear(storage);
        free(replacement);
        return false;
    }
    if (xSemaphoreTake(g_lock, portMAX_DELAY) != pdTRUE) {
        action_storage_clear(storage);
        free(replacement);
        return false;
    }
    const bool saved = storage_write_atomic(HW_BUTTONS_PATH, json, len);
    if (saved) replace_config(replacement, storage);
    xSemaphoreGive(g_lock);
    action_storage_clear(storage);
    free(replacement);
    if (saved) storage_publish_usage(false);
    return saved;
}

#endif
