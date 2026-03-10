#include "pad_config.h"

#include "board_config.h"
#include "fs_health.h"
#include "log_manager.h"
#if HAS_DISPLAY
#include "widgets/widget.h"
#endif

#include <ArduinoJson.h>
#include <LittleFS.h>

#include <esp_heap_caps.h>
#include <esp_partition.h>

#include <stdio.h>
#include <string.h>

#define TAG "PadCfg"

static bool g_fs_mounted = false;
static uint32_t g_generation = 0;

// In-RAM cache so the LVGL render task (PSRAM stack) never touches flash.
// Flash reads require disabling cache, which is incompatible with PSRAM stacks.
static PadPageConfig* g_cache[MAX_PAD_PAGES] = {};

// Forward declaration — defined after pad_config_init()
static bool pad_config_load_from_flash(uint8_t page, PadPageConfig* out);

// ============================================================================
// Helpers
// ============================================================================

static void pad_config_path(uint8_t page, char* buf, size_t buf_len) {
    snprintf(buf, buf_len, "/config/pad_%u.json", page);
}

// Parse a color value from JSON (supports "#RRGGBB", "RRGGBB", "0xRRGGBB", or integer)
static uint32_t parse_color(JsonVariant v, uint32_t default_val) {
    if (v.isNull()) return default_val;
    if (v.is<unsigned long>() || v.is<long>()) return (uint32_t)v.as<unsigned long>();
    if (!v.is<const char*>()) return default_val;

    const char* s = v.as<const char*>();
    if (!s || !*s) return default_val;

    // Skip leading # or 0x
    if (s[0] == '#') s++;
    else if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;

    char* end = nullptr;
    uint32_t val = strtoul(s, &end, 16);
    if (end == s) return default_val;
    return val;
}

// ============================================================================
// Label Style DSL parser
// ============================================================================
// Format: "key:value;key:value;..."
// Keys: font (12/14/18/24/32/36), align (left/center/right),
//        y (int offset), mode (clip/scroll/dot/wrap), color (#RRGGBB)
void label_style_parse(const char* dsl, LabelStyle* out) {
    memset(out, 0, sizeof(LabelStyle));
    if (!dsl || !dsl[0]) return;

    // Work on a local copy to tokenize
    char buf[CONFIG_LABEL_STYLE_MAX_LEN];
    strlcpy(buf, dsl, sizeof(buf));

    char* saveptr = nullptr;
    char* token = strtok_r(buf, ";", &saveptr);
    while (token) {
        // Skip leading whitespace
        while (*token == ' ') token++;
        char* colon = strchr(token, ':');
        if (colon) {
            *colon = '\0';
            const char* key = token;
            const char* val = colon + 1;

            if (strcmp(key, "font") == 0) {
                int sz = atoi(val);
                if (sz == 12 || sz == 14 || sz == 18 || sz == 24 || sz == 32 || sz == 36) {
                    out->font_size = (uint8_t)sz;
                }
            } else if (strcmp(key, "align") == 0) {
                if (strcmp(val, "left") == 0)        out->align = LABEL_ALIGN_LEFT;
                else if (strcmp(val, "right") == 0)  out->align = LABEL_ALIGN_RIGHT;
                else if (strcmp(val, "center") == 0) out->align = LABEL_ALIGN_CENTER;
            } else if (strcmp(key, "y") == 0) {
                int y = atoi(val);
                if (y < -128) y = -128;
                if (y > 127)  y = 127;
                out->y_offset = (int8_t)y;
            } else if (strcmp(key, "mode") == 0) {
                if (strcmp(val, "clip") == 0)        out->long_mode = LABEL_MODE_CLIP;
                else if (strcmp(val, "scroll") == 0) out->long_mode = LABEL_MODE_SCROLL;
                else if (strcmp(val, "dot") == 0)    out->long_mode = LABEL_MODE_DOT;
                else if (strcmp(val, "wrap") == 0)   out->long_mode = LABEL_MODE_WRAP;
            } else if (strcmp(key, "color") == 0) {
                uint32_t c;
                if (parse_hex_color(val, &c)) {
                    out->color = c | 0x01000000; // Set marker bit
                }
            }
        }
        token = strtok_r(nullptr, ";", &saveptr);
    }
}

static void init_button_defaults(ScreenButtonConfig* btn) {
    memset(btn, 0, sizeof(ScreenButtonConfig));
    btn->col_span = 1;
    btn->row_span = 1;
    strlcpy(btn->bg_color, "#333333", CONFIG_COLOR_MAX_LEN);
    strlcpy(btn->fg_color, "#FFFFFF", CONFIG_COLOR_MAX_LEN);
    strlcpy(btn->border_color, "#000000", CONFIG_COLOR_MAX_LEN);
    btn->bg_color_default = 0x333333;
    btn->fg_color_default = 0xFFFFFF;
    btn->border_color_default = 0x000000;
    btn->border_width_px = 0;
    btn->corner_radius_px = 8;
}

// Parse a color field from JSON into a string (binding or hex color).
// If the JSON value is an integer, convert it to "#RRGGBB" string.
static void parse_color_field(JsonVariant v, char* out, size_t out_len, const char* default_hex) {
    if (v.isNull()) {
        strlcpy(out, default_hex, out_len);
        return;
    }
    if (v.is<unsigned long>() || v.is<long>()) {
        // Integer → convert to hex string
        uint32_t val = (uint32_t)v.as<unsigned long>();
        snprintf(out, out_len, "#%06X", val & 0xFFFFFF);
        return;
    }
    if (v.is<const char*>()) {
        const char* s = v.as<const char*>();
        if (s && s[0]) {
            strlcpy(out, s, out_len);
            return;
        }
    }
    strlcpy(out, default_hex, out_len);
}

// Parse a static color string (hex only) to uint32_t.
// Returns default_val if the string is empty, null, or a binding template.
static uint32_t parse_static_color(const char* s, uint32_t default_val) {
    if (!s || !s[0] || s[0] == '[') return default_val;
    uint32_t val;
    return parse_hex_color(s, &val) ? val : default_val;
}

// Parse a typed action object: { "type": "screen", "target": "pad_1" }
// Also supports legacy flat key fallback (old "action_screen" string → type=screen).
static void parse_action(JsonVariant v, ButtonAction* act, const char* legacy_screen_key, JsonObject obj) {
    memset(act, 0, sizeof(ButtonAction));

    if (v.is<JsonObject>()) {
        JsonObject a = v.as<JsonObject>();
        strlcpy(act->type, a["type"] | "", CONFIG_ACTION_TYPE_MAX_LEN);
        strlcpy(act->screen_id, a["target"] | "", CONFIG_SCREEN_ID_MAX_LEN);
        strlcpy(act->mqtt_topic, a["topic"] | "", CONFIG_MQTT_TOPIC_MAX_LEN);
        strlcpy(act->mqtt_payload, a["payload"] | "", CONFIG_MQTT_PAYLOAD_MAX_LEN);
        return;
    }

    // Legacy flat key fallback: "action_screen" / "lp_action_screen" → type=screen
    if (legacy_screen_key) {
        const char* screen = obj[legacy_screen_key] | "";
        if (screen[0]) {
            strlcpy(act->type, ACTION_TYPE_SCREEN, CONFIG_ACTION_TYPE_MAX_LEN);
            strlcpy(act->screen_id, screen, CONFIG_SCREEN_ID_MAX_LEN);
        }
    }
}

static void parse_button(JsonObject obj, ScreenButtonConfig* btn) {
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

    // Per-label style overrides (DSL strings, e.g. "font:24;align:right")
    label_style_parse(obj["label_top_style"] | "", &btn->style_top);
    label_style_parse(obj["label_center_style"] | "", &btn->style_center);
    label_style_parse(obj["label_bottom_style"] | "", &btn->style_bottom);

    strlcpy(btn->icon_id, obj["icon_id"] | "", CONFIG_ICON_ID_MAX_LEN);
    btn->icon_scale_pct = obj["icon_scale_pct"] | (uint8_t)0;

    parse_color_field(obj["bg_color"], btn->bg_color, CONFIG_COLOR_MAX_LEN, "#333333");
    parse_color_field(obj["fg_color"], btn->fg_color, CONFIG_COLOR_MAX_LEN, "#FFFFFF");
    parse_color_field(obj["border_color"], btn->border_color, CONFIG_COLOR_MAX_LEN, "#000000");
    btn->bg_color_default = parse_color(obj["bg_color_default"], parse_static_color(btn->bg_color, 0x333333));
    btn->fg_color_default = parse_color(obj["fg_color_default"], parse_static_color(btn->fg_color, 0xFFFFFF));
    btn->border_color_default = parse_color(obj["border_color_default"], parse_static_color(btn->border_color, 0x000000));
    btn->border_width_px = obj["border_width"] | (uint16_t)0;
    btn->corner_radius_px = obj["corner_radius"] | (uint16_t)8;

    // Typed actions (with legacy flat key fallback)
    parse_action(obj["action"], &btn->action, "action_screen", obj);
    parse_action(obj["lp_action"], &btn->lp_action, "lp_action_screen", obj);

    // Background image fields
    strlcpy(btn->bg_image_url, obj["bg_image_url"] | "", CONFIG_BG_IMAGE_URL_MAX_LEN);
    strlcpy(btn->bg_image_user, obj["bg_image_user"] | "", CONFIG_BG_IMAGE_USER_MAX_LEN);
    strlcpy(btn->bg_image_password, obj["bg_image_password"] | "", CONFIG_BG_IMAGE_PASS_MAX_LEN);
    btn->bg_image_interval_ms = obj["bg_image_interval_ms"] | (uint32_t)0;
    btn->bg_image_letterbox = obj["bg_image_letterbox"] | false;

    // Widget type (bar_chart, gauge, etc.)
    const char* wtype = obj["widget_type"] | "";
    strlcpy(btn->widget.type, wtype, CONFIG_WIDGET_TYPE_MAX_LEN);
    strlcpy(btn->widget.data_binding, obj["widget_data_binding"] | "", CONFIG_LABEL_MAX_LEN);
    strlcpy(btn->widget.data_binding_2, obj["widget_data_binding_2"] | "", CONFIG_LABEL_MAX_LEN);
    strlcpy(btn->widget.data_binding_3, obj["widget_data_binding_3"] | "", CONFIG_LABEL_MAX_LEN);
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

    // Find storage partition by subtype (label may vary across boards)
    const esp_partition_t* part = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA,
        ESP_PARTITION_SUBTYPE_DATA_SPIFFS,
        nullptr);
    if (!part) {
        LOGW(TAG, "No storage partition found — pad configs will not persist");
        return false;
    }

    LOGI(TAG, "Found storage partition '%s' (%u KB)", part->label, part->size / 1024);

    if (!LittleFS.begin(true /* formatOnFail */, "/littlefs", 10, part->label)) {
        LOGE(TAG, "LittleFS mount failed on partition '%s'", part->label);
        return false;
    }

    g_fs_mounted = true;

    // Update fs_health stats
    fs_health_set_storage_usage(LittleFS.usedBytes(), LittleFS.totalBytes());

    // Ensure /config directory exists
    if (!LittleFS.exists("/config")) {
        LittleFS.mkdir("/config");
    }

    LOGI(TAG, "LittleFS mounted (total=%u used=%u)", LittleFS.totalBytes(), LittleFS.usedBytes());

    // Pre-load all existing page configs into RAM cache.
    // This runs on the main task (internal stack) so flash access is safe.
    bool any_loaded = false;
    for (uint8_t i = 0; i < MAX_PAD_PAGES; i++) {
        char path[32];
        pad_config_path(i, path, sizeof(path));
        if (LittleFS.exists(path)) {
            PadPageConfig* cfg = (PadPageConfig*)heap_caps_malloc(
                sizeof(PadPageConfig), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (!cfg) cfg = (PadPageConfig*)malloc(sizeof(PadPageConfig));
            if (cfg) {
                // Temporarily call the flash-reading parse logic directly
                memset(cfg, 0, sizeof(PadPageConfig));
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
static bool pad_config_load_from_flash(uint8_t page, PadPageConfig* out) {
    if (!out) return false;
    memset(out, 0, sizeof(PadPageConfig));

    strlcpy(out->layout, "grid", CONFIG_LAYOUT_NAME_MAX_LEN);
    out->cols = 3;
    out->rows = 3;

    if (page >= MAX_PAD_PAGES) return false;
    if (!g_fs_mounted) return false;

    char path[32];
    pad_config_path(page, path, sizeof(path));

    File f = LittleFS.open(path, "r");
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

    DynamicJsonDocument doc(file_size * 2 + 256);
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
    parse_color_field(doc["bg_color"], out->bg_color, CONFIG_COLOR_MAX_LEN, "#000000");
    out->bg_color_default = parse_color(doc["bg_color_default"], parse_static_color(out->bg_color, 0x000000));

    if (out->cols < 1) out->cols = 1;
    if (out->cols > MAX_GRID_COLS) out->cols = MAX_GRID_COLS;
    if (out->rows < 1) out->rows = 1;
    if (out->rows > MAX_GRID_ROWS) out->rows = MAX_GRID_ROWS;

    JsonArray buttons = doc["buttons"];
    out->button_count = 0;
    for (JsonObject btn_obj : buttons) {
        if (out->button_count >= MAX_PAD_BUTTONS) break;
        parse_button(btn_obj, &out->buttons[out->button_count]);
        out->button_count++;
    }

    LOGI(TAG, "Page %u loaded: layout=%s cols=%u rows=%u buttons=%u",
         page, out->layout, out->cols, out->rows, out->button_count);
    return true;
}

// Update the in-RAM cache for a page (parse from flash).
// Call from a task with internal-RAM stack (web server, main task).
static void cache_update(uint8_t page) {
    PadPageConfig* cfg = (PadPageConfig*)heap_caps_malloc(
        sizeof(PadPageConfig), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!cfg) cfg = (PadPageConfig*)malloc(sizeof(PadPageConfig));
    if (!cfg) return;

    if (pad_config_load_from_flash(page, cfg)) {
        PadPageConfig* old = g_cache[page];
        g_cache[page] = cfg;
        free(old);
    } else {
        free(cfg);
        // Config no longer loadable — clear cache
        free(g_cache[page]);
        g_cache[page] = nullptr;
    }
}

bool pad_config_load(uint8_t page, PadPageConfig* out) {
    if (!out) return false;
    memset(out, 0, sizeof(PadPageConfig));

    strlcpy(out->layout, "grid", CONFIG_LAYOUT_NAME_MAX_LEN);
    out->cols = 3;
    out->rows = 3;

    if (page >= MAX_PAD_PAGES) return false;

    // Serve from RAM cache — safe to call from any task (including PSRAM-stack LVGL task)
    if (g_cache[page]) {
        memcpy(out, g_cache[page], sizeof(PadPageConfig));
        return true;
    }

    // No cache entry — page not configured
    return false;
}

bool pad_config_save_raw(uint8_t page, const uint8_t* json, size_t len) {
    if (page >= MAX_PAD_PAGES) return false;
    if (!g_fs_mounted) return false;
    if (!json || len == 0) return false;

    char path[32];
    pad_config_path(page, path, sizeof(path));

    File f = LittleFS.open(path, "w");
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

    g_generation++;

    // Update fs_health stats
    fs_health_set_storage_usage(LittleFS.usedBytes(), LittleFS.totalBytes());

    LOGI(TAG, "Page %u saved (%u bytes, gen=%u)", page, (unsigned)len, g_generation);
    return true;
}

bool pad_config_delete(uint8_t page) {
    if (page >= MAX_PAD_PAGES) return false;
    if (!g_fs_mounted) return false;

    char path[32];
    pad_config_path(page, path, sizeof(path));

    if (!LittleFS.exists(path)) {
        LOGD(TAG, "Page %u: nothing to delete", page);
        return true;  // Already gone
    }

    if (!LittleFS.remove(path)) {
        LOGE(TAG, "Page %u: delete failed", page);
        return false;
    }

    // Clear RAM cache before bumping generation (same ordering rationale as save)
    free(g_cache[page]);
    g_cache[page] = nullptr;

    g_generation++;

    fs_health_set_storage_usage(LittleFS.usedBytes(), LittleFS.totalBytes());

    LOGI(TAG, "Page %u deleted (gen=%u)", page, g_generation);
    return true;
}

bool pad_config_exists(uint8_t page) {
    if (page >= MAX_PAD_PAGES) return false;
    if (!g_fs_mounted) return false;

    char path[32];
    pad_config_path(page, path, sizeof(path));
    return LittleFS.exists(path);
}

char* pad_config_read_raw(uint8_t page, size_t* out_len) {
    if (out_len) *out_len = 0;
    if (page >= MAX_PAD_PAGES) return nullptr;
    if (!g_fs_mounted) return nullptr;

    char path[32];
    pad_config_path(page, path, sizeof(path));

    File f = LittleFS.open(path, "r");
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
