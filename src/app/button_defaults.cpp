#include "button_defaults.h"

#if HAS_DISPLAY

#include "log_manager.h"
#include "fs_health.h"
#include "pad_config.h"
#include "psram_json_allocator.h"
#include "config_psram.h"
#include "pad_layout.h"
#include "font_store.h"

#include <ArduinoJson.h>
#include "storage.h"
#include <string.h>

#define TAG "BtnDef"

static const char* BTN_DEFAULTS_PATH = "/config/button_defaults.json";

static ButtonDefaults* g_defaults = nullptr;
static const ButtonDefaults g_empty_defaults = {
    "", "", "", "", "", "", "", "", "", "", ICON_POS_ABOVE,
    {DEFAULT_BUTTON_SHADOW_ENABLED, DEFAULT_BUTTON_SHADOW_TYPE, DEFAULT_BUTTON_SHADOW_COLOR_MODE,
     DEFAULT_BUTTON_SHADOW_COLOR, DEFAULT_BUTTON_SHADOW_DARKEN_PCT,
     DEFAULT_BUTTON_SHADOW_OFFSET_X_PX, DEFAULT_BUTTON_SHADOW_OFFSET_Y_PX, DEFAULT_BUTTON_SHADOW_DROP_BLUR_PX},
    {DEFAULT_BUTTON_SPACING_PX, DEFAULT_PAD_INSET_TOP_PX, DEFAULT_PAD_INSET_RIGHT_PX,
        DEFAULT_PAD_INSET_BOTTOM_PX, DEFAULT_PAD_INSET_LEFT_PX, DEFAULT_PIXEL_SHIFT_DISTANCE}, "@builtin", 0
};

static bool ensure_defaults() {
    if (!g_defaults) {
        g_defaults = static_cast<ButtonDefaults*>(config_psram_alloc(sizeof(ButtonDefaults), "button_defaults"));
        if (!g_defaults) return false;
        *g_defaults = g_empty_defaults;
    }
    return true;
}

static void init_shadow_defaults(ButtonShadowSettings* shadow) {
    *shadow = g_empty_defaults.shadow;
}

static void init_layout_defaults(PadLayoutSettings* layout) {
    *layout = g_empty_defaults.layout;
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

bool button_defaults_load_overrides(JsonDocument& doc) {
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

    DeserializationError err = deserializeJson(doc, f);
    f.close();

    if (err) {
        LOGE(TAG, "JSON parse error: %s", err.c_str());
        doc.clear();
        return false;
    }
    return doc.is<JsonObject>();
}

static bool load_from_flash(ButtonDefaults* d) {
    memset(d, 0, sizeof(ButtonDefaults));
    strlcpy(d->default_font, "@builtin", sizeof(d->default_font));
    init_shadow_defaults(&d->shadow);
    init_layout_defaults(&d->layout);
    BasicJsonDocument<PsramJsonAllocator> doc(2048);
    if (!button_defaults_load_overrides(doc)) return false;

    const char* font = doc["default_font"] | "@builtin";
    if (strcmp(font, "@builtin") == 0 || font_store_validate_name(font))
        strlcpy(d->default_font, font, sizeof(d->default_font));
    if (doc["base_text_size"].is<uint16_t>()) d->base_text_size = doc["base_text_size"].as<uint16_t>();

    parse_bindable(doc["bg_color"], d->bg_color, CONFIG_COLOR_MAX_LEN, "");
    parse_bindable(doc["default_pad_bg_color"], d->default_pad_bg_color,
                   CONFIG_COLOR_MAX_LEN, "");
    parse_bindable(doc["fg_color"], d->fg_color, CONFIG_COLOR_MAX_LEN, "");
    parse_bindable(doc["border_color"], d->border_color, CONFIG_COLOR_MAX_LEN, "");
    parse_bindable(doc["border_width"], d->border_width, CONFIG_BINDABLE_SHORT_LEN, "", false);
    parse_bindable(doc["corner_radius"], d->corner_radius, CONFIG_BINDABLE_SHORT_LEN, "", false);
    parse_bindable(doc["content_pad"], d->content_pad, CONFIG_BINDABLE_SHORT_LEN, "", false);
    d->shadow.enabled = doc["button_shadow_enabled"] | g_empty_defaults.shadow.enabled;
    const char* shadow_type = doc["button_shadow_type"] |
        (g_empty_defaults.shadow.type == BUTTON_SHADOW_TYPE_DROP ? "drop" : "opaque");
    d->shadow.type = strcmp(shadow_type, "drop") == 0
        ? BUTTON_SHADOW_TYPE_DROP : BUTTON_SHADOW_TYPE_OPAQUE;
    const char* shadow_color_mode = doc["button_shadow_color_mode"] |
        (g_empty_defaults.shadow.color_mode == BUTTON_SHADOW_COLOR_DARKEN_BACKGROUND ? "darken_background" : "fixed");
    d->shadow.color_mode = strcmp(shadow_color_mode, "darken_background") == 0
        ? BUTTON_SHADOW_COLOR_DARKEN_BACKGROUND : BUTTON_SHADOW_COLOR_FIXED;
    parse_bindable(doc["button_shadow_color"], d->shadow.color,
                   sizeof(d->shadow.color), DEFAULT_BUTTON_SHADOW_COLOR);
    d->shadow.darken_pct = constrain(doc["button_shadow_darken_pct"] | DEFAULT_BUTTON_SHADOW_DARKEN_PCT, 0, 100);
    d->shadow.offset_x_px = constrain(doc["button_shadow_offset_x_px"] | DEFAULT_BUTTON_SHADOW_OFFSET_X_PX, -64, 64);
    d->shadow.offset_y_px = constrain(doc["button_shadow_offset_y_px"] | DEFAULT_BUTTON_SHADOW_OFFSET_Y_PX, -64, 64);
    d->shadow.drop_blur_px = constrain(doc["button_shadow_drop_blur_px"] | DEFAULT_BUTTON_SHADOW_DROP_BLUR_PX, 0, 32);
    d->layout.spacing_px = constrain(doc["button_spacing_px"] | DEFAULT_BUTTON_SPACING_PX, 0, 64);
    d->layout.inset_top_px = constrain(doc["pad_inset_top_px"] | DEFAULT_PAD_INSET_TOP_PX, 0, 64);
    d->layout.inset_right_px = constrain(doc["pad_inset_right_px"] | DEFAULT_PAD_INSET_RIGHT_PX, 0, 64);
    d->layout.inset_bottom_px = constrain(doc["pad_inset_bottom_px"] | DEFAULT_PAD_INSET_BOTTOM_PX, 0, 64);
    d->layout.inset_left_px = constrain(doc["pad_inset_left_px"] | DEFAULT_PAD_INSET_LEFT_PX, 0, 64);
    d->layout.pixel_shift_distance_px = constrain(doc["pixel_shift_distance_px"] | DEFAULT_PIXEL_SHIFT_DISTANCE, 0, 8);
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
    if (!ensure_defaults()) return;
    bool loaded = load_from_flash(g_defaults);

    // If defaults were loaded, rebuild pad caches so they pick up the
    // saved defaults (pad_config_init runs first with empty defaults).
    if (loaded) {
        pad_config_rebuild_all_caches();
    }
}

const ButtonDefaults* button_defaults_get() {
    return ensure_defaults() ? g_defaults : &g_empty_defaults;
}

uint8_t button_defaults_get_pixel_shift_distance() {
    return button_defaults_get()->layout.pixel_shift_distance_px;
}

const char* button_defaults_validate(JsonObjectConst config) {
    if (config.containsKey("default_font")) {
        if (!config["default_font"].is<const char*>()) return "default_font must be @builtin or an uploaded font ID";
        const char* name = config["default_font"].as<const char*>();
        if (strcmp(name, "@builtin") != 0 && !font_store_validate_name(name))
            return "default_font must be @builtin or an uploaded font ID";
    }
    if (config.containsKey("base_text_size") && !config["base_text_size"].is<uint16_t>())
        return "base_text_size must be 0 (Automatic) or an integer from 1 to 65535";
    return nullptr;
}

void button_defaults_typography_status(JsonObject status) {
    const DeviceTypography& active = device_typography_get();
    const ButtonDefaults* saved = button_defaults_get();
    const char* active_font = active.font_name[0] ? active.font_name : "@builtin";
    status["active_font"] = active_font;
    status["active_base_text_size"] = active.base_size;
    status["board_medium_size"] = pad_font_pixel_size(pad_get_scale_info().font_medium);
    status["ttf_supported"] = bool(HAS_TTF_FONTS);
    status["reboot_required"] = strcmp(active_font, saved->default_font) != 0 || active.base_size != saved->base_text_size;
}

bool button_defaults_save_raw(const uint8_t* json, size_t len) {
    if (!json || len == 0 || len > 4096) return false;
    BasicJsonDocument<PsramJsonAllocator> document(2048);
    if (deserializeJson(document, json, len) || !document.is<JsonObject>() ||
        button_defaults_validate(document.as<JsonObjectConst>())) return false;
    if (!ensure_defaults()) return false;

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
    load_from_flash(g_defaults);
    storage_publish_usage(false);

    // Rebuild all pad caches so they pick up the new defaults
    pad_config_rebuild_all_caches();

    LOGI(TAG, "Saved (%u bytes)", (unsigned)len);
    return true;
}

#endif // HAS_DISPLAY
