#include "shutter_session_actions.h"

#if IS_SHUTTER_TESTER

#include "action_dispatch.h"
#include "action_list.h"
#include "log_manager.h"
#include "psram_json_allocator.h"
#include "storage.h"

#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string.h>

#define TAG "ShutSessAct"

static const char* CONFIG_PATH = "/config/shutter_session_actions.json";

// RAM cache (always valid — empty if file missing)
static ShutterSessionActionsConfig g_config;
static bool g_loaded = false;

// Deferred-dispatch flag for save_complete. Set from background persist task,
// drained from the LVGL/main loop. Spinlock keeps the read-and-clear atomic.
static portMUX_TYPE s_complete_mux = portMUX_INITIALIZER_UNLOCKED;
static volatile bool s_save_complete_pending = false;

static void apply_defaults(ShutterSessionActionsConfig* cfg) {
    memset(cfg, 0, sizeof(ShutterSessionActionsConfig));
}

static bool load_from_flash(ShutterSessionActionsConfig* cfg) {
    apply_defaults(cfg);

    if (!Storage.exists(CONFIG_PATH)) {
        LOGD(TAG, "No config file, using defaults");
        return false;
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

    BasicJsonDocument<PsramJsonAllocator> doc(2048);
    DeserializationError err = deserializeJson(doc, f);
    f.close();

    if (err) {
        LOGE(TAG, "JSON parse error: %s", err.c_str());
        return false;
    }

    cfg->save_start_count    = action_list_parse(doc["save_start_actions"],    cfg->save_start_actions,    MAX_BUTTON_ACTIONS);
    cfg->save_complete_count = action_list_parse(doc["save_complete_actions"], cfg->save_complete_actions, MAX_BUTTON_ACTIONS);

    LOGI(TAG, "Loaded (start=%u complete=%u)",
         cfg->save_start_count, cfg->save_complete_count);
    return true;
}

void shutter_session_actions_init() {
    load_from_flash(&g_config);
    g_loaded = true;
}

const ShutterSessionActionsConfig* shutter_session_actions_get() {
    if (!g_loaded) {
        apply_defaults(&g_config);
        g_loaded = true;
    }
    return &g_config;
}

bool shutter_session_actions_save_raw(const uint8_t* json, size_t len) {
    if (!json || len == 0) return false;

    File f = Storage.open(CONFIG_PATH, "w");
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

    load_from_flash(&g_config);
    storage_publish_usage(false);

    LOGI(TAG, "Saved (%u bytes)", (unsigned)len);
    return true;
}

static void dispatch_guarded(const ButtonAction& act) {
    if (!act.type[0]) return;  // none configured
    if (shutter_session_actions_is_self_trigger(act)) {
        LOGW(TAG, "Skipping self-triggering shutter action (%s)", act.shutter_command);
        return;
    }
    action_dispatch(act, "SessSave");
}

static void dispatch_sequence(const ButtonAction* actions, uint8_t count) {
    for (uint8_t i = 0; i < count; i++) {
        dispatch_guarded(actions[i]);
    }
}

void shutter_session_actions_dispatch_start() {
    const ShutterSessionActionsConfig* cfg = shutter_session_actions_get();
    dispatch_sequence(cfg->save_start_actions, cfg->save_start_count);
}

void shutter_session_actions_notify_complete() {
    taskENTER_CRITICAL(&s_complete_mux);
    s_save_complete_pending = true;
    taskEXIT_CRITICAL(&s_complete_mux);
}

void shutter_session_actions_loop() {
    bool pending;
    taskENTER_CRITICAL(&s_complete_mux);
    pending = s_save_complete_pending;
    s_save_complete_pending = false;
    taskEXIT_CRITICAL(&s_complete_mux);
    if (!pending) return;

    const ShutterSessionActionsConfig* cfg = shutter_session_actions_get();
    dispatch_sequence(cfg->save_complete_actions, cfg->save_complete_count);
}

#endif // IS_SHUTTER_TESTER
