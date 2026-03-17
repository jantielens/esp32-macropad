#include "swipe_config.h"

#if HAS_DISPLAY

#include "log_manager.h"
#include "fs_health.h"

#include <ArduinoJson.h>
#include <LittleFS.h>
#include <string.h>

#define TAG "SwipeCfg"

static const char* SWIPE_CONFIG_PATH = "/config/swipe_actions.json";

// RAM cache (always valid — defaults if file missing)
static SwipeConfig g_config;
static bool g_loaded = false;

// Parse a JSON action object into a ButtonAction struct.
// Same JSON schema as pad button actions: { "type", "target", "topic", "payload", "sequence" }
static void parse_swipe_action(JsonVariant v, ButtonAction* act) {
    memset(act, 0, sizeof(ButtonAction));
    if (!v.is<JsonObject>()) return;
    JsonObject a = v.as<JsonObject>();
    strlcpy(act->type,         a["type"]     | "", CONFIG_ACTION_TYPE_MAX_LEN);
    strlcpy(act->screen_id,    a["target"]   | "", CONFIG_SCREEN_ID_MAX_LEN);
    strlcpy(act->mqtt_topic,   a["topic"]    | "", CONFIG_MQTT_TOPIC_MAX_LEN);
    strlcpy(act->mqtt_payload, a["payload"]  | "", CONFIG_MQTT_PAYLOAD_MAX_LEN);
    strlcpy(act->key_sequence, a["sequence"] | "", CONFIG_KEY_SEQ_MAX_LEN);
}

static void apply_defaults(SwipeConfig* cfg) {
    memset(cfg, 0, sizeof(SwipeConfig));
    // Default: swipe right = navigate back (natural iOS-like gesture)
    strlcpy(cfg->swipe_right.type, ACTION_TYPE_BACK, CONFIG_ACTION_TYPE_MAX_LEN);
}

static bool load_from_flash(SwipeConfig* cfg) {
    apply_defaults(cfg);

    if (!LittleFS.exists(SWIPE_CONFIG_PATH)) {
        LOGD(TAG, "No swipe config file, using defaults");
        return false;
    }

    File f = LittleFS.open(SWIPE_CONFIG_PATH, "r");
    if (!f) {
        LOGW(TAG, "Failed to open swipe config");
        return false;
    }

    size_t file_size = f.size();
    if (file_size == 0 || file_size > 4096) {
        LOGW(TAG, "Invalid swipe config size: %u", (unsigned)file_size);
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

    parse_swipe_action(doc["swipe_left"],  &cfg->swipe_left);
    parse_swipe_action(doc["swipe_right"], &cfg->swipe_right);
    parse_swipe_action(doc["swipe_up"],    &cfg->swipe_up);
    parse_swipe_action(doc["swipe_down"],  &cfg->swipe_down);

    LOGI(TAG, "Loaded (L=%s R=%s U=%s D=%s)",
         cfg->swipe_left.type[0]  ? cfg->swipe_left.type  : "-",
         cfg->swipe_right.type[0] ? cfg->swipe_right.type : "-",
         cfg->swipe_up.type[0]    ? cfg->swipe_up.type    : "-",
         cfg->swipe_down.type[0]  ? cfg->swipe_down.type  : "-");
    return true;
}

void swipe_config_init() {
    load_from_flash(&g_config);
    g_loaded = true;
}

const SwipeConfig* swipe_config_get() {
    if (!g_loaded) {
        apply_defaults(&g_config);
        g_loaded = true;
    }
    return &g_config;
}

bool swipe_config_save_raw(const uint8_t* json, size_t len) {
    if (!json || len == 0) return false;

    File f = LittleFS.open(SWIPE_CONFIG_PATH, "w");
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
    fs_health_set_storage_usage(LittleFS.usedBytes(), LittleFS.totalBytes());

    LOGI(TAG, "Saved (%u bytes)", (unsigned)len);
    return true;
}

void swipe_config_reload() {
    load_from_flash(&g_config);
}

#endif // HAS_DISPLAY
