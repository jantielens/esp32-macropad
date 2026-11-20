#include "shutter_session_actions.h"

#if IS_SHUTTER_TESTER

#include "action_dispatch.h"
#include "action_list.h"
#include "action_parse.h"
#include "action_storage.h"
#include "action_validate.h"
#include "json_key_validation.h"
#include "config_psram.h"
#include "display_manager.h"
#include "log_manager.h"
#include "psram_json_allocator.h"
#include "storage.h"

#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <stdlib.h>
#include <string.h>

#define TAG "ShutSessAct"

static const char* CONFIG_PATH = "/config/shutter_session_actions.json";

static ShutterSessionActionsConfig* g_config = nullptr;
static ActionStorage g_storage = {};
static SemaphoreHandle_t g_lock = nullptr;
static bool g_config_valid = false;

// Deferred-dispatch flag for save_complete. Set from background persist task,
// drained from the LVGL/main loop. Spinlock keeps the read-and-clear atomic.
static portMUX_TYPE s_complete_mux = portMUX_INITIALIZER_UNLOCKED;
static volatile bool s_save_complete_pending = false;

static bool self_trigger(const char* operation) {
    return !strcmp(operation, "shutter.session.start") ||
           !strcmp(operation, "shutter.session.stop") ||
           !strcmp(operation, "shutter.session.toggle");
}

static bool parse_list(JsonArray list, ButtonAction* actions, uint8_t& count,
                       ActionStorage& storage, bool validate_bindings) {
    if (list.isNull() || list.size() > MAX_BUTTON_ACTIONS) {
        LOGW(TAG, "Invalid shutter session action list");
        return false;
    }
    ActionStorageScope scope(&storage);
    for (JsonVariant value : list) {
        if (!value.is<JsonObject>()) {
            LOGW(TAG, "Shutter session action must be an object");
            return false;
        }
        JsonObject action = value.as<JsonObject>();
        const char* error = validate_bindings ? action_validate_json(action)
                                              : action_operation_validate_json(action, false);
        const char* operation = action["op"] | "";
        if (!error && (action.containsKey("type") || !operation[0]))
            error = "legacy action is not supported";
        if (!error && self_trigger(operation))
            error = "session action would retrigger session saving";
        if (error) {
            LOGW(TAG, "Invalid shutter session action: %s", error);
            return false;
        }
        action_parse(action, actions[count]);
        if (storage.failed || !actions[count].type[0]) {
            LOGW(TAG, "Cannot parse shutter session action");
            return false;
        }
        ++count;
    }
    return true;
}

static bool parse_config(JsonDocument& doc, ShutterSessionActionsConfig& cfg,
                         ActionStorage& storage, bool validate_bindings) {
    if (!doc.is<JsonObject>() || !doc["action_format_version"].is<int>() ||
        doc["action_format_version"].as<int>() != 1) {
        LOGW(TAG, "Incompatible shutter session actions document; recreate it");
        return false;
    }
    return parse_list(doc["save_start_actions"].as<JsonArray>(), cfg.save_start_actions,
                      cfg.save_start_count, storage, validate_bindings) &&
           parse_list(doc["save_complete_actions"].as<JsonArray>(), cfg.save_complete_actions,
                      cfg.save_complete_count, storage, validate_bindings);
}

static bool load_from_flash() {
    if (!Storage.exists(CONFIG_PATH)) {
        LOGD(TAG, "No config file, using defaults");
        return true;
    }

    File f = Storage.open(CONFIG_PATH, "r");
    if (!f) {
        LOGW(TAG, "Failed to open config");
        return false;
    }

    size_t file_size = f.size();
    if (file_size == 0 || file_size > 4096) {
        LOGW(TAG, "Invalid config size: %u", (unsigned)file_size);
        f.close();
        return false;
    }

    if (json_stream_scan_keys(f) != JsonKeyScanResult::Valid) {
        LOGW(TAG, "Shutter session actions contain an invalid JSON NUL or could not be read");
        f.close();
        return false;
    }
    BasicJsonDocument<PsramJsonAllocator> doc(4096);
    DeserializationError err = deserializeJson(doc, f);
    f.close();

    if (err || doc.overflowed()) {
        LOGE(TAG, "JSON parse error: %s", err.c_str());
        return false;
    }

    ActionStorage storage = {};
    if (!parse_config(doc, *g_config, storage, false)) {
        action_storage_clear(storage);
        memset(g_config, 0, sizeof(*g_config));
        return false;
    }
    action_storage_move(g_storage, storage);
    LOGI(TAG, "Loaded (start=%u complete=%u)",
         g_config->save_start_count, g_config->save_complete_count);
    return true;
}

void shutter_session_actions_init() {
    if (!g_lock) g_lock = xSemaphoreCreateMutex();
    if (!g_lock) {
        LOGE(TAG, "Failed to create shutter session actions lock");
        return;
    }
    g_config = static_cast<ShutterSessionActionsConfig*>(
        config_psram_alloc(sizeof(ShutterSessionActionsConfig), "shutter_session_actions"));
    if (!g_config) {
        LOGE(TAG, "Failed to allocate shutter session actions");
        return;
    }
    g_config_valid = load_from_flash();
}

bool shutter_session_actions_to_json(std::string& out) {
    if (!g_config || !g_lock || xSemaphoreTake(g_lock, portMAX_DELAY) != pdTRUE)
        return false;
    if (!g_config_valid) {
        xSemaphoreGive(g_lock);
        return false;
    }
    BasicJsonDocument<PsramJsonAllocator> doc(4096);
    doc["action_format_version"] = 1;
    JsonArray start = doc.createNestedArray("save_start_actions");
    JsonArray complete = doc.createNestedArray("save_complete_actions");
    for (uint8_t i = 0; i < g_config->save_start_count; ++i)
        action_to_json(g_config->save_start_actions[i], start.createNestedObject());
    for (uint8_t i = 0; i < g_config->save_complete_count; ++i)
        action_to_json(g_config->save_complete_actions[i], complete.createNestedObject());
    out.clear();
    const bool valid = !doc.overflowed() && serializeJson(doc, out) > 0;
    xSemaphoreGive(g_lock);
    return valid;
}

bool shutter_session_actions_save_raw(const uint8_t* json, size_t len) {
    if (!g_config || !g_lock || !json || !len || len > 4096) {
        LOGW(TAG, "Invalid shutter session actions save");
        return false;
    }
    if (!json_keys_valid(json, len)) {
        LOGW(TAG, "Shutter session actions JSON contains an invalid NUL");
        return false;
    }
    BasicJsonDocument<PsramJsonAllocator> doc(4096);
    const DeserializationError err = deserializeJson(doc, json, len);
    if (err || doc.overflowed()) {
        LOGW(TAG, "Invalid shutter session actions JSON: %s", err.c_str());
        return false;
    }
    auto* replacement = static_cast<ShutterSessionActionsConfig*>(
        config_psram_alloc(sizeof(ShutterSessionActionsConfig), "shutter_session_save"));
    if (!replacement) return false;
    ActionStorage storage = {};
    if (!parse_config(doc, *replacement, storage, true)) {
        action_storage_clear(storage);
        free(replacement);
        return false;
    }
    if (xSemaphoreTake(g_lock, portMAX_DELAY) != pdTRUE) {
        LOGE(TAG, "Failed to lock shutter session actions");
        action_storage_clear(storage);
        free(replacement);
        return false;
    }
    const bool saved = storage_write_atomic(CONFIG_PATH, json, len);
    if (saved) {
        action_storage_clear(g_storage);
        *g_config = *replacement;
        action_storage_move(g_storage, storage);
        g_config_valid = true;
    }
    xSemaphoreGive(g_lock);
    action_storage_clear(storage);
    free(replacement);
    if (!saved) {
        LOGE(TAG, "Atomic shutter session actions write failed");
        return false;
    }
    storage_publish_usage(false);

    LOGI(TAG, "Saved (%u bytes)", (unsigned)len);
    return true;
}

static void dispatch_hooks(bool complete) {
    if (!g_config || !g_lock) return;
    auto* snapshot = static_cast<ShutterSessionActionsConfig*>(
        config_psram_alloc(sizeof(ShutterSessionActionsConfig), "shutter_session_dispatch"));
    if (!snapshot) {
        LOGE(TAG, "Failed to allocate shutter session action snapshot");
        return;
    }
    ActionStorage storage = {};
    if (xSemaphoreTake(g_lock, portMAX_DELAY) != pdTRUE) {
        LOGE(TAG, "Failed to lock shutter session actions for dispatch");
        free(snapshot);
        return;
    }
    const uint8_t count = g_config_valid ? (complete ? g_config->save_complete_count : g_config->save_start_count) : 0;
    const ButtonAction* actions = complete ? g_config->save_complete_actions : g_config->save_start_actions;
    const bool copied = action_storage_copy_actions(storage, snapshot->save_start_actions, actions, count);
    xSemaphoreGive(g_lock);
    if (!copied) LOGE(TAG, "Failed to copy shutter session actions");
    else if (count) {
        const ActionContinuationOwner owner = displayManager && displayManager->isInLvglTask()
            ? ACTION_CONTINUATION_OWNER_LVGL : ACTION_CONTINUATION_OWNER_LOOP;
        for (uint8_t i = 0; i < count; ++i)
            action_list_dispatch(&snapshot->save_start_actions[i], 1, "SessSave", owner);
    }
    action_storage_clear(storage);
    free(snapshot);
}

void shutter_session_actions_dispatch_start() {
    dispatch_hooks(false);
}

void shutter_session_actions_notify_complete() {
    portENTER_CRITICAL(&s_complete_mux);
    s_save_complete_pending = true;
    portEXIT_CRITICAL(&s_complete_mux);
}

void shutter_session_actions_loop() {
    bool pending;
    portENTER_CRITICAL(&s_complete_mux);
    pending = s_save_complete_pending;
    s_save_complete_pending = false;
    portEXIT_CRITICAL(&s_complete_mux);
    if (!pending) return;

    dispatch_hooks(true);
}

#endif // IS_SHUTTER_TESTER
