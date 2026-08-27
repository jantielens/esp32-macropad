#include "camera_motion_actions.h"

#if CAMERA_MOTION_ACTIONS_ENABLED

#include "action_dispatch.h"
#include "action_list.h"
#include "action_parse.h"
#include "config_psram.h"
#include "log_manager.h"
#include "main_loop_bridge.h"
#include "psram_json_allocator.h"
#include "storage.h"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <string.h>

namespace {

constexpr const char* kConfigPath = "/config/camera_motion_actions.json";
constexpr size_t kJsonCapacity = 2048;
constexpr uint32_t kSaveTimeoutMs = 5000;
CameraMotionActionsConfig* s_config = nullptr;
SemaphoreHandle_t s_lock = nullptr;

struct SaveRequest {
    const uint8_t* json;
    size_t len;
};

void apply_defaults(CameraMotionActionsConfig* config) {
    if (config) memset(config, 0, sizeof(*config));
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
    if (error) {
        LOGW("Camera", "Motion actions JSON parse error: %s", error.c_str());
        return false;
    }
    s_config->action_count = action_list_parse(doc["actions"], s_config->actions, MAX_BUTTON_ACTIONS);
    LOGI("Camera", "Loaded %u motion action(s)", s_config->action_count);
    return true;
}

bool save_raw_on_main(const uint8_t* json, size_t len) {
    if (!json || !len || len > kJsonCapacity || !s_lock) return false;
    File file = Storage.open(kConfigPath, "w");
    if (!file) {
        LOGW("Camera", "Failed to write motion actions");
        return false;
    }
    const size_t written = file.write(json, len);
    file.close();
    if (written != len) return false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    load_from_storage();
    xSemaphoreGive(s_lock);
    storage_publish_usage(false);
    return true;
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
    ButtonAction actions[MAX_BUTTON_ACTIONS] = {};
    xSemaphoreTake(s_lock, portMAX_DELAY);
    const uint8_t count = s_config->action_count > MAX_BUTTON_ACTIONS
        ? MAX_BUTTON_ACTIONS : s_config->action_count;
    memcpy(actions, s_config->actions, static_cast<size_t>(count) * sizeof(ButtonAction));
    xSemaphoreGive(s_lock);
    if (!count) return;
    LOGI("Camera", "Dispatching %u motion action(s)", count);
    action_list_dispatch(actions, count, "Camera motion");
}

void camera_motion_actions_to_json(JsonArray actions) {
    if (!s_config || !s_lock) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    const uint8_t count = s_config->action_count > MAX_BUTTON_ACTIONS
        ? MAX_BUTTON_ACTIONS : s_config->action_count;
    for (uint8_t index = 0; index < count; ++index) {
        JsonObject action = actions.createNestedObject();
        action_to_json(s_config->actions[index], action);
    }
    xSemaphoreGive(s_lock);
}

bool camera_motion_actions_save_raw(const uint8_t* json, size_t len) {
    const SaveRequest request = {.json = json, .len = len};
    bool saved = false;
    char message[64] = {};
    return loop_bridge_dispatch(save_on_main, &request, sizeof(request), kSaveTimeoutMs,
                                &saved, message, sizeof(message)) == LOOP_BRIDGE_OK && saved;
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