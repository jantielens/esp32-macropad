#include "timer_config.h"

#if HAS_DISPLAY

#include "action_list.h"
#include "config_psram.h"
#include "fs_health.h"
#include "log_manager.h"
#include "psram_json_allocator.h"
#include "timer_engine.h"

#include <ArduinoJson.h>
#include "storage.h"
#include <string.h>

#define TAG "TimerCfg"

static const char* TIMER_CONFIG_PATH = "/config/timers.json";

// RAM cache (always valid — defaults if file missing). Heap-allocated
// (PSRAM preferred, SRAM fallback) via config_psram_alloc().
static TimerConfig* g_config = nullptr;
static bool g_loaded = false;

static void apply_defaults(TimerConfig* cfg) {
    if (!cfg) return;
    memset(cfg, 0, sizeof(TimerConfig));
    // All timers default to count-up mode, no expire actions
}

// Apply loaded config to the timer engine
static void apply_to_engine(const TimerConfig* cfg) {
    if (!cfg) return;
    for (uint8_t i = 0; i < TIMER_COUNT; i++) {
        uint8_t id = i + 1;
        const TimerSettings& ts = cfg->timers[i];
        timer_set_mode(id, ts.mode);
        if (ts.mode == TIMER_MODE_DOWN && ts.countdown > 0) {
            timer_set_countdown(id, ts.countdown);
        }
        if (ts.expire_action_count > 0) {
            timer_set_expire_actions(id, ts.expire_actions, ts.expire_action_count);
        } else {
            timer_clear_expire_actions(id);
        }
    }
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

        TimerSettings& ts = cfg->timers[i];

        // Mode
        const char* mode_str = tobj["mode"] | "up";
        ts.mode = (strcmp(mode_str, "down") == 0) ? TIMER_MODE_DOWN : TIMER_MODE_UP;

        // Countdown
        ts.countdown = tobj["countdown"] | 0;

        // Expire actions
        ts.expire_action_count = action_list_parse(tobj["expire_actions"], ts.expire_actions, TIMER_MAX_EXPIRE_ACTIONS);

        LOGI(TAG, "Timer %u: mode=%s countdown=%us expire_actions=%u",
             i + 1, ts.mode == TIMER_MODE_DOWN ? "down" : "up",
             (unsigned)ts.countdown, ts.expire_action_count);
    }

    return true;
}

void timer_config_init() {
    g_config = (TimerConfig*)config_psram_alloc(sizeof(TimerConfig), "timer_config");
    if (!g_config) {
        LOGE(TAG, "Failed to allocate timer config");
        return;  // feature disabled
    }
    load_from_flash(g_config);
    apply_to_engine(g_config);
    g_loaded = true;
}

const TimerConfig* timer_config_get() {
    if (!g_config) return nullptr;
    if (!g_loaded) {
        apply_defaults(g_config);
        g_loaded = true;
    }
    return g_config;
}

bool timer_config_save_raw(const uint8_t* json, size_t len) {
    if (!json || len == 0) return false;

    File f = Storage.open(TIMER_CONFIG_PATH, "w");
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

    // Update RAM cache and re-apply to engine
    load_from_flash(g_config);
    apply_to_engine(g_config);
    storage_publish_usage(false);

    LOGI(TAG, "Saved (%u bytes)", (unsigned)len);
    return true;
}

#endif // HAS_DISPLAY
