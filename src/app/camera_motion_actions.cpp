#include "camera_motion_actions.h"

#if CAMERA_MOTION_ACTIONS_ENABLED

#include "action_dispatch.h"
#include "action_list.h"
#include "action_parse.h"
#include "action_registry.h"
#include "action_storage.h"
#include "action_validate.h"
#include "config_psram.h"
#include "log_manager.h"
#include "main_loop_bridge.h"
#include "psram_json_allocator.h"
#include "storage.h"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <stdlib.h>
#include <string.h>

namespace {

constexpr const char* kConfigPath = "/config/camera_motion_actions.json";
constexpr size_t kJsonCapacity = 2048;
constexpr uint32_t kSaveTimeoutMs = 5000;
CameraMotionActionsConfig* s_config = nullptr;
ActionStorage s_storage = {};
SemaphoreHandle_t s_lock = nullptr;

struct SaveRequest {
    const uint8_t* json;
    size_t len;
};

void apply_defaults(CameraMotionActionsConfig* config) {
    if (config) memset(config, 0, sizeof(*config));
}

bool parse_actions(JsonArray actions, CameraMotionActionsConfig& config,
                   ActionStorage& storage, bool validate_bindings) {
    if (actions.isNull() || actions.size() > MAX_BUTTON_ACTIONS) {
        LOGW("Camera", "Invalid motion action list");
        return false;
    }
    ActionStorageScope scope(&storage);
    for (JsonVariant value : actions) {
        if (!value.is<JsonObject>()) {
            LOGW("Camera", "Motion action must be an object");
            return false;
        }
        JsonObject action = value.as<JsonObject>();
        const char* type = action["type"] | "";
        const char* error = validate_bindings ? action_validate_json(action)
            : (!action.containsKey("type") && action.containsKey("op")
                ? action_operation_validate_json(action, false)
                : (!action["type"].is<const char*>() || action.containsKey("args") ||
                   (action.containsKey("op") && strcmp(type, ACTION_TYPE_VISUAL_ALERT))
                    ? "invalid action envelope" : nullptr));
        if (!error && type[0] && strcmp(type, "none") && !action_type_is_supported(type))
            error = "action is unavailable on this board";
        if (error) {
            LOGW("Camera", "Invalid motion action: %s", error);
            return false;
        }
        if (!strcmp(type, "none")) continue;
        action_parse(action, config.actions[config.action_count]);
        if (storage.failed || !config.actions[config.action_count].type[0]) {
            LOGW("Camera", "Cannot parse motion action");
            return false;
        }
        ++config.action_count;
    }
    return true;
}

bool load_from_storage() {
    if (!s_config) return false;
    apply_defaults(s_config);
    if (!Storage.exists(kConfigPath)) return false;

    File file = Storage.open(kConfigPath, "r");
    if (!file) {
        LOGW("Camera", "Failed to open motion actions");
        return false;
    }
    if (file.size() == 0 || file.size() > kJsonCapacity) {
        file.close();
        LOGW("Camera", "Invalid motion actions size");
        return false;
    }
    BasicJsonDocument<PsramJsonAllocator> doc(kJsonCapacity);
    const DeserializationError error = deserializeJson(doc, file);
    file.close();
    if (error || doc.overflowed()) {
        LOGW("Camera", "Motion actions JSON parse error: %s", error.c_str());
        return false;
    }
    ActionStorage storage = {};
    if (!parse_actions(doc["actions"].as<JsonArray>(), *s_config, storage, false)) {
        action_storage_clear(storage);
        apply_defaults(s_config);
        return false;
    }
    action_storage_move(s_storage, storage);
    LOGI("Camera", "Loaded %u motion action(s)", s_config->action_count);
    return true;
}

bool save_raw_on_main(const uint8_t* json, size_t len) {
    if (!json || !len || len > kJsonCapacity || !s_lock || !s_config) return false;
    BasicJsonDocument<PsramJsonAllocator> doc(kJsonCapacity);
    const DeserializationError error = deserializeJson(doc, json, len);
    if (error || doc.overflowed()) {
        LOGW("Camera", "Invalid motion actions JSON: %s", error.c_str());
        return false;
    }
    auto* replacement = static_cast<CameraMotionActionsConfig*>(
        config_psram_alloc(sizeof(CameraMotionActionsConfig), "camera_motion_save"));
    if (!replacement) return false;
    ActionStorage storage = {};
    if (!parse_actions(doc["actions"].as<JsonArray>(), *replacement, storage, true)) {
        action_storage_clear(storage);
        free(replacement);
        return false;
    }
    if (xSemaphoreTake(s_lock, portMAX_DELAY) != pdTRUE) {
        LOGE("Camera", "Failed to lock motion actions for save");
        action_storage_clear(storage);
        free(replacement);
        return false;
    }
    const bool saved = storage_write_atomic(kConfigPath, json, len);
    if (saved) {
        action_storage_clear(s_storage);
        *s_config = *replacement;
        action_storage_move(s_storage, storage);
    }
    xSemaphoreGive(s_lock);
    action_storage_clear(storage);
    free(replacement);
    if (!saved) {
        LOGW("Camera", "Failed to write motion actions");
        return false;
    }
    storage_publish_usage(false);
    return true;
}

void cleanup_save_request(const void* opaque) {
    const SaveRequest* request = static_cast<const SaveRequest*>(opaque);
    free(const_cast<uint8_t*>(request->json));
}

void save_on_main(const void* opaque, bool* ok, char* message, size_t message_len) {
    const SaveRequest* request = static_cast<const SaveRequest*>(opaque);
    if (!request || !save_raw_on_main(request->json, request->len)) {
        strlcpy(message, "motion actions could not be saved", message_len);
        return;
    }
    *ok = true;
}

} // namespace

void camera_motion_actions_init() {
    if (!s_lock) s_lock = xSemaphoreCreateMutex();
    if (!s_lock) {
        LOGE("Camera", "Failed to create motion actions lock");
        return;
    }
    s_config = static_cast<CameraMotionActionsConfig*>(
        config_psram_alloc(sizeof(CameraMotionActionsConfig), "camera_motion_actions"));
    if (!s_config) {
        LOGE("Camera", "Failed to allocate motion actions");
        return;
    }
    storage_mount();
    xSemaphoreTake(s_lock, portMAX_DELAY);
    load_from_storage();
    xSemaphoreGive(s_lock);
}

void camera_motion_actions_on_presence_started() {
    if (!s_config || !s_lock) return;
    auto* snapshot = static_cast<CameraMotionActionsConfig*>(
        config_psram_alloc(sizeof(CameraMotionActionsConfig), "camera_motion_dispatch"));
    if (!snapshot) return;
    ActionStorage storage = {};
    if (xSemaphoreTake(s_lock, portMAX_DELAY) != pdTRUE) {
        LOGE("Camera", "Failed to lock motion actions for dispatch");
        free(snapshot);
        return;
    }
    const uint8_t count = s_config->action_count > MAX_BUTTON_ACTIONS
        ? MAX_BUTTON_ACTIONS : s_config->action_count;
    const bool copied = action_storage_copy_actions(storage, snapshot->actions, s_config->actions, count);
    xSemaphoreGive(s_lock);
    if (!copied) LOGE("Camera", "Failed to copy motion actions for dispatch");
    else if (count) {
        LOGI("Camera", "Dispatching %u motion action(s)", count);
        action_list_dispatch(snapshot->actions, count, "Camera motion");
    }
    action_storage_clear(storage);
    free(snapshot);
}

bool camera_motion_actions_to_json(JsonArray actions) {
    if (!s_config || !s_lock || actions.isNull()) return false;
    if (xSemaphoreTake(s_lock, portMAX_DELAY) != pdTRUE) {
        LOGE("Camera", "Failed to lock motion actions for serialization");
        return false;
    }
    const uint8_t count = s_config->action_count > MAX_BUTTON_ACTIONS
        ? MAX_BUTTON_ACTIONS : s_config->action_count;
    for (uint8_t index = 0; index < count; ++index) {
        JsonObject action = actions.createNestedObject();
        action_to_json(s_config->actions[index], action);
    }
    xSemaphoreGive(s_lock);
    return true;
}

bool camera_motion_actions_validate(JsonArray actions) {
    auto* config = static_cast<CameraMotionActionsConfig*>(
        config_psram_alloc(sizeof(CameraMotionActionsConfig), "camera_motion_validate"));
    if (!config) return false;
    ActionStorage storage = {};
    const bool valid = parse_actions(actions, *config, storage, true);
    action_storage_clear(storage);
    free(config);
    return valid;
}

bool camera_motion_actions_save_raw(const uint8_t* json, size_t len) {
    if (!json || !len || len > kJsonCapacity) return false;
    auto* owned = static_cast<uint8_t*>(config_psram_alloc(len, "camera_motion_request"));
    if (!owned) return false;
    memcpy(owned, json, len);
    const SaveRequest request = {.json = owned, .len = len};
    bool saved = false;
    char message[64] = {};
    const LoopBridgeResult result = loop_bridge_dispatch(save_on_main, &request, sizeof(request), kSaveTimeoutMs,
                                &saved, message, sizeof(message), cleanup_save_request);
    if (result != LOOP_BRIDGE_TIMEOUT) free(owned);
    if (result != LOOP_BRIDGE_OK) LOGW("Camera", "Motion actions save bridge failed (%d)", result);
    return result == LOOP_BRIDGE_OK && saved;
}

#if HAS_MQTT
void camera_motion_actions_collect_binding_topics(void* user_data) {
    if (!s_config || !s_lock) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    const uint8_t count = s_config->action_count > MAX_BUTTON_ACTIONS
        ? MAX_BUTTON_ACTIONS : s_config->action_count;
    for (uint8_t index = 0; index < count; ++index) {
        action_collect_binding_topics(s_config->actions[index], user_data);
    }
    xSemaphoreGive(s_lock);
}
#endif

#endif // CAMERA_MOTION_ACTIONS_ENABLED