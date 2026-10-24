#include "pad_config.h"
#include "board_config.h"
#include "fill_pattern.h"

#if HAS_DISPLAY

#include "action_list.h"
#include "action_parse.h"
#include "button_defaults.h"
#include "fs_health.h"
#include "icon_store.h"
#include "log_manager.h"
#include "pad_cycle.h"
#include "pad_validate.h"
#include "widgets/widget.h"

#include <ArduinoJson.h>
#include "psram_json_allocator.h"
#include "storage.h"

#include <esp_heap_caps.h>
#include <esp_partition.h>

#include <stdio.h>
#include <atomic>
#include <ctype.h>
#include <string.h>

#define TAG "PadCfg"

static bool g_fs_mounted = false;
static uint32_t g_generation = 0;
static std::atomic<uint32_t> g_eligible_mask{0};

// In-RAM cache so the LVGL render task (PSRAM stack) never touches flash.
// Flash reads require disabling cache, which is incompatible with PSRAM stacks.
static PadConfig* g_cache[MAX_PADS] = {};
static portMUX_TYPE g_cache_mux = portMUX_INITIALIZER_UNLOCKED;

static void cache_lock() {
    portENTER_CRITICAL(&g_cache_mux);
}

static void cache_unlock() {
    portEXIT_CRITICAL(&g_cache_mux);
}

static void publish_eligibility(uint8_t page, bool eligible) {
    uint32_t bit = (uint32_t)1U << page;
    if (eligible) {
        g_eligible_mask.fetch_or(bit, std::memory_order_release);
    } else {
        g_eligible_mask.fetch_and(~bit, std::memory_order_release);
    }
}

// Forward declaration — defined after pad_config_init().
static PadConfig* pad_config_load_from_flash(uint8_t page,
                                             bool skip_template = false);

// ============================================================================
// Helpers
// ============================================================================

static void pad_config_path(uint8_t page, char* buf, size_t buf_len) {
    snprintf(buf, buf_len, "/config/pad_%u.json", page);
}

static void recover_pad_config_file(uint8_t page) {
    char path[32], temporary[40], backup[40];
    pad_config_path(page, path, sizeof(path));
    snprintf(temporary, sizeof(temporary), "%s.tmp", path);
    snprintf(backup, sizeof(backup), "%s.bak", path);
    if (Storage.exists(temporary)) Storage.remove(temporary);
    if (!Storage.exists(backup)) return;
    if (Storage.exists(path)) Storage.remove(backup);
    else Storage.rename(backup, path);
}

static PadConfig* pad_config_create(uint8_t button_capacity, const char* name = "") {
    if (button_capacity > MAX_PAD_BUTTONS) return nullptr;

    const size_t name_size = strlen(name) + 1;
    const size_t size = sizeof(PadConfig) +
                        (size_t)button_capacity * sizeof(ScreenButtonConfig) + name_size;
    PadConfig* config = nullptr;
    if (psramFound()) {
        config = (PadConfig*)heap_caps_malloc(
            size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    if (!config) config = (PadConfig*)malloc(size);
    if (!config) return nullptr;

    memset(config, 0, size);
    config->ref_count = 1;
    config->button_capacity = button_capacity;
    config->buttons = (ScreenButtonConfig*)(config + 1);
    char* stored_name = (char*)(config->buttons + button_capacity);
    memcpy(stored_name, name, name_size);
    config->name = stored_name;
    return config;
}

static void pad_config_destroy(PadConfig* config) {
    for (uint8_t index = 0; index < config->button_count; ++index) {
        free(config->buttons[index].fill_style);
    }
    free(config);
}

static bool pad_config_resize(PadConfig** config_ptr, uint8_t button_capacity) {
    PadConfig* config = *config_ptr;
    if (!config || button_capacity < config->button_count ||
        button_capacity > MAX_PAD_BUTTONS) {
        return false;
    }
    if (button_capacity == config->button_capacity) return true;

    PadConfig* replacement = pad_config_create(button_capacity, config->name);
    if (!replacement) return false;
    const char* replacement_name = replacement->name;
    memcpy(replacement, config, sizeof(PadConfig));
    replacement->ref_count = 1;
    replacement->button_capacity = button_capacity;
    replacement->buttons = (ScreenButtonConfig*)(replacement + 1);
    replacement->name = replacement_name;
    memcpy(replacement->buttons, config->buttons,
           (size_t)config->button_count * sizeof(ScreenButtonConfig));
    config->button_count = 0;
    pad_config_destroy(config);
    *config_ptr = replacement;
    return true;
}

static uint16_t parse_track_size(JsonVariantConst value) {
    if (!value.is<uint32_t>()) return UINT16_MAX;
    const uint32_t size = value.as<uint32_t>();
    return size <= UINT16_MAX ? (uint16_t)size : UINT16_MAX;
}

static void cache_replace(uint8_t page, PadConfig* replacement) {
    cache_lock();
    PadConfig* old = g_cache[page];
    g_cache[page] = replacement;
    cache_unlock();
    if (old) pad_config_release(old);
}

static bool cache_uses_template(uint8_t page, uint8_t template_page) {
    cache_lock();
    const PadConfig* config = g_cache[page];
    const bool uses_template = config &&
                               config->template_pad == (int8_t)template_page;
    cache_unlock();
    return uses_template;
}

// ============================================================================
// Label Style DSL parser
// ============================================================================
// label_style_parse() lives in label_style.cpp (extracted so host-native tests
// can exercise the DSL tokenizer + binding-color capture without pulling in the
// full pad_config translation unit).

// Validate a pad binding name: [a-zA-Z][a-zA-Z0-9_]*, non-empty, max len.
static bool is_valid_binding_name(const char* name) {
    if (!name || !name[0]) return false;
    if (!((name[0] >= 'a' && name[0] <= 'z') || (name[0] >= 'A' && name[0] <= 'Z'))) return false;
    for (const char* p = name + 1; *p; p++) {
        char c = *p;
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_')) return false;
    }
    return strlen(name) < PAD_BINDING_NAME_MAX_LEN;
}

static void init_button_defaults(ScreenButtonConfig* btn) {
    memset(btn, 0, sizeof(ScreenButtonConfig));
    btn->col_span = 1;
    btn->row_span = 1;
    strlcpy(btn->bg_color, DEFAULT_BUTTON_BG_COLOR, CONFIG_COLOR_MAX_LEN);
    strlcpy(btn->fg_color, DEFAULT_BUTTON_FG_COLOR, CONFIG_COLOR_MAX_LEN);
    strlcpy(btn->border_color, DEFAULT_BUTTON_BORDER_COLOR, CONFIG_COLOR_MAX_LEN);
    strlcpy(btn->border_width, DEFAULT_BUTTON_BORDER_WIDTH, CONFIG_BINDABLE_SHORT_LEN);
    strlcpy(btn->corner_radius, DEFAULT_BUTTON_CORNER_RADIUS, CONFIG_BINDABLE_SHORT_LEN);
    strlcpy(btn->content_pad, DEFAULT_BUTTON_CONTENT_PAD, CONFIG_BINDABLE_SHORT_LEN);
}

static void parse_ui_offset_field(JsonVariant v, int16_t* out_x, int16_t* out_y) {
    *out_x = 0;
    *out_y = 0;
    if (v.isNull() || !v.is<const char*>()) return;

    const char* raw = v.as<const char*>();
    if (!raw || !raw[0]) return;

    char buf[32];
    strlcpy(buf, raw, sizeof(buf));

    char* semi = strchr(buf, ';');
    char* x_part = buf;
    char* y_part = nullptr;
    if (semi) {
        *semi = '\0';
        y_part = semi + 1;
    }

    auto parse_int_part = [](char* s, int16_t* out_val) {
        if (!s) return;
        while (*s && isspace((unsigned char)*s)) s++;
        if (!*s) return;
        char* end = nullptr;
        long val = strtol(s, &end, 10);
        if (end == s) return;
        *out_val = (int16_t)val;
    };

    parse_int_part(x_part, out_x);
    parse_int_part(y_part, out_y);
}

// Parse a bindable field from JSON into a string.
// For colors (is_color=true): integers → "#RRGGBB", strings stored as-is.
// For numbers (is_color=false): integers → decimal string, strings stored as-is.
static void parse_bindable_field(JsonVariant v, char* out, size_t out_len, const char* default_str, bool is_color = true) {
    if (v.isNull()) {
        strlcpy(out, default_str, out_len);
        return;
    }
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
        if (s && s[0]) {
            strlcpy(out, s, out_len);
            return;
        }
    }
    strlcpy(out, default_str, out_len);
}

// Parse a typed action object, with legacy flat key fallback for old pad configs.
static void parse_pad_action(JsonVariant v, ButtonAction* act, const char* legacy_screen_key, JsonObject obj) {
    memset(act, 0, sizeof(ButtonAction));

    if (v.is<JsonObject>()) {
#if HAS_DISPLAY
        action_parse(v.as<JsonObject>(), *act);
#endif
        return;
    }

    // Legacy flat key fallback: "action_screen" / "lp_action_screen" → type=screen
    if (legacy_screen_key) {
        const char* screen = obj[legacy_screen_key] | "";
        if (screen[0]) {
            strlcpy(act->type, ACTION_TYPE_SCREEN, CONFIG_ACTION_TYPE_MAX_LEN);
            strlcpy(act->payload.screen.screen_id, screen, CONFIG_SCREEN_ID_MAX_LEN);
        }
    }
}

// Helper: resolve a fallback default string from ButtonDefaults (if non-empty),
// otherwise use the hardcoded default.
static const char* btn_default(const char* pad_default, const char* hardcoded) {
    return (pad_default && pad_default[0]) ? pad_default : hardcoded;
}

static ButtonShadowMode parse_button_shadow_mode(JsonVariant v) {
    const char* value = v | "";
    if (strcmp(value, "enabled") == 0) return BUTTON_SHADOW_ENABLED;
    if (strcmp(value, "disabled") == 0) return BUTTON_SHADOW_DISABLED;
    return BUTTON_SHADOW_INHERIT;
}

static bool parse_button_fill_style(JsonObject obj, ScreenButtonConfig* btn) {
    const char* pattern_fields[] = {"bg_pattern", "widget_bar_pattern", "widget_bar_pattern_2",
        "widget_bar_pattern_3", "widget_bar_pattern_4", "widget_bar_bg_pattern"};
    const char* color_fields[] = {"bg_pattern_color", "widget_bar_pattern_color", "widget_bar_pattern_color_2",
        "widget_bar_pattern_color_3", "widget_bar_pattern_color_4", "widget_bar_bg_pattern_color",
        "widget_track_outline_color"};
    bool configured = obj.containsKey("pattern_spacing") || obj.containsKey("pattern_stroke_width") ||
                      obj.containsKey("widget_track_outline_width") || obj.containsKey("widget_bar_track_radius");
    for (unsigned index = 0; index < 7; ++index) configured |= obj.containsKey(color_fields[index]);
    for (unsigned index = 0; index < 6; ++index) configured |= obj.containsKey(pattern_fields[index]);
    if (!configured) return true;

    ButtonFillStyle header{};
    header.spacing = fill_geometry_clamp(obj["pattern_spacing"] | 8, 2, 64);
    header.stroke = fill_geometry_clamp(obj["pattern_stroke_width"] | 1, 1,
                                       header.spacing < 8 ? header.spacing : 8);
    header.outline_width = fill_geometry_clamp(obj["widget_track_outline_width"] | 0, 0, 8);
    header.track_radius = fill_geometry_clamp(obj["widget_bar_track_radius"] | 4, 0, 64);
    char colors[7][CONFIG_COLOR_MAX_LEN];
    size_t bytes = sizeof(header);
    for (unsigned index = 0; index < 7; ++index) {
        if (index < 6) header.patterns[index] = fill_pattern_parse(obj[pattern_fields[index]] | "none");
        parse_bindable_field(obj[color_fields[index]], colors[index],
                             index < 5 ? CONFIG_COLOR_MAX_LEN : CONFIG_BINDABLE_SHORT_LEN, "#000000");
        header.color_offsets[index] = bytes;
        bytes += strlen(colors[index]) + 1;
    }
    header.bytes = bytes;
    ButtonFillStyle* style = nullptr;
    if (psramFound()) style = static_cast<ButtonFillStyle*>(heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!style) style = static_cast<ButtonFillStyle*>(malloc(bytes));
    if (!style) return false;
    memcpy(style, &header, sizeof(header));
    for (unsigned index = 0; index < 7; ++index) {
        memcpy(reinterpret_cast<char*>(style) + header.color_offsets[index], colors[index], strlen(colors[index]) + 1);
    }
    btn->fill_style = style;
    return true;
}

static bool parse_button(JsonObject obj, ScreenButtonConfig* btn, const ButtonDefaults* defs) {
    init_button_defaults(btn);

    btn->col = obj["col"] | (uint8_t)0;
    btn->row = obj["row"] | (uint8_t)0;
    btn->col_span = obj["col_span"] | (uint8_t)1;
    btn->row_span = obj["row_span"] | (uint8_t)1;

    if (btn->col_span < 1) btn->col_span = 1;
    if (btn->row_span < 1) btn->row_span = 1;

    strlcpy(btn->label_top, obj["label_top"] | "", CONFIG_LABEL_MAX_LEN);
    strlcpy(btn->label_center, obj["label_center"] | "", CONFIG_LABEL_MAX_LEN);
    strlcpy(btn->label_bottom, obj["label_bottom"] | "", CONFIG_LABEL_MAX_LEN);

    // Per-label style overrides: button JSON → device button_defaults → empty (auto)
    const char* dsl_top = obj["label_top_style"] | "";
    const char* dsl_ctr = obj["label_center_style"] | "";
    const char* dsl_bot = obj["label_bottom_style"] | "";
    if (!dsl_top[0] && defs) dsl_top = defs->label_top_style;
    if (!dsl_ctr[0] && defs) dsl_ctr = defs->label_center_style;
    if (!dsl_bot[0] && defs) dsl_bot = defs->label_bottom_style;
    label_style_parse(dsl_top, &btn->style_top,
                      btn->label_top_color_bind, sizeof(btn->label_top_color_bind));
    label_style_parse(dsl_ctr, &btn->style_center,
                      btn->label_center_color_bind, sizeof(btn->label_center_color_bind));
    label_style_parse(dsl_bot, &btn->style_bottom,
                      btn->label_bottom_color_bind, sizeof(btn->label_bottom_color_bind));

    JsonObjectConst icon = obj["icon"].as<JsonObjectConst>();
    if (!icon.isNull()) {
        strlcpy(btn->icon_source_id, icon["source_id"] | "",
                CONFIG_ICON_SOURCE_ID_MAX_LEN);
        btn->icon_kind = strcmp(icon["kind"] | "", "mono") == 0
            ? ICON_KIND_MONO : ICON_KIND_COLOR;
    }
    btn->icon_scale_pct = obj["icon_scale_pct"] | (uint8_t)0;
    {
        const char* ip = obj["icon_position"] | "";
        if (ip[0] == 'l')      btn->icon_position = ICON_POS_LEFT;
        else if (ip[0] == 'c') btn->icon_position = ICON_POS_CENTER;
        else if (ip[0] == 'a') btn->icon_position = ICON_POS_ABOVE;
        else                    btn->icon_position = defs ? defs->icon_position : ICON_POS_ABOVE;
    }
    parse_ui_offset_field(obj["ui_offset"], &btn->ui_offset_x, &btn->ui_offset_y);

    // Appearance fields: button JSON → device button_defaults → hardcoded default
    parse_bindable_field(obj["bg_color"], btn->bg_color, CONFIG_COLOR_MAX_LEN,
                         btn_default(defs ? defs->bg_color : nullptr, DEFAULT_BUTTON_BG_COLOR));
    parse_bindable_field(obj["fg_color"], btn->fg_color, CONFIG_COLOR_MAX_LEN,
                         btn_default(defs ? defs->fg_color : nullptr, DEFAULT_BUTTON_FG_COLOR));
    parse_bindable_field(obj["border_color"], btn->border_color, CONFIG_COLOR_MAX_LEN,
                         btn_default(defs ? defs->border_color : nullptr, DEFAULT_BUTTON_BORDER_COLOR));
    parse_bindable_field(obj["border_width"], btn->border_width, CONFIG_BINDABLE_SHORT_LEN,
                         btn_default(defs ? defs->border_width : nullptr, DEFAULT_BUTTON_BORDER_WIDTH), false);
    parse_bindable_field(obj["corner_radius"], btn->corner_radius, CONFIG_BINDABLE_SHORT_LEN,
                         btn_default(defs ? defs->corner_radius : nullptr, DEFAULT_BUTTON_CORNER_RADIUS), false);
    parse_bindable_field(obj["content_pad"], btn->content_pad, CONFIG_BINDABLE_SHORT_LEN,
                         btn_default(defs ? defs->content_pad : nullptr, DEFAULT_BUTTON_CONTENT_PAD), false);
    btn->button_shadow = parse_button_shadow_mode(obj["button_shadow"]);

    // Typed actions — array of up to MAX_BUTTON_ACTIONS sequential actions per gesture.
    // JSON: "actions": [ { "type": "mqtt", ... }, { "type": "sound_alert", ... } ]
    btn->action_count = 0;
    btn->lp_action_count = 0;
    memset(btn->actions, 0, sizeof(btn->actions));
    memset(btn->lp_actions, 0, sizeof(btn->lp_actions));

    {
        JsonVariant v = obj["actions"];
        if (v.is<JsonArray>()) {
            JsonArray arr = v.as<JsonArray>();
            for (size_t i = 0; i < arr.size() && btn->action_count < MAX_BUTTON_ACTIONS; i++) {
                parse_pad_action(arr[i], &btn->actions[btn->action_count], nullptr, obj);
                if (btn->actions[btn->action_count].type[0]) btn->action_count++;
            }
        } else {
            // Legacy: try singular "action" key (old single-object format)
            JsonVariant legacy = v.isNull() ? obj["action"].as<JsonVariant>() : v;
            parse_pad_action(legacy, &btn->actions[0], "action_screen", obj);
            if (btn->actions[0].type[0]) btn->action_count = 1;
        }
    }
    {
        JsonVariant v = obj["lp_actions"];
        if (v.is<JsonArray>()) {
            JsonArray arr = v.as<JsonArray>();
            for (size_t i = 0; i < arr.size() && btn->lp_action_count < MAX_BUTTON_ACTIONS; i++) {
                parse_pad_action(arr[i], &btn->lp_actions[btn->lp_action_count], nullptr, obj);
                if (btn->lp_actions[btn->lp_action_count].type[0]) btn->lp_action_count++;
            }
        } else {
            // Legacy: try singular "lp_action" key (old single-object format)
            JsonVariant legacy = v.isNull() ? obj["lp_action"].as<JsonVariant>() : v;
            parse_pad_action(legacy, &btn->lp_actions[0], "lp_action_screen", obj);
            if (btn->lp_actions[0].type[0]) btn->lp_action_count = 1;
        }
    }


    // Background image fields
    strlcpy(btn->bg_image_url, obj["bg_image_url"] | "", CONFIG_BG_IMAGE_URL_MAX_LEN);
    strlcpy(btn->bg_image_path, obj["bg_image_path"] | "", CONFIG_BG_IMAGE_PATH_MAX_LEN);
    strlcpy(btn->bg_image_user, obj["bg_image_user"] | "", CONFIG_BG_IMAGE_USER_MAX_LEN);
    strlcpy(btn->bg_image_password, obj["bg_image_password"] | "", CONFIG_BG_IMAGE_PASS_MAX_LEN);
    btn->bg_image_interval_ms = obj["bg_image_interval_ms"] | (uint32_t)0;
    btn->bg_image_letterbox = obj["bg_image_letterbox"] | false;

    // Widget type (bar_chart, gauge, etc.)
    const char* wtype = obj["widget_type"] | "";
    strlcpy(btn->widget.type, wtype, CONFIG_WIDGET_TYPE_MAX_LEN);
    btn->widget.disable_pad_swipes = obj["widget_disable_pad_swipes"] | false;
    // Data bindings: data_binding[0] from "widget_data_binding",
    // data_binding[1..3] from "widget_data_binding_2..4"
    strlcpy(btn->widget.data_binding[0], obj["widget_data_binding"] | "", CONFIG_LABEL_MAX_LEN);
    strlcpy(btn->widget.data_binding[1], obj["widget_data_binding_2"] | "", CONFIG_LABEL_MAX_LEN);
    strlcpy(btn->widget.data_binding[2], obj["widget_data_binding_3"] | "", CONFIG_LABEL_MAX_LEN);
    strlcpy(btn->widget.data_binding[3], obj["widget_data_binding_4"] | "", CONFIG_LABEL_MAX_LEN);
    memset(btn->widget.data, 0, WIDGET_CONFIG_MAX_BYTES);
#if HAS_DISPLAY
    if (wtype[0]) {
        const WidgetType* wt = widget_find(wtype);
        if (wt && wt->parseConfig) {
            wt->parseConfig(obj, btn->widget.data);
        }
    }
#endif

    // Button state (tri-state: "enabled", "disabled", "hidden"; empty = enabled)
    strlcpy(btn->btn_state, obj["btn_state"] | "", CONFIG_BTN_STATE_MAX_LEN);
    return parse_button_fill_style(obj, btn);
}

// ============================================================================
// Public API
// ============================================================================

bool pad_config_init() {
    if (g_fs_mounted) return true;

    if (!storage_mount()) {
        LOGW(TAG, "Storage mount failed — pad configs will not persist");
        return false;
    }
    g_fs_mounted = true;
    g_eligible_mask.store(0, std::memory_order_release);

    for (uint8_t page = 0; page < MAX_PADS; ++page) recover_pad_config_file(page);

    // Pre-load all existing page configs into RAM cache.
    // This runs on the main task (internal stack) so flash access is safe.
    bool any_loaded = false;
    for (uint8_t i = 0; i < MAX_PADS; i++) {
        char path[32];
        pad_config_path(i, path, sizeof(path));
        if (Storage.exists(path)) {
            PadConfig* cfg = pad_config_load_from_flash(i);
            if (cfg) {
                g_cache[i] = cfg;
                publish_eligibility(i, cfg->button_count > 0 ||
                                       cfg->pad_action_count > 0);
                any_loaded = true;
                LOGT(TAG, "Cached pad %u", i);
            }
        }
    }

    // Bump generation so subsystems that depend on pad configs (data stream
    // registry, pad screens) detect that configs are now available.  The LVGL
    // task may have already run data_stream_rebuild() before LittleFS was
    // mounted and found zero configs — this ensures it re-scans.
    if (any_loaded) {
        g_generation++;
        LOGI(TAG, "Configs cached, gen=%u", g_generation);
    }

    return true;
}

// Internal: read and parse page config from flash. Only call from a task
// with an internal-RAM stack (main task, web server tasks). Never from the
// LVGL render task whose stack may live in PSRAM.

static int select_template_buttons(const PadConfig& out, const PadConfig& tpl,
                                   uint8_t* selected) {
    bool occupied[MAX_GRID_COLS][MAX_GRID_ROWS] = {};
    for (uint8_t i = 0; i < out.main_button_count; i++) {
        const ScreenButtonConfig& b = out.buttons[i];
        uint8_t cs = b.col_span ? b.col_span : 1;
        uint8_t rs = b.row_span ? b.row_span : 1;
        for (uint8_t dc = 0; dc < cs; dc++) {
            for (uint8_t dr = 0; dr < rs; dr++) {
                uint8_t c = b.col + dc, r = b.row + dr;
                if (c < MAX_GRID_COLS && r < MAX_GRID_ROWS)
                    occupied[c][r] = true;
            }
        }
    }

    uint8_t merged = 0;
    for (uint8_t i = 0; i < tpl.main_button_count; i++) {
        const ScreenButtonConfig& tb = tpl.buttons[i];
        // Skip template buttons outside target grid
        if (tb.col >= out.cols || tb.row >= out.rows) continue;
        // Check all cells the template button would occupy
        uint8_t cs = tb.col_span ? tb.col_span : 1;
        uint8_t rs = tb.row_span ? tb.row_span : 1;
        bool conflict = false;
        for (uint8_t dc = 0; dc < cs && !conflict; dc++) {
            for (uint8_t dr = 0; dr < rs && !conflict; dr++) {
                uint8_t c = tb.col + dc, r = tb.row + dr;
                if (c >= out.cols || r >= out.rows || occupied[c][r])
                    conflict = true;
            }
        }
        if (conflict) continue;
        if (out.button_count + merged >= MAX_PAD_BUTTONS) return -1;
        if (selected) selected[merged] = i;
        merged++;
        for (uint8_t dc = 0; dc < cs; dc++) {
            for (uint8_t dr = 0; dr < rs; dr++) {
                uint8_t c = tb.col + dc, r = tb.row + dr;
                if (c < MAX_GRID_COLS && r < MAX_GRID_ROWS)
                    occupied[c][r] = true;
            }
        }
    }
    return merged;
}

// Merge buttons from a template pad into empty grid positions.
// Target pad's own buttons always win on col/row conflict.
static bool merge_template_buttons(uint8_t page, PadConfig** out_ptr) {
    PadConfig* out = *out_ptr;
    const int8_t tpl = out->template_pad;
    if (tpl < 0 || tpl >= MAX_PADS || tpl == (int8_t)page) return true;

    PadConfig* tpl_cfg = pad_config_load_from_flash((uint8_t)tpl, true);
    if (!tpl_cfg) return true;
    uint8_t selected[MAX_PAD_BUTTONS];
    const int merged = select_template_buttons(*out, *tpl_cfg, selected);
    if (merged < 0 || !pad_config_resize(out_ptr, out->button_count + merged)) {
        LOGW(TAG, "Pad %u: template merge exceeds capacity or allocation failed", page);
        pad_config_release(tpl_cfg);
        return false;
    }
    out = *out_ptr;
    const uint8_t insertion = out->main_button_count;
    const uint8_t overlay_count = out->button_count - insertion;
    if (merged) {
        memmove(&out->buttons[insertion + merged], &out->buttons[insertion],
                (size_t)overlay_count * sizeof(ScreenButtonConfig));
        for (int index = 0; index < merged; ++index) {
            ScreenButtonConfig& inherited = tpl_cfg->buttons[selected[index]];
            memcpy(&out->buttons[insertion + index], &inherited, sizeof(ScreenButtonConfig));
            inherited.fill_style = nullptr;
        }
        out->main_button_count += merged;
        out->button_count += merged;
    }

    // Merge template bindings (target wins on name collision)
    for (uint8_t i = 0; i < tpl_cfg->binding_count; i++) {
        if (out->binding_count >= PAD_MAX_BINDINGS) break;
        const PadBinding& tb = tpl_cfg->bindings[i];
        bool exists = false;
        for (uint8_t j = 0; j < out->binding_count; j++) {
            if (strcmp(out->bindings[j].name, tb.name) == 0) {
                exists = true;
                break;
            }
        }
        if (!exists) {
            memcpy(&out->bindings[out->binding_count], &tb, sizeof(PadBinding));
            out->binding_count++;
        }
    }

    if (merged > 0) {
        LOGT(TAG, "Pad %u: merged %u buttons from template pad %d", page, merged, tpl);
    }

    pad_config_release(tpl_cfg);
    return true;
}

static PadConfig* pad_config_parse(uint8_t page, JsonDocument& doc) {
    (void)page;
    JsonArray buttons = doc["buttons"].as<JsonArray>();
    JsonArray overlay_buttons = doc["overlay_buttons"].as<JsonArray>();
    if ((doc.containsKey("buttons") && buttons.isNull()) ||
        (doc.containsKey("overlay_buttons") && overlay_buttons.isNull()) ||
        buttons.size() + overlay_buttons.size() > MAX_PAD_BUTTONS) {
        LOGW(TAG, "Pad %u: invalid button collections or combined capacity", page);
        return nullptr;
    }
    const uint8_t button_capacity = (uint8_t)(buttons.size() + overlay_buttons.size());
    PadConfig* out = pad_config_create(button_capacity, doc["name"] | "");
    if (!out) {
        LOGE(TAG, "Pad %u: OOM for %u buttons", page, button_capacity);
        return nullptr;
    }
    strlcpy(out->layout, "grid", CONFIG_LAYOUT_NAME_MAX_LEN);
    out->cols = 3;
    out->rows = 3;
    out->template_pad = -1;
    out->button_shadow = BUTTON_SHADOW_INHERIT;
    out->shadow = button_defaults_get()->shadow;
    out->layout_settings = button_defaults_get()->layout;

    strlcpy(out->layout, doc["layout"] | "grid", CONFIG_LAYOUT_NAME_MAX_LEN);
    out->cols = doc["cols"] | (uint8_t)3;
    out->rows = doc["rows"] | (uint8_t)3;
    JsonArrayConst column_widths = doc["column_widths_px"].as<JsonArrayConst>();
    for (uint8_t i = 0; i < column_widths.size() && i < MAX_GRID_COLS; i++)
        out->column_widths_px[i] = parse_track_size(column_widths[i]);
    JsonArrayConst row_heights = doc["row_heights_px"].as<JsonArrayConst>();
    for (uint8_t i = 0; i < row_heights.size() && i < MAX_GRID_ROWS; i++)
        out->row_heights_px[i] = parse_track_size(row_heights[i]);
    strlcpy(out->wake_screen, doc["wake_screen"] | "", CONFIG_SCREEN_ID_MAX_LEN);
    parse_bindable_field(doc["bg_color"], out->bg_color, CONFIG_COLOR_MAX_LEN,
                         btn_default(button_defaults_get()->default_pad_bg_color, DEFAULT_PAD_BG_COLOR));
    out->button_shadow = parse_button_shadow_mode(doc["button_shadow"]);
    out->template_pad = doc["template_pad"] | (int8_t)-1;
    if (out->template_pad >= MAX_PADS) out->template_pad = -1;

    // Parse named page-level bindings: { "bindings": { "name": "template", ... } }
    out->binding_count = 0;
    JsonObject bindings_obj = doc["bindings"];
    if (!bindings_obj.isNull()) {
        for (JsonPair kv : bindings_obj) {
            if (out->binding_count >= PAD_MAX_BINDINGS) {
                LOGW(TAG, "Pad %u: max %d bindings reached, skipping rest", page, PAD_MAX_BINDINGS);
                break;
            }
            const char* name = kv.key().c_str();
            const char* value = kv.value().as<const char*>();
            if (!name || !value) continue;
            if (!is_valid_binding_name(name)) {
                LOGW(TAG, "Pad %u: skipping invalid binding name '%s'", page, name);
                continue;
            }
            PadBinding& b = out->bindings[out->binding_count];
            strlcpy(b.name, name, PAD_BINDING_NAME_MAX_LEN);
            strlcpy(b.value, value, CONFIG_LABEL_MAX_LEN);
            out->binding_count++;
        }
        if (out->binding_count > 0) {
            LOGT(TAG, "Pad %u: %u named bindings loaded", page, out->binding_count);
        }
    }

    if (out->cols < 1) out->cols = 1;
    if (out->cols > MAX_GRID_COLS) out->cols = MAX_GRID_COLS;
    if (out->rows < 1) out->rows = 1;
    if (out->rows > MAX_GRID_ROWS) out->rows = MAX_GRID_ROWS;

    // Full-screen actions are deliberately parsed before template buttons are
    // merged so template_pad contributes only buttons and named bindings.
    out->pad_action_count = action_list_parse(doc["pad_actions"], out->pad_actions,
                                              MAX_BUTTON_ACTIONS, true);

    // Use device-level pad and button defaults for cascading to buttons
#if HAS_DISPLAY
    const ButtonDefaults* defs = button_defaults_get();
#else
    const ButtonDefaults* defs = nullptr;
#endif

    out->button_count = 0;
    for (JsonObject btn_obj : buttons) {
        if (!parse_button(btn_obj, &out->buttons[out->button_count], defs)) {
            LOGE(TAG, "Pad %u: OOM loading button styling", page);
            pad_config_destroy(out);
            return nullptr;
        }
        out->button_count++;
    }
    out->main_button_count = out->button_count;
    for (JsonObject btn_obj : overlay_buttons) {
        if (!parse_button(btn_obj, &out->buttons[out->button_count], defs)) {
            LOGE(TAG, "Pad %u: OOM loading button styling", page);
            pad_config_destroy(out);
            return nullptr;
        }
        out->button_count++;
    }

    LOGT(TAG, "Pad %u loaded: layout=%s cols=%u rows=%u buttons=%u",
         page, out->layout, out->cols, out->rows, out->button_count);
    return out;
}

static PadConfig* pad_config_load_from_flash(uint8_t page, bool skip_template) {
    if (page >= MAX_PADS || !g_fs_mounted) return nullptr;
    char path[32];
    pad_config_path(page, path, sizeof(path));
    File file = Storage.open(path, "r");
    if (!file) return nullptr;
    const size_t size = file.size();
    if (size == 0 || size > 64 * 1024) return nullptr;
    char* buffer = psramFound()
        ? static_cast<char*>(heap_caps_malloc(size + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT))
        : nullptr;
    if (!buffer) buffer = static_cast<char*>(malloc(size + 1));
    if (!buffer) return nullptr;
    const size_t read = file.readBytes(buffer, size);
    file.close();
    buffer[read] = '\0';
    BasicJsonDocument<PsramJsonAllocator> doc(size * 2 + 256);
    const DeserializationError error = deserializeJson(doc, buffer, read);
    free(buffer);
    if (error) {
        LOGE(TAG, "Pad %u: JSON parse error: %s", page, error.c_str());
        return nullptr;
    }
    PadConfig* config = pad_config_parse(page, doc);
    if (config && !skip_template && !merge_template_buttons(page, &config)) {
        pad_config_destroy(config);
        return nullptr;
    }
    return config;
}

// Update the in-RAM cache for a page (parse from flash).
// Call from a task with internal-RAM stack (web server, main task).
static bool cache_update(uint8_t page) {
    PadConfig* replacement = pad_config_load_from_flash(page);
    if (!replacement) {
        LOGE(TAG, "Pad %u: cache allocation failed", page);
        return false;
    }
    cache_replace(page, replacement);
    publish_eligibility(page, replacement->button_count > 0 ||
                               replacement->pad_action_count > 0);
    return true;
}

void pad_config_rebuild_all_caches() {
    if (!g_fs_mounted) return;
    bool refresh_ok = true;
    for (uint8_t i = 0; i < MAX_PADS; i++) {
        if (!cache_update(i)) refresh_ok = false;
    }
    g_generation++;
    if (refresh_ok) {
        LOGI(TAG, "Rebuilt all pad caches");
    } else {
        LOGE(TAG, "Pad cache rebuild incomplete; retained previous entries");
    }
}

const PadConfig* pad_config_acquire(uint8_t page) {
    if (page >= MAX_PADS) return nullptr;

    cache_lock();
    PadConfig* config = g_cache[page];
    if (config) config->ref_count++;
    cache_unlock();
    return config;
}

void pad_config_release(const PadConfig* config) {
    if (!config) return;

    PadConfig* mutable_config = const_cast<PadConfig*>(config);
    bool destroy = false;
    cache_lock();
    if (mutable_config->ref_count > 0) {
        mutable_config->ref_count--;
        destroy = mutable_config->ref_count == 0;
    }
    cache_unlock();
    if (destroy) pad_config_destroy(mutable_config);
}

bool pad_config_get_data_stream_snapshot(uint8_t page,
                                         PadDataStreamSnapshot* out) {
    if (!out || page >= MAX_PADS) return false;

    const PadConfig* config = pad_config_acquire(page);
    if (!config) return false;

    out->binding_count = config->binding_count;
    memcpy(out->bindings, config->bindings, sizeof(out->bindings));
    out->button_count = config->button_count;
    for (uint8_t button = 0; button < out->button_count; button++) {
        out->widgets[button] = config->buttons[button].widget;
    }
    pad_config_release(config);
    return true;
}

static const char* validate_pad_button_icon(JsonObjectConst pad, JsonObjectConst button,
                                           uint8_t page, const char* layer) {
    const int template_page = pad["template_pad"] | -1;
    JsonVariantConst icon_value = button["icon"];
    if (icon_value.isNull()) return nullptr;
    if (!icon_value.is<JsonObjectConst>()) return "icon must be an object";
    JsonObjectConst icon = icon_value.as<JsonObjectConst>();
    const char* source_id = icon["source_id"] | "";
    const char* catalog_id = icon.containsKey("catalog_id")
        ? icon["catalog_id"].as<const char*>() : nullptr;
    const char* kind_name = icon["kind"] | "";
    if (!icon_store_source_matches_metadata(source_id, catalog_id, kind_name))
        return "icon source is missing or does not match kind and optional catalog_id";
    const uint8_t col = button["col"] | (uint8_t)0;
    const uint8_t row = button["row"] | (uint8_t)0;
    const uint8_t col_span = button["col_span"] | (uint8_t)1;
    const uint8_t row_span = button["row_span"] | (uint8_t)1;
    if ((uint16_t)col + col_span > (uint8_t)(pad["cols"] | (uint8_t)3) ||
        (uint16_t)row + row_span > (uint8_t)(pad["rows"] | (uint8_t)3)) return nullptr;
    const IconKind kind = strcmp(kind_name, "mono") == 0
        ? ICON_KIND_MONO : ICON_KIND_COLOR;
    const uint32_t render_key = pad_icon_render_key(pad, button);
    char key[CONFIG_ICON_KEY_MAX_LEN];
    icon_store_build_key(page, layer, col, row, key, sizeof(key));
    if (icon_store_artifact_matches(key, source_id, kind, render_key)) return nullptr;
    if (strcmp(layer, "main") == 0 && template_page >= 0 && template_page < MAX_PADS) {
        icon_store_build_key((uint8_t)template_page, "main", col, row, key, sizeof(key));
        if (icon_store_artifact_matches(key, source_id, kind, render_key)) return nullptr;
    }
    return "icon PNG is not prepared for its source, position, and layout; save through the portal";
}

static const char* validate_inherited_icon_artifacts(JsonObjectConst pad, uint8_t page,
                                                    const PadConfig& config,
                                                    JsonObjectConst template_pad,
                                                    const PadConfig& template_config) {
    uint8_t selected[MAX_PAD_BUTTONS];
    const int count = select_template_buttons(config, template_config, selected);
    if (count < 0) return "template buttons exceed capacity";
    JsonArrayConst buttons = template_pad["buttons"].as<JsonArrayConst>();
    for (int index = 0; index < count; ++index) {
        const char* error = validate_pad_button_icon(pad, buttons[selected[index]], page, "main");
        if (error) return error;
    }
    return nullptr;
}

static uint8_t collect_icon_sources(const PadConfig* config,
                                    char source_ids[MAX_PAD_BUTTONS][CONFIG_ICON_SOURCE_ID_MAX_LEN]) {
    if (!config) return 0;
    uint8_t count = 0;
    for (uint8_t index = 0; index < config->button_count; ++index) {
        const char* source_id = config->buttons[index].icon_source_id;
        if (!source_id[0]) continue;
        bool duplicate = false;
        for (uint8_t found = 0; found < count; ++found) {
            if (strcmp(source_ids[found], source_id) == 0) duplicate = true;
        }
        if (!duplicate && count < MAX_PAD_BUTTONS) {
            strlcpy(source_ids[count++], source_id, CONFIG_ICON_SOURCE_ID_MAX_LEN);
        }
    }
    return count;
}

bool pad_config_save_raw(uint8_t page, const uint8_t* json, size_t len,
                         const char** error) {
    if (error) *error = nullptr;
    if (page >= MAX_PADS) return false;
    if (!g_fs_mounted) return false;
    if (!json || len == 0 || len > 64 * 1024) return false;

    BasicJsonDocument<PsramJsonAllocator> doc(len * 2 + 256);
    if (deserializeJson(doc, json, len)) return false;
    PadConfig* replacement = pad_config_parse(page, doc);
    if (!replacement) return false;
    auto reject_icon_error = [&](const char* icon_error) {
        if (error) *error = icon_error;
        pad_config_release(replacement);
        return false;
    };
    for (const char* layer : {"main", "overlay"}) {
        JsonArrayConst buttons = doc[strcmp(layer, "main") == 0 ? "buttons" : "overlay_buttons"];
        for (JsonObjectConst button : buttons) {
            const char* icon_error = validate_pad_button_icon(doc.as<JsonObjectConst>(), button, page, layer);
            if (icon_error) return reject_icon_error(icon_error);
        }
    }
    if (replacement->template_pad >= 0 && replacement->template_pad != page) {
        PadConfig* template_config = pad_config_load_from_flash(replacement->template_pad, true);
        if (template_config) {
            size_t template_length = 0;
            char* template_json = pad_config_read_raw(replacement->template_pad, &template_length);
            BasicJsonDocument<PsramJsonAllocator> template_doc(template_length * 2 + 256);
            const bool loaded = template_json && !deserializeJson(template_doc, template_json, template_length);
            free(template_json);
            const char* icon_error = loaded
                ? validate_inherited_icon_artifacts(doc.as<JsonObjectConst>(), page, *replacement,
                    template_doc.as<JsonObjectConst>(), *template_config)
                : "could not validate template icons";
            pad_config_release(template_config);
            if (icon_error) return reject_icon_error(icon_error);
        }
    }
    for (uint8_t dependent_page = 0; dependent_page < MAX_PADS; ++dependent_page) {
        if (dependent_page == page || !cache_uses_template(dependent_page, page)) continue;
        PadConfig* dependent = pad_config_load_from_flash(dependent_page, true);
        size_t dependent_length = 0;
        char* dependent_json = dependent ? pad_config_read_raw(dependent_page, &dependent_length) : nullptr;
        BasicJsonDocument<PsramJsonAllocator> dependent_doc(dependent_length * 2 + 256);
        const bool loaded = dependent_json && !deserializeJson(dependent_doc, dependent_json, dependent_length);
        free(dependent_json);
        const char* icon_error = loaded
            ? validate_inherited_icon_artifacts(dependent_doc.as<JsonObjectConst>(), dependent_page,
                *dependent, doc.as<JsonObjectConst>(), *replacement)
            : "could not validate dependent pad icons";
        pad_config_release(dependent);
        if (icon_error) return reject_icon_error(icon_error);
    }
    if (!merge_template_buttons(page, &replacement)) {
        pad_config_release(replacement);
        return false;
    }

    const PadConfig* previous = pad_config_acquire(page);
    char previous_sources[MAX_PAD_BUTTONS][CONFIG_ICON_SOURCE_ID_MAX_LEN] = {};
    const uint8_t previous_source_count = collect_icon_sources(previous, previous_sources);

    char path[32];
    pad_config_path(page, path, sizeof(path));
    if (!storage_write_atomic(path, json, len)) {
        LOGE(TAG, "Pad %u: atomic JSON write failed", page);
        pad_config_release(replacement);
        pad_config_release(previous);
        return false;
    }

    // Update RAM cache BEFORE bumping generation so the LVGL task
    // always reads fresh data when it detects the new generation.
    cache_replace(page, replacement);
    publish_eligibility(page, replacement->button_count > 0 || replacement->pad_action_count > 0);

    // Preload icons for this pad (picks up template button icons that
    // aren't yet in the PSRAM icon cache, e.g. after template_pad change).
#if HAS_DISPLAY
    icon_store_preload_pad(page);
#endif

    // Also refresh any pad that references this page as its template_pad
    bool refresh_ok = true;
    for (uint8_t i = 0; i < MAX_PADS; i++) {
        if (i == page) continue;
        if (cache_uses_template(i, page)) {
            if (!cache_update(i)) {
                refresh_ok = false;
                continue;
            }
#if HAS_DISPLAY
            icon_store_preload_pad(i);
#endif
        }
    }

    g_generation++;

    // Update fs_health stats
    storage_publish_usage(false);

    LOGI(TAG, "Pad %u saved (%u bytes, gen=%u)", page, (unsigned)len, g_generation);
    if (!refresh_ok) {
        LOGE(TAG, "Pad %u: dependent cache refresh incomplete", page);
    }
#if HAS_DISPLAY
    if (refresh_ok) icon_store_cleanup_pad_icons(page);
#endif
    for (uint8_t source = 0; source < previous_source_count; ++source)
        icon_store_source_delete_if_unreferenced(previous_sources[source]);
    pad_config_release(previous);
    return refresh_ok;
}

bool pad_config_delete(uint8_t page) {
    if (page >= MAX_PADS) return false;
    if (!g_fs_mounted) return false;

    char path[32];
    pad_config_path(page, path, sizeof(path));

    const PadConfig* previous = pad_config_acquire(page);
    char previous_sources[MAX_PAD_BUTTONS][CONFIG_ICON_SOURCE_ID_MAX_LEN] = {};
    const uint8_t previous_source_count = collect_icon_sources(previous, previous_sources);

    bool existed = Storage.exists(path);
    if (existed && !Storage.remove(path)) {
        LOGE(TAG, "Pad %u: delete failed", page);
        pad_config_release(previous);
        return false;
    }

    // Clear RAM cache before bumping generation (same ordering rationale as save)
    cache_lock();
    PadConfig* old = g_cache[page];
    g_cache[page] = nullptr;
    cache_unlock();
    pad_config_release(old);
    publish_eligibility(page, false);
    icon_store_delete_page_icons(page);

    // Refresh any pad that referenced this page as its template_pad
    bool refresh_ok = true;
    for (uint8_t i = 0; i < MAX_PADS; i++) {
        if (i == page) continue;
        if (cache_uses_template(i, page)) {
            if (!cache_update(i)) refresh_ok = false;
        }
    }

    g_generation++;

    storage_publish_usage(false);

    LOGI(TAG, "Pad %u %s (gen=%u)", page,
         existed ? "deleted" : "cache reconciled", g_generation);
    if (!refresh_ok) {
        LOGE(TAG, "Pad %u: dependent cache refresh incomplete", page);
    }
    for (uint8_t source = 0; source < previous_source_count; ++source)
        icon_store_source_delete_if_unreferenced(previous_sources[source]);
    pad_config_release(previous);
    return refresh_ok;
}

bool pad_config_exists(uint8_t page) {
    if (page >= MAX_PADS) return false;
    if (!g_fs_mounted) return false;

    char path[32];
    pad_config_path(page, path, sizeof(path));
    return Storage.exists(path);
}

char* pad_config_read_raw(uint8_t page, size_t* out_len) {
    if (out_len) *out_len = 0;
    if (page >= MAX_PADS) return nullptr;
    if (!g_fs_mounted) return nullptr;

    char path[32];
    pad_config_path(page, path, sizeof(path));

    File f = Storage.open(path, "r");
    if (!f) return nullptr;

    size_t file_size = f.size();
    if (file_size == 0 || file_size > 64 * 1024) {
        f.close();
        return nullptr;
    }

    char* buf = nullptr;
    if (psramFound()) {
        buf = (char*)heap_caps_malloc(file_size + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    if (!buf) {
        buf = (char*)malloc(file_size + 1);
    }
    if (!buf) {
        f.close();
        return nullptr;
    }

    size_t read = f.readBytes(buf, file_size);
    f.close();
    buf[read] = '\0';

    if (out_len) *out_len = read;
    return buf;
}

uint32_t pad_config_get_generation() {
    return g_generation;
}

uint32_t pad_config_get_eligible_mask() {
    return g_eligible_mask.load(std::memory_order_acquire);
}

bool pad_config_read_name(uint8_t page, char* out, size_t out_len) {
    if (out && out_len) out[0] = '\0';
    if (!out || out_len == 0 || page >= MAX_PADS) return false;
    const PadConfig* config = pad_config_acquire(page);
    if (!config) return false;
    const bool ok = config->name && config->name[0];
    if (ok) strlcpy(out, config->name, out_len);
    pad_config_release(config);
    return ok;
}

int pad_config_resolve_ref(const char* ref, char* err, size_t err_len) {
    if (err && err_len) err[0] = '\0';
    if (!ref || !ref[0]) { if (err) strlcpy(err, "missing pad reference (id 'pad_N' or friendly name)", err_len); return -1; }

    // Canonical id form: pad_<digits>.
    if (strncmp(ref, "pad_", 4) == 0) {
        const char* d = ref + 4;
        bool all_digits = d[0] != '\0';
        for (const char* p = d; *p; ++p) if (!isdigit((unsigned char)*p)) { all_digits = false; break; }
        if (all_digits) {
            int pg = atoi(d);
            if (pg >= 0 && pg < MAX_PADS) return pg;
            if (err) snprintf(err, err_len, "pad id out of range (0..%d)", MAX_PADS - 1);
            return -1;
        }
    }

    // Friendly-name lookup (case-insensitive) among existing pads.
    int found = -1, count = 0;
    char name[64], cand[120];
    size_t cl = 0; cand[0] = '\0';
    for (uint8_t pg = 0; pg < MAX_PADS; ++pg) {
        if (!pad_config_exists(pg)) continue;
        if (!pad_config_read_name(pg, name, sizeof(name))) continue;
        if (strcasecmp(name, ref) != 0) continue;
        found = pg; count++;
        int n = snprintf(cand + cl, sizeof(cand) - cl, "%spad_%u", cl ? ", " : "", (unsigned)pg);
        if (n > 0 && (size_t)n < sizeof(cand) - cl) cl += (size_t)n;
    }
    if (count == 1) return found;
    if (count == 0) { if (err) snprintf(err, err_len, "no pad with id or name '%s'", ref); return -1; }
    if (err) snprintf(err, err_len, "ambiguous pad name '%s' matches %s — use the pad id", ref, cand);
    return -1;
}

#endif // HAS_DISPLAY
