#include "button_defaults.h"

#if HAS_DISPLAY

#include "log_manager.h"
#include "fs_health.h"
#include "pad_config.h"
#include "psram_json_allocator.h"

#include <ArduinoJson.h>
#include "storage.h"
#include <string.h>

#define TAG "BtnDef"

static const char* BTN_DEFAULTS_PATH = "/config/button_defaults.json";

// RAM cache (always valid — all-empty if file missing)
static ButtonDefaults g_defaults;
static bool g_loaded = false;

static void init_shadow_defaults(ButtonShadowSettings* shadow) {
    memset(shadow, 0, sizeof(*shadow));
    shadow->type = BUTTON_SHADOW_TYPE_OPAQUE;
    shadow->color_mode = BUTTON_SHADOW_COLOR_FIXED;
    strlcpy(shadow->color, "#101010", sizeof(shadow->color));
    shadow->darken_pct = 35;
    shadow->offset_y_px = 5;
    shadow->drop_blur_px = 4;
}

static void init_layout_defaults(PadLayoutSettings* layout) {
    memset(layout, 0, sizeof(*layout));
    layout->spacing_px = 6;
    layout->pixel_shift_distance_px = 4;
}

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
    init_shadow_defaults(&d->shadow);
    init_layout_defaults(&d->layout);

    if (!Storage.exists(BTN_DEFAULTS_PATH)) {
        LOGD(TAG, "No pad and button defaults file, using empty defaults");
        return false;
    }

    File f = Storage.open(BTN_DEFAULTS_PATH, "r");
    if (!f) {
        LOGW(TAG, "Failed to open pad and button defaults");
        return false;
    }

    size_t file_size = f.size();
    if (file_size == 0 || file_size > 4096) {
        LOGW(TAG, "Invalid pad and button defaults size: %u", (unsigned)file_size);
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

    parse_bindable(doc["bg_color"], d->bg_color, CONFIG_COLOR_MAX_LEN, "");
    parse_bindable(doc["default_pad_bg_color"], d->default_pad_bg_color,
                   CONFIG_COLOR_MAX_LEN, "");
    if (!d->default_pad_bg_color[0]) {
        // Preserve device defaults saved while this feature used its initial key.
        parse_bindable(doc["pad_bg_color"], d->default_pad_bg_color,
                       CONFIG_COLOR_MAX_LEN, "");
    }
    parse_bindable(doc["fg_color"], d->fg_color, CONFIG_COLOR_MAX_LEN, "");
    parse_bindable(doc["border_color"], d->border_color, CONFIG_COLOR_MAX_LEN, "");
    parse_bindable(doc["border_width"], d->border_width, CONFIG_BINDABLE_SHORT_LEN, "", false);
    parse_bindable(doc["corner_radius"], d->corner_radius, CONFIG_BINDABLE_SHORT_LEN, "", false);
    parse_bindable(doc["content_pad"], d->content_pad, CONFIG_BINDABLE_SHORT_LEN, "", false);
    d->shadow.enabled = doc["button_shadow_enabled"] | false;
    const char* shadow_type = doc["button_shadow_type"] | "opaque";
    d->shadow.type = strcmp(shadow_type, "drop") == 0
        ? BUTTON_SHADOW_TYPE_DROP : BUTTON_SHADOW_TYPE_OPAQUE;
    const char* shadow_color_mode = doc["button_shadow_color_mode"] | "fixed";
    d->shadow.color_mode = strcmp(shadow_color_mode, "darken_background") == 0
        ? BUTTON_SHADOW_COLOR_DARKEN_BACKGROUND : BUTTON_SHADOW_COLOR_FIXED;
    parse_bindable(doc["button_shadow_color"], d->shadow.color,
                   sizeof(d->shadow.color), "#101010");
    d->shadow.darken_pct = constrain(doc["button_shadow_darken_pct"] | 35, 0, 100);
    d->shadow.offset_x_px = constrain(doc["button_shadow_offset_x_px"] | 0, -64, 64);
    d->shadow.offset_y_px = constrain(doc["button_shadow_offset_y_px"] | 5, -64, 64);
    d->shadow.drop_blur_px = constrain(doc["button_shadow_drop_blur_px"] | 4, 0, 32);
    d->layout.spacing_px = constrain(doc["button_spacing_px"] | 6, 0, 64);
    d->layout.inset_top_px = constrain(doc["pad_inset_top_px"] | 0, 0, 64);
    d->layout.inset_right_px = constrain(doc["pad_inset_right_px"] | 0, 0, 64);
    d->layout.inset_bottom_px = constrain(doc["pad_inset_bottom_px"] | 0, 0, 64);
    d->layout.inset_left_px = constrain(doc["pad_inset_left_px"] | 0, 0, 64);
    d->layout.pixel_shift_distance_px = constrain(doc["pixel_shift_distance_px"] | 4, 0, 8);
    strlcpy(d->label_top_style, doc["label_top_style"] | "", CONFIG_LABEL_STYLE_MAX_LEN);
    strlcpy(d->label_center_style, doc["label_center_style"] | "", CONFIG_LABEL_STYLE_MAX_LEN);
    strlcpy(d->label_bottom_style, doc["label_bottom_style"] | "", CONFIG_LABEL_STYLE_MAX_LEN);
    {
        const char* ip = doc["icon_position"] | "";
        if (ip[0] == 'l')      d->icon_position = ICON_POS_LEFT;
        else if (ip[0] == 'c') d->icon_position = ICON_POS_CENTER;
        else                    d->icon_position = ICON_POS_ABOVE;
    }

    LOGI(TAG, "Loaded device pad and button defaults");
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
        init_shadow_defaults(&g_defaults.shadow);
        init_layout_defaults(&g_defaults.layout);
        g_loaded = true;
    }
    return &g_defaults;
}

uint8_t button_defaults_get_pixel_shift_distance() {
    return button_defaults_get()->layout.pixel_shift_distance_px;
}

bool button_defaults_save_raw(const uint8_t* json, size_t len) {
    if (!json || len == 0) return false;

    File f = Storage.open(BTN_DEFAULTS_PATH, "w");
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
    storage_publish_usage(false);

    // Rebuild all pad caches so they pick up the new defaults
    pad_config_rebuild_all_caches();

    LOGI(TAG, "Saved (%u bytes)", (unsigned)len);
    return true;
}

#endif // HAS_DISPLAY
