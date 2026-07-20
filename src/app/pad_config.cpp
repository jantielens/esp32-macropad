#include "pad_config.h"
#include "board_config.h"

#if HAS_DISPLAY

#include "action_parse.h"
#include "button_defaults.h"
#include "fs_health.h"
#include "icon_store.h"
#include "log_manager.h"
#include "widgets/widget.h"

#include <ArduinoJson.h>
#include "psram_json_allocator.h"
#include "storage.h"

#include <esp_heap_caps.h>
#include <esp_partition.h>

#include <stdio.h>
#include <ctype.h>
#include <string.h>

#define TAG "PadCfg"

static bool g_fs_mounted = false;
static uint32_t g_generation = 0;

// In-RAM cache so the LVGL render task (PSRAM stack) never touches flash.
// Flash reads require disabling cache, which is incompatible with PSRAM stacks.
static PadConfig* g_cache[MAX_PADS] = {};

// Forward declaration — defined after pad_config_init()
static bool pad_config_load_from_flash(uint8_t page, PadConfig* out,
                                       bool skip_template = false);

// ============================================================================
// Helpers
// ============================================================================

static void pad_config_path(uint8_t page, char* buf, size_t buf_len) {
    snprintf(buf, buf_len, "/config/pad_%u.json", page);
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
    strlcpy(btn->bg_color, "#333333", CONFIG_COLOR_MAX_LEN);
    strlcpy(btn->fg_color, "#FFFFFF", CONFIG_COLOR_MAX_LEN);
    strlcpy(btn->border_color, "#000000", CONFIG_COLOR_MAX_LEN);
    strlcpy(btn->border_width, "0", CONFIG_BINDABLE_SHORT_LEN);
    strlcpy(btn->corner_radius, "8", CONFIG_BINDABLE_SHORT_LEN);
    strlcpy(btn->content_pad, "4", CONFIG_BINDABLE_SHORT_LEN);
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

static void parse_button(JsonObject obj, ScreenButtonConfig* btn, const ButtonDefaults* defs) {
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

    strlcpy(btn->icon_id, obj["icon_id"] | "", CONFIG_ICON_ID_MAX_LEN);
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
                         btn_default(defs ? defs->bg_color : nullptr, "#333333"));
    parse_bindable_field(obj["fg_color"], btn->fg_color, CONFIG_COLOR_MAX_LEN,
                         btn_default(defs ? defs->fg_color : nullptr, "#FFFFFF"));
    parse_bindable_field(obj["border_color"], btn->border_color, CONFIG_COLOR_MAX_LEN,
                         btn_default(defs ? defs->border_color : nullptr, "#000000"));
    parse_bindable_field(obj["border_width"], btn->border_width, CONFIG_BINDABLE_SHORT_LEN,
                         btn_default(defs ? defs->border_width : nullptr, "0"), false);
    parse_bindable_field(obj["corner_radius"], btn->corner_radius, CONFIG_BINDABLE_SHORT_LEN,
                         btn_default(defs ? defs->corner_radius : nullptr, "8"), false);
    parse_bindable_field(obj["content_pad"], btn->content_pad, CONFIG_BINDABLE_SHORT_LEN,
                         btn_default(defs ? defs->content_pad : nullptr, "4"), false);

    // Typed actions — array of up to MAX_BUTTON_ACTIONS sequential actions per gesture.
    // JSON: "actions": [ { "type": "mqtt", ... }, { "type": "beep", ... } ]
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

    btn->confirm = obj["confirm"] | false;
    strlcpy(btn->confirm_text, obj["confirm_text"] | "", sizeof(btn->confirm_text));

    // Background image fields
    strlcpy(btn->bg_image_url, obj["bg_image_url"] | "", CONFIG_BG_IMAGE_URL_MAX_LEN);
    strlcpy(btn->bg_image_user, obj["bg_image_user"] | "", CONFIG_BG_IMAGE_USER_MAX_LEN);
    strlcpy(btn->bg_image_password, obj["bg_image_password"] | "", CONFIG_BG_IMAGE_PASS_MAX_LEN);
    btn->bg_image_interval_ms = obj["bg_image_interval_ms"] | (uint32_t)0;
    btn->bg_image_letterbox = obj["bg_image_letterbox"] | false;

    // Widget type (bar_chart, gauge, etc.)
    const char* wtype = obj["widget_type"] | "";
    strlcpy(btn->widget.type, wtype, CONFIG_WIDGET_TYPE_MAX_LEN);
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

    // Pre-load all existing page configs into RAM cache.
    // This runs on the main task (internal stack) so flash access is safe.
    bool any_loaded = false;
    for (uint8_t i = 0; i < MAX_PADS; i++) {
        char path[32];
        pad_config_path(i, path, sizeof(path));
        if (Storage.exists(path)) {
            PadConfig* cfg = (PadConfig*)heap_caps_malloc(
                sizeof(PadConfig), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (!cfg) cfg = (PadConfig*)malloc(sizeof(PadConfig));
            if (cfg) {
                // Temporarily call the flash-reading parse logic directly
                memset(cfg, 0, sizeof(PadConfig));
                if (pad_config_load_from_flash(i, cfg)) {
                    g_cache[i] = cfg;
                    any_loaded = true;
                    LOGD(TAG, "Cached page %u", i);
                } else {
                    free(cfg);
                }
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

// Merge buttons from a template pad into empty grid positions.
// Target pad's own buttons always win on col/row conflict.
static void merge_template_buttons(uint8_t page, PadConfig* out) {
    int8_t tpl = out->template_pad;
    if (tpl < 0 || tpl >= MAX_PADS || tpl == (int8_t)page) return;

    PadConfig* tpl_cfg = (PadConfig*)heap_caps_malloc(
        sizeof(PadConfig), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!tpl_cfg) tpl_cfg = (PadConfig*)malloc(sizeof(PadConfig));
    if (!tpl_cfg) return;

    // Load template pad with skip_template=true to prevent chaining
    if (!pad_config_load_from_flash((uint8_t)tpl, tpl_cfg, true)) {
        free(tpl_cfg);
        return;
    }

    // Build occupancy set for target pad's own buttons
    bool occupied[MAX_GRID_COLS][MAX_GRID_ROWS] = {};
    for (uint8_t i = 0; i < out->button_count; i++) {
        const ScreenButtonConfig& b = out->buttons[i];
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

    // Merge template buttons into empty positions
    uint8_t merged = 0;
    for (uint8_t i = 0; i < tpl_cfg->button_count; i++) {
        if (out->button_count >= MAX_PAD_BUTTONS) break;
        const ScreenButtonConfig& tb = tpl_cfg->buttons[i];
        // Skip template buttons outside target grid
        if (tb.col >= out->cols || tb.row >= out->rows) continue;
        // Check all cells the template button would occupy
        uint8_t cs = tb.col_span ? tb.col_span : 1;
        uint8_t rs = tb.row_span ? tb.row_span : 1;
        bool conflict = false;
        for (uint8_t dc = 0; dc < cs && !conflict; dc++) {
            for (uint8_t dr = 0; dr < rs && !conflict; dr++) {
                uint8_t c = tb.col + dc, r = tb.row + dr;
                if (c >= out->cols || r >= out->rows || occupied[c][r])
                    conflict = true;
            }
        }
        if (conflict) continue;
        // Copy template button and mark cells occupied
        memcpy(&out->buttons[out->button_count], &tb, sizeof(ScreenButtonConfig));
        out->button_count++;
        merged++;
        for (uint8_t dc = 0; dc < cs; dc++) {
            for (uint8_t dr = 0; dr < rs; dr++) {
                uint8_t c = tb.col + dc, r = tb.row + dr;
                if (c < MAX_GRID_COLS && r < MAX_GRID_ROWS)
                    occupied[c][r] = true;
            }
        }
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
        LOGI(TAG, "Page %u: merged %u buttons from template pad %d", page, merged, tpl);
    }

    free(tpl_cfg);
}

static bool pad_config_load_from_flash(uint8_t page, PadConfig* out,
                                       bool skip_template) {
    if (!out) return false;
    memset(out, 0, sizeof(PadConfig));

    strlcpy(out->layout, "grid", CONFIG_LAYOUT_NAME_MAX_LEN);
    out->cols = 3;
    out->rows = 3;
    out->template_pad = -1;
    if (page >= MAX_PADS) return false;
    if (!g_fs_mounted) return false;

    char path[32];
    pad_config_path(page, path, sizeof(path));

    File f = Storage.open(path, "r");
    if (!f) {
        LOGD(TAG, "Page %u config not found", page);
        return false;
    }

    size_t file_size = f.size();
    if (file_size == 0 || file_size > 64 * 1024) {
        LOGW(TAG, "Page %u: invalid file size %u", page, (unsigned)file_size);
        f.close();
        return false;
    }

    char* buf = nullptr;
    if (psramFound()) {
        buf = (char*)heap_caps_malloc(file_size + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    if (!buf) {
        buf = (char*)malloc(file_size + 1);
    }
    if (!buf) {
        LOGE(TAG, "Page %u: OOM for %u bytes", page, (unsigned)file_size);
        f.close();
        return false;
    }

    size_t read = f.readBytes(buf, file_size);
    f.close();
    buf[read] = '\0';

    BasicJsonDocument<PsramJsonAllocator> doc(file_size * 2 + 256);
    DeserializationError err = deserializeJson(doc, buf, read);
    free(buf);

    if (err) {
        LOGE(TAG, "Page %u: JSON parse error: %s", page, err.c_str());
        return false;
    }

    strlcpy(out->layout, doc["layout"] | "grid", CONFIG_LAYOUT_NAME_MAX_LEN);
    out->cols = doc["cols"] | (uint8_t)3;
    out->rows = doc["rows"] | (uint8_t)3;
    strlcpy(out->wake_screen, doc["wake_screen"] | "", CONFIG_SCREEN_ID_MAX_LEN);
    parse_bindable_field(doc["bg_color"], out->bg_color, CONFIG_COLOR_MAX_LEN, "#000000");
    out->template_pad = doc["template_pad"] | (int8_t)-1;
    if (out->template_pad >= MAX_PADS) out->template_pad = -1;

    // Parse named page-level bindings: { "bindings": { "name": "template", ... } }
    out->binding_count = 0;
    JsonObject bindings_obj = doc["bindings"];
    if (!bindings_obj.isNull()) {
        for (JsonPair kv : bindings_obj) {
            if (out->binding_count >= PAD_MAX_BINDINGS) {
                LOGW(TAG, "Page %u: max %d bindings reached, skipping rest", page, PAD_MAX_BINDINGS);
                break;
            }
            const char* name = kv.key().c_str();
            const char* value = kv.value().as<const char*>();
            if (!name || !value) continue;
            if (!is_valid_binding_name(name)) {
                LOGW(TAG, "Page %u: skipping invalid binding name '%s'", page, name);
                continue;
            }
            PadBinding& b = out->bindings[out->binding_count];
            strlcpy(b.name, name, PAD_BINDING_NAME_MAX_LEN);
            strlcpy(b.value, value, CONFIG_LABEL_MAX_LEN);
            out->binding_count++;
        }
        if (out->binding_count > 0) {
            LOGI(TAG, "Page %u: %u named bindings loaded", page, out->binding_count);
        }
    }

    if (out->cols < 1) out->cols = 1;
    if (out->cols > MAX_GRID_COLS) out->cols = MAX_GRID_COLS;
    if (out->rows < 1) out->rows = 1;
    if (out->rows > MAX_GRID_ROWS) out->rows = MAX_GRID_ROWS;

    // Use device-level button defaults for cascading to buttons
#if HAS_DISPLAY
    const ButtonDefaults* defs = button_defaults_get();
#else
    const ButtonDefaults* defs = nullptr;
#endif

    JsonArray buttons = doc["buttons"];
    out->button_count = 0;
    for (JsonObject btn_obj : buttons) {
        if (out->button_count >= MAX_PAD_BUTTONS) break;
        parse_button(btn_obj, &out->buttons[out->button_count], defs);
        out->button_count++;
    }

    // Merge template pad buttons into empty grid positions
    if (!skip_template) {
        merge_template_buttons(page, out);
    }

    LOGI(TAG, "Page %u loaded: layout=%s cols=%u rows=%u buttons=%u",
         page, out->layout, out->cols, out->rows, out->button_count);
    return true;
}

// Update the in-RAM cache for a page (parse from flash).
// Call from a task with internal-RAM stack (web server, main task).
static void cache_update(uint8_t page) {
    PadConfig* cfg = (PadConfig*)heap_caps_malloc(
        sizeof(PadConfig), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!cfg) cfg = (PadConfig*)malloc(sizeof(PadConfig));
    if (!cfg) return;

    if (pad_config_load_from_flash(page, cfg)) {
        PadConfig* old = g_cache[page];
        g_cache[page] = cfg;
        free(old);
    } else {
        free(cfg);
        // Config no longer loadable — clear cache
        free(g_cache[page]);
        g_cache[page] = nullptr;
    }
}

void pad_config_rebuild_all_caches() {
    if (!g_fs_mounted) return;
    for (uint8_t i = 0; i < MAX_PADS; i++) {
        if (g_cache[i]) cache_update(i);
    }
    g_generation++;
    LOGI(TAG, "Rebuilt all pad caches");
}

bool pad_config_load(uint8_t page, PadConfig* out) {
    if (!out) return false;
    memset(out, 0, sizeof(PadConfig));

    strlcpy(out->layout, "grid", CONFIG_LAYOUT_NAME_MAX_LEN);
    out->cols = 3;
    out->rows = 3;
    out->template_pad = -1;

    if (page >= MAX_PADS) return false;

    // Serve from RAM cache — safe to call from any task (including PSRAM-stack LVGL task)
    if (g_cache[page]) {
        memcpy(out, g_cache[page], sizeof(PadConfig));
        return true;
    }

    // No cache entry — page not configured
    return false;
}

bool pad_config_save_raw(uint8_t page, const uint8_t* json, size_t len) {
    if (page >= MAX_PADS) return false;
    if (!g_fs_mounted) return false;
    if (!json || len == 0) return false;

    char path[32];
    pad_config_path(page, path, sizeof(path));

    File f = Storage.open(path, "w");
    if (!f) {
        LOGE(TAG, "Page %u: failed to open for write", page);
        return false;
    }

    size_t written = f.write(json, len);
    f.close();

    if (written != len) {
        LOGE(TAG, "Page %u: write failed (wrote %u of %u)", page, (unsigned)written, (unsigned)len);
        return false;
    }

    // Update RAM cache BEFORE bumping generation so the LVGL task
    // always reads fresh data when it detects the new generation.
    cache_update(page);

    // Preload icons for this pad (picks up template button icons that
    // aren't yet in the PSRAM icon cache, e.g. after template_pad change).
#if HAS_DISPLAY
    icon_store_preload_pad(page);
#endif

    // Also refresh any pad that references this page as its template_pad
    for (uint8_t i = 0; i < MAX_PADS; i++) {
        if (i == page) continue;
        if (g_cache[i] && g_cache[i]->template_pad == (int8_t)page) {
            cache_update(i);
#if HAS_DISPLAY
            icon_store_preload_pad(i);
#endif
        }
    }

    g_generation++;

    // Update fs_health stats
    storage_publish_usage(false);

    LOGI(TAG, "Page %u saved (%u bytes, gen=%u)", page, (unsigned)len, g_generation);
    return true;
}

bool pad_config_delete(uint8_t page) {
    if (page >= MAX_PADS) return false;
    if (!g_fs_mounted) return false;

    char path[32];
    pad_config_path(page, path, sizeof(path));

    if (!Storage.exists(path)) {
        LOGD(TAG, "Page %u: nothing to delete", page);
        return true;  // Already gone
    }

    if (!Storage.remove(path)) {
        LOGE(TAG, "Page %u: delete failed", page);
        return false;
    }

    // Clear RAM cache before bumping generation (same ordering rationale as save)
    free(g_cache[page]);
    g_cache[page] = nullptr;

    // Refresh any pad that referenced this page as its template_pad
    for (uint8_t i = 0; i < MAX_PADS; i++) {
        if (i == page) continue;
        if (g_cache[i] && g_cache[i]->template_pad == (int8_t)page) {
            cache_update(i);
        }
    }

    g_generation++;

    storage_publish_usage(false);

    LOGI(TAG, "Page %u deleted (gen=%u)", page, g_generation);
    return true;
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

bool pad_config_read_name(uint8_t page, char* out, size_t out_len) {
    if (out && out_len) out[0] = '\0';
    if (!out || out_len == 0 || page >= MAX_PADS) return false;
    size_t len = 0;
    char* raw = pad_config_read_raw(page, &len);
    if (!raw) return false;
    // Read only the top-level "name" (friendly label) without parsing the whole
    // pad. Mirrors the filtered read in web_portal_device_api.
    JsonDocument filter;
    filter["name"] = true;
    JsonDocument doc;
    bool ok = false;
    if (deserializeJson(doc, raw, len, DeserializationOption::Filter(filter)) == DeserializationError::Ok
        && doc["name"].is<const char*>()) {
        const char* n = doc["name"];
        if (n && n[0]) { strlcpy(out, n, out_len); ok = true; }
    }
    free(raw);
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
