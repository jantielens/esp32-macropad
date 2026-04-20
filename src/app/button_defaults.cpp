#include "button_defaults.h"

#if HAS_DISPLAY

#include "log_manager.h"
#include "fs_health.h"
#include "pad_config.h"

#include <ArduinoJson.h>
#include <LittleFS.h>
#include <string.h>

#define TAG "BtnDef"

static const char* BTN_DEFAULTS_PATH = "/config/button_defaults.json";

// RAM cache (always valid — all-empty if file missing)
static ButtonDefaults g_defaults;
static bool g_loaded = false;

// Forward declaration of parse helper from pad_config.cpp — we duplicate the
// minimal parsing logic here to stay self-contained.
static void parse_bindable(JsonVariant v, char* out, size_t out_len, const char* def, bool is_color = true) {
    if (v.isNull()) { strlcpy(out, def, out_len); return; }
    if (v.is<unsigned long>() || v.is<long>()) {
        if (is_color) {
            uint32_t val = (uint32_t)v.as<unsigned long>();
            snprintf(out, out_len, "#%06X", val & 0xFFFFFF);
        } else {
            long val = v.as<long>();
            snprintf(out, out_len, "%ld", val);
        }
        return;
    }
    if (v.is<const char*>()) {
        const char* s = v.as<const char*>();
        if (s && s[0]) { strlcpy(out, s, out_len); return; }
    }
    strlcpy(out, def, out_len);
}

static bool load_from_flash(ButtonDefaults* d) {
    memset(d, 0, sizeof(ButtonDefaults));

    if (!LittleFS.exists(BTN_DEFAULTS_PATH)) {
        LOGD(TAG, "No button defaults file, using empty defaults");
        return false;
    }

    File f = LittleFS.open(BTN_DEFAULTS_PATH, "r");
    if (!f) {
        LOGW(TAG, "Failed to open button defaults");
        return false;
    }

    size_t file_size = f.size();
    if (file_size == 0 || file_size > 4096) {
        LOGW(TAG, "Invalid button defaults size: %u", (unsigned)file_size);
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

    parse_bindable(doc["bg_color"], d->bg_color, CONFIG_COLOR_MAX_LEN, "");
    parse_bindable(doc["fg_color"], d->fg_color, CONFIG_COLOR_MAX_LEN, "");
    parse_bindable(doc["border_color"], d->border_color, CONFIG_COLOR_MAX_LEN, "");
    parse_bindable(doc["border_width"], d->border_width, CONFIG_BINDABLE_SHORT_LEN, "", false);
    parse_bindable(doc["corner_radius"], d->corner_radius, CONFIG_BINDABLE_SHORT_LEN, "", false);
    strlcpy(d->label_top_style, doc["label_top_style"] | "", CONFIG_LABEL_STYLE_MAX_LEN);
    strlcpy(d->label_center_style, doc["label_center_style"] | "", CONFIG_LABEL_STYLE_MAX_LEN);
    strlcpy(d->label_bottom_style, doc["label_bottom_style"] | "", CONFIG_LABEL_STYLE_MAX_LEN);
    {
        const char* ip = doc["icon_position"] | "";
        if (ip[0] == 'l')      d->icon_position = ICON_POS_LEFT;
        else if (ip[0] == 'c') d->icon_position = ICON_POS_CENTER;
        else                    d->icon_position = ICON_POS_ABOVE;
    }

    LOGI(TAG, "Loaded device button defaults");
    return true;
}

void button_defaults_init() {
    bool loaded = load_from_flash(&g_defaults);
    g_loaded = true;

    // If defaults were loaded, rebuild pad caches so they pick up the
    // saved defaults (pad_config_init runs first with empty defaults).
    if (loaded) {
        pad_config_rebuild_all_caches();
    }
}

const ButtonDefaults* button_defaults_get() {
    if (!g_loaded) {
        memset(&g_defaults, 0, sizeof(ButtonDefaults));
        g_loaded = true;
    }
    return &g_defaults;
}

bool button_defaults_save_raw(const uint8_t* json, size_t len) {
    if (!json || len == 0) return false;

    File f = LittleFS.open(BTN_DEFAULTS_PATH, "w");
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
    load_from_flash(&g_defaults);
    fs_health_set_storage_usage(LittleFS.usedBytes(), LittleFS.totalBytes());

    // Rebuild all pad caches so they pick up the new defaults
    pad_config_rebuild_all_caches();

    LOGI(TAG, "Saved (%u bytes)", (unsigned)len);
    return true;
}

#endif // HAS_DISPLAY
