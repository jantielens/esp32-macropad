#include "boot_actions.h"

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

#include <ArduinoJson.h>
#include <freertos/semphr.h>
#include "storage.h"
#include <stdlib.h>
#include <string.h>

#define TAG "BootAct"

static const char* BOOT_ACTIONS_PATH = "/config/boot_actions.json";

// RAM cache (always valid — empty if file missing). Heap-allocated
// (PSRAM preferred, SRAM fallback) via config_psram_alloc().
static BootActionsConfig* g_config = nullptr;
static ActionStorage g_storage = {};
static SemaphoreHandle_t g_lock = nullptr;
static bool g_config_valid = false;
static bool g_config_reset = false;

static bool parse_config(JsonDocument& doc, BootActionsConfig& cfg, ActionStorage& storage,
                         bool validate_bindings) {
    if (!doc.is<JsonObject>() || !doc["action_format_version"].is<int>() ||
        doc["action_format_version"].as<int>() != 1 || !doc["actions"].is<JsonArray>()) {
        LOGW(TAG, "Incompatible boot actions document; recreate it");
        return false;
    }
    JsonArray actions = doc["actions"].as<JsonArray>();
    if (actions.size() > MAX_BUTTON_ACTIONS) {
        LOGW(TAG, "Too many boot actions");
        return false;
    }
    ActionStorageScope scope(&storage);
    for (JsonVariant value : actions) {
        if (!value.is<JsonObject>()) {
            LOGW(TAG, "Boot action must be an object");
            return false;
        }
        JsonObject action = value.as<JsonObject>();
        const char* error = validate_bindings ? action_validate_json(action)
                                              : action_operation_validate_json(action, false);
        const char* operation = action["op"] | "";
        if (!error && (action.containsKey("type") || !operation[0]))
            error = "legacy action is not supported";
        if (error) {
            LOGW(TAG, "Invalid boot action: %s", error);
            return false;
        }
        action_parse(action, cfg.actions[cfg.action_count]);
        if (storage.failed || !cfg.actions[cfg.action_count].type[0]) {
            LOGW(TAG, "Cannot parse boot action");
            return false;
        }
        ++cfg.action_count;
    }
    return true;
}

static bool load_from_flash(BootActionsConfig* cfg) {
    if (!cfg) return false;
    g_config_reset = false;

    if (!Storage.exists(BOOT_ACTIONS_PATH)) {
        LOGD(TAG, "No boot actions file, using defaults");
        return true;
    }

    File f = Storage.open(BOOT_ACTIONS_PATH, "r");
    if (!f) {
        LOGW(TAG, "Failed to open boot actions");
        return false;
    }

    size_t file_size = f.size();
    if (file_size == 0 || file_size > 4096) {
        LOGW(TAG, "Invalid boot actions size: %u", (unsigned)file_size);
        f.close();
        return false;
    }

    if (json_stream_scan_keys(f) != JsonKeyScanResult::Valid) {
        LOGW(TAG, "Boot actions contain an invalid JSON NUL or could not be read");
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
    if (!parse_config(doc, *cfg, storage, false)) {
        const bool allocation_failed = storage.failed;
        action_storage_clear(storage);
        memset(cfg, 0, sizeof(*cfg));
        if (allocation_failed) return false;
        g_config_reset = true;
        LOGW(TAG, "Unsupported boot actions cleared in memory; save to replace stored config");
        return true;
    }
    action_storage_move(g_storage, storage);

    LOGI(TAG, "Loaded %u boot action(s)", cfg->action_count);
    return true;
}

void boot_actions_init() {
    if (!g_lock) g_lock = xSemaphoreCreateMutex();
    if (!g_lock) {
        LOGE(TAG, "Failed to allocate boot actions lock");
        return;
    }
    g_config = (BootActionsConfig*)config_psram_alloc(sizeof(BootActionsConfig), "boot_actions");
    if (!g_config) {
        LOGE(TAG, "Failed to allocate boot actions config");
        return;  // feature disabled
    }
    g_config_valid = load_from_flash(g_config);
}

bool boot_actions_to_json(JsonArray actions, bool* config_reset) {
    if (!g_config || !g_lock || actions.isNull()) return false;
    if (xSemaphoreTake(g_lock, portMAX_DELAY) != pdTRUE) return false;
    if (!g_config_valid) {
        xSemaphoreGive(g_lock);
        return false;
    }
    if (config_reset) *config_reset = g_config_reset;
    for (uint8_t index = 0; index < g_config->action_count; ++index) {
        JsonObject action = actions.createNestedObject();
        action_to_json(g_config->actions[index], action);
    }
    xSemaphoreGive(g_lock);
    return true;
}

bool boot_actions_save_raw(const uint8_t* json, size_t len) {
    if (!g_config || !g_lock || !json || !len || len > 4096) {
        LOGW(TAG, "Boot actions save unavailable or invalid size");
        return false;
    }
    if (!json_keys_valid(json, len)) {
        LOGW(TAG, "Boot actions JSON contains an invalid NUL");
        return false;
    }
    BasicJsonDocument<PsramJsonAllocator> doc(4096);
    const DeserializationError error = deserializeJson(doc, json, len);
    if (error || doc.overflowed()) {
        LOGW(TAG, "Invalid boot actions JSON: %s", error.c_str());
        return false;
    }
    auto* replacement = static_cast<BootActionsConfig*>(
        config_psram_alloc(sizeof(BootActionsConfig), "boot_actions_save"));
    if (!replacement) {
        LOGE(TAG, "Failed to allocate boot actions for save");
        return false;
    }
    ActionStorage storage = {};
    if (!parse_config(doc, *replacement, storage, true)) {
        action_storage_clear(storage);
        free(replacement);
        return false;
    }
    if (xSemaphoreTake(g_lock, portMAX_DELAY) != pdTRUE) {
        action_storage_clear(storage);
        free(replacement);
        LOGE(TAG, "Failed to lock boot actions for save");
        return false;
    }
    const bool saved = storage_write_atomic(BOOT_ACTIONS_PATH, json, len);
    if (saved) {
        action_storage_clear(g_storage);
        *g_config = *replacement;
        action_storage_move(g_storage, storage);
        g_config_valid = true;
        g_config_reset = false;
    }
    xSemaphoreGive(g_lock);
    action_storage_clear(storage);
    free(replacement);
    if (!saved) {
        LOGE(TAG, "Atomic boot actions write failed");
        return false;
    }
    storage_publish_usage(false);

    LOGI(TAG, "Saved (%u bytes)", (unsigned)len);
    return true;
}

void boot_actions_dispatch() {
    if (!g_config || !g_lock) return;
    auto* snapshot = static_cast<BootActionsConfig*>(
        config_psram_alloc(sizeof(BootActionsConfig), "boot_actions_dispatch"));
    if (!snapshot) {
        LOGE(TAG, "Failed to allocate boot actions for dispatch");
        return;
    }
    ActionStorage storage = {};
    if (xSemaphoreTake(g_lock, portMAX_DELAY) != pdTRUE) {
        LOGE(TAG, "Failed to lock boot actions for dispatch");
        free(snapshot);
        return;
    }
    snapshot->action_count = g_config->action_count;
    const bool copied = action_storage_copy_actions(storage, snapshot->actions, g_config->actions,
                                                    snapshot->action_count);
    xSemaphoreGive(g_lock);
    if (!copied) {
        LOGE(TAG, "Failed to copy boot actions for dispatch");
        action_storage_clear(storage);
        free(snapshot);
        return;
    }

    // Note: runs from setup() on the Arduino main task, not the LVGL task.
    // binding_template_resolve() uses internal buffers that are not thread-safe,
    // but this is safe at boot because the LVGL task is not yet processing
    // user-driven events that could trigger concurrent binding resolution.
    if (snapshot->action_count) {
        LOGI(TAG, "Dispatching %u boot action(s)", snapshot->action_count);
        action_list_dispatch(snapshot->actions, snapshot->action_count, "Boot");
    }
    action_storage_clear(storage);
    free(snapshot);
}

#endif // HAS_DISPLAY
