#include "hw_button_config.h"

#if HAS_BUTTON

#include "action_list.h"
#include "log_manager.h"
#include "psram_json_allocator.h"

#include <ArduinoJson.h>
#include "storage.h"
#include <string.h>

#define TAG "HwBtnCfg"

static const char* HW_BUTTONS_PATH = "/config/hw_buttons.json";

// RAM cache (always valid — empty if file missing). Sized to NUM_HW_BUTTONS
// (not MAX_HW_BUTTONS) to minimize RAM on constrained boards.
static HwButtonConfig g_config[NUM_HW_BUTTONS];
static bool g_loaded = false;

static void apply_defaults() {
    memset(g_config, 0, sizeof(g_config));
}

static bool load_from_flash() {
    apply_defaults();

    if (!Storage.exists(HW_BUTTONS_PATH)) {
        LOGD(TAG, "No hw buttons file, using defaults");
        return false;
    }

    File f = Storage.open(HW_BUTTONS_PATH, "r");
    if (!f) {
        LOGW(TAG, "Failed to open hw buttons config");
        return false;
    }

    size_t file_size = f.size();
    if (file_size == 0 || file_size > 4096) {
        LOGW(TAG, "Invalid hw buttons size: %u", (unsigned)file_size);
        f.close();
        return false;
    }

    BasicJsonDocument<PsramJsonAllocator> doc(4096);
    DeserializationError err = deserializeJson(doc, f);
    f.close();

    if (err) {
        LOGE(TAG, "JSON parse error: %s", err.c_str());
        return false;
    }

    JsonArray buttons = doc["buttons"].as<JsonArray>();
    if (buttons.isNull()) {
        LOGD(TAG, "No 'buttons' array; defaults");
        return false;
    }

    // Parse up to NUM_HW_BUTTONS entries; extras (board changed) are ignored,
    // missing entries default to empty.
    uint8_t n = 0;
    for (size_t i = 0; i < buttons.size() && i < NUM_HW_BUTTONS; i++) {
        JsonObject b = buttons[i].as<JsonObject>();
        if (b.isNull()) continue;
        g_config[i].tap_count =
            action_list_parse(b["tap_actions"], g_config[i].tap_actions, MAX_BUTTON_ACTIONS);
        g_config[i].hold_count =
            action_list_parse(b["hold_actions"], g_config[i].hold_actions, MAX_BUTTON_ACTIONS);
        n++;
    }

    LOGI(TAG, "Loaded config for %u button(s)", n);
    return true;
}

void hw_button_config_init() {
    storage_mount();  // idempotent — ensures FS is mounted on headless boards too
    load_from_flash();
    g_loaded = true;
}

const HwButtonConfig* hw_button_config_get(uint8_t index) {
    if (index >= NUM_HW_BUTTONS) return nullptr;
    if (!g_loaded) {
        apply_defaults();
        g_loaded = true;
    }
    return &g_config[index];
}

bool hw_button_config_save_raw(const uint8_t* json, size_t len) {
    if (!json || len == 0) return false;

    File f = Storage.open(HW_BUTTONS_PATH, "w");
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
    load_from_flash();
    storage_publish_usage(false);

    LOGI(TAG, "Saved (%u bytes)", (unsigned)len);
    return true;
}

#endif  // HAS_BUTTON
