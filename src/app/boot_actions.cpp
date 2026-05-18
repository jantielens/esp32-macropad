#include "boot_actions.h"

#if HAS_DISPLAY

#include "action_dispatch.h"
#include "action_parse.h"
#include "fs_health.h"
#include "log_manager.h"

#include <ArduinoJson.h>
#include "storage.h"
#include <string.h>

#define TAG "BootAct"

static const char* BOOT_ACTIONS_PATH = "/config/boot_actions.json";

// RAM cache (always valid — empty if file missing)
static BootActionsConfig g_config;
static bool g_loaded = false;

static void apply_defaults(BootActionsConfig* cfg) {
    memset(cfg, 0, sizeof(BootActionsConfig));
}

static bool load_from_flash(BootActionsConfig* cfg) {
    apply_defaults(cfg);

    if (!Storage.exists(BOOT_ACTIONS_PATH)) {
        LOGD(TAG, "No boot actions file, using defaults");
        return false;
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

    StaticJsonDocument<2048> doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();

    if (err) {
        LOGE(TAG, "JSON parse error: %s", err.c_str());
        return false;
    }

    JsonVariant v = doc["actions"];
    if (v.is<JsonArray>()) {
        JsonArray arr = v.as<JsonArray>();
        for (size_t i = 0; i < arr.size() && cfg->action_count < MAX_BUTTON_ACTIONS; i++) {
            if (!arr[i].is<JsonObject>()) continue;
            action_parse(arr[i].as<JsonObject>(), cfg->actions[cfg->action_count]);
            if (cfg->actions[cfg->action_count].type[0]) cfg->action_count++;
        }
    }

    LOGI(TAG, "Loaded %u boot action(s)", cfg->action_count);
    return true;
}

void boot_actions_init() {
    load_from_flash(&g_config);
    g_loaded = true;
}

const BootActionsConfig* boot_actions_get() {
    if (!g_loaded) {
        apply_defaults(&g_config);
        g_loaded = true;
    }
    return &g_config;
}

bool boot_actions_save_raw(const uint8_t* json, size_t len) {
    if (!json || len == 0) return false;

    File f = Storage.open(BOOT_ACTIONS_PATH, "w");
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

    // Update RAM cache
    load_from_flash(&g_config);
    storage_publish_usage(false);

    LOGI(TAG, "Saved (%u bytes)", (unsigned)len);
    return true;
}

void boot_actions_dispatch() {
    const BootActionsConfig* cfg = boot_actions_get();
    if (cfg->action_count == 0) return;

    // Note: runs from setup() on the Arduino main task, not the LVGL task.
    // binding_template_resolve() uses internal buffers that are not thread-safe,
    // but this is safe at boot because the LVGL task is not yet processing
    // user-driven events that could trigger concurrent binding resolution.
    LOGI(TAG, "Dispatching %u boot action(s)", cfg->action_count);
    for (uint8_t i = 0; i < cfg->action_count; i++) {
        action_dispatch(cfg->actions[i], "Boot");
    }
}

#endif // HAS_DISPLAY
