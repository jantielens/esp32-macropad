#include "widget.h"

#if HAS_DISPLAY

#include "../log_manager.h"
#include <Arduino.h>
#include <stdlib.h>
#include <string.h>
#if __has_include("soc/soc_caps.h")
#include "soc/soc_caps.h"
#endif

#define TAG "Table"

#define TABLE_MAX_COLS    8
#define TABLE_MAX_ROWS    64
#define TABLE_COL_KEY_LEN 20
#define TABLE_PAYLOAD_MAX 2048

struct TableWidgetConfig {
    char binding[CONFIG_LABEL_MAX_LEN];
    char table_style[CONFIG_LABEL_STYLE_MAX_LEN];
    bool scrollable;
};

static_assert(sizeof(TableWidgetConfig) <= WIDGET_CONFIG_MAX_BYTES,
              "TableWidgetConfig exceeds WIDGET_CONFIG_MAX_BYTES");

struct TableWidgetState {
    lv_obj_t*  table;
    lv_style_t cell_style;
    lv_color_t hdr_text_color;
    lv_color_t hdr_border_color;
    lv_color_t default_bg;
    lv_color_t default_text_color;
    lv_color_t row_colors[TABLE_MAX_ROWS];
    lv_color_t cell_bg[TABLE_MAX_ROWS][TABLE_MAX_COLS];
    lv_color_t cell_text[TABLE_MAX_ROWS][TABLE_MAX_COLS];
    bool       cell_bg_set[TABLE_MAX_ROWS][TABLE_MAX_COLS];
    bool       cell_text_set[TABLE_MAX_ROWS][TABLE_MAX_COLS];
    char       col_keys[TABLE_MAX_COLS][TABLE_COL_KEY_LEN];
    uint8_t    col_src_idx[TABLE_MAX_COLS];
    uint8_t    col_count;
    uint16_t   row_count;
    uint32_t   last_payload_hash;
    bool       hide_header;
};

static inline TableWidgetState* table_get_state(WidgetState* state) {
    TableWidgetState* p;
    memcpy(&p, state->data, sizeof(p));
    return p;
}

static inline void table_set_state(WidgetState* state, TableWidgetState* p) {
    memcpy(state->data, &p, sizeof(p));
}

static uint32_t table_hash_str(const char* s) {
    uint32_t hash = 2166136261u;
    if (!s) return hash;
    while (*s) {
        hash ^= (uint8_t)(*s++);
        hash *= 16777619u;
    }
    return hash;
}

static bool table_parse_color_str(JsonVariantConst v, lv_color_t* out) {
    if (!v.is<const char*>()) return false;
    uint32_t rgb;
    if (!parse_hex_color(v.as<const char*>(), &rgb)) return false;
    *out = lv_color_make((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
    return true;
}

static void table_value_to_cstr(JsonVariantConst v, char* out, size_t out_len) {
    if (v.is<const char*>()) {
        strlcpy(out, v.as<const char*>(), out_len);
        return;
    }
    if (v.is<bool>()) {
        strlcpy(out, v.as<bool>() ? "true" : "false", out_len);
        return;
    }
    if (v.is<long>() || v.is<int>() || v.is<unsigned long>() || v.is<unsigned int>()) {
        snprintf(out, out_len, "%ld", v.as<long>());
        return;
    }
    if (v.is<float>() || v.is<double>()) {
        snprintf(out, out_len, "%g", v.as<double>());
        return;
    }
    out[0] = '\0';
}

static void table_reset_colors(TableWidgetState* st) {
    for (uint16_t r = 0; r < TABLE_MAX_ROWS; r++) {
        st->row_colors[r] = st->default_bg;
        for (uint8_t c = 0; c < TABLE_MAX_COLS; c++) {
            st->cell_bg_set[r][c] = false;
            st->cell_text_set[r][c] = false;
        }
    }
}

static void table_set_cell(TableWidgetState* st, uint16_t row, uint8_t col,
                           JsonVariantConst cell) {
    char text[64];
    text[0] = '\0';

    if (cell.is<JsonObjectConst>()) {
        JsonObjectConst obj = cell.as<JsonObjectConst>();
        table_value_to_cstr(obj["text"], text, sizeof(text));

        lv_color_t color;
        if (table_parse_color_str(obj["bg"], &color)) {
            st->cell_bg[row][col] = color;
            st->cell_bg_set[row][col] = true;
        }
        if (table_parse_color_str(obj["color"], &color)) {
            st->cell_text[row][col] = color;
            st->cell_text_set[row][col] = true;
        }
    } else {
        table_value_to_cstr(cell, text, sizeof(text));
    }

    lv_table_set_cell_value(st->table, row + 1, col, text);
}

static void table_apply_columns(TableWidgetState* st, JsonArrayConst columns,
                                JsonObjectConst first_row) {
    st->col_count = 0;

    if (!columns.isNull()) {
        uint8_t src_idx = 0;
        for (JsonVariantConst col_var : columns) {
            if (st->col_count >= TABLE_MAX_COLS) break;
            if (!col_var.is<JsonObjectConst>()) {
                src_idx++;
                continue;
            }
            JsonObjectConst col = col_var.as<JsonObjectConst>();
            const char* key = col["key"] | "";
            if (!key[0]) {
                src_idx++;
                continue;
            }
            strlcpy(st->col_keys[st->col_count], key, sizeof(st->col_keys[st->col_count]));
            st->col_src_idx[st->col_count] = src_idx;
            st->col_count++;
            src_idx++;
        }
    } else if (!first_row.isNull()) {
        for (JsonPairConst kv : first_row) {
            if (st->col_count >= TABLE_MAX_COLS) break;
            const char* key = kv.key().c_str();
            if (!key[0] || key[0] == '_') continue;
            strlcpy(st->col_keys[st->col_count], key, sizeof(st->col_keys[st->col_count]));
            st->col_src_idx[st->col_count] = 0xFF;
            st->col_count++;
        }
    }

    if (st->col_count == 0) {
        lv_table_set_column_count(st->table, 1);
        lv_table_set_column_width(st->table, 0, lv_obj_get_width(st->table));
        lv_table_set_row_count(st->table, 1);
        lv_table_set_cell_value(st->table, 0, 0, "Table");
        return;
    }

    lv_table_set_column_count(st->table, st->col_count);

    int16_t usable_w = lv_obj_get_content_width(st->table);
    if (usable_w <= 0) usable_w = lv_obj_get_width(st->table);
    int16_t assigned_px = 0;
    uint8_t fill_count = 0;

    for (uint8_t c = 0; c < st->col_count; c++) {
        int16_t col_w = 0;
        if (!columns.isNull()) {
            uint8_t src_idx = st->col_src_idx[c];
            if (src_idx != 0xFF && src_idx < columns.size()) {
                JsonObjectConst col = columns[src_idx].as<JsonObjectConst>();
                uint8_t pct = col["width_pct"] | (uint8_t)0;
                if (pct > 0) col_w = (int16_t)((usable_w * pct) / 100);
            }
        }
        if (col_w > 0) {
            lv_table_set_column_width(st->table, c, col_w);
            assigned_px += col_w;
        } else {
            lv_table_set_column_width(st->table, c, 0);
            fill_count++;
        }
    }

    int16_t remaining = usable_w - assigned_px;
    int16_t per_fill = (fill_count > 0 && remaining > 0) ? (remaining / fill_count) : (usable_w / st->col_count);
    for (uint8_t c = 0; c < st->col_count; c++) {
        if (lv_table_get_column_width(st->table, c) == 0) {
            lv_table_set_column_width(st->table, c, per_fill > 0 ? per_fill : 1);
        }
    }

    lv_table_set_row_count(st->table, 1);
    for (uint8_t c = 0; c < st->col_count; c++) {
        const char* header = st->col_keys[c];
        if (!columns.isNull()) {
            uint8_t src_idx = st->col_src_idx[c];
            if (src_idx != 0xFF && src_idx < columns.size()) {
                JsonObjectConst col = columns[src_idx].as<JsonObjectConst>();
                header = col["header"] | st->col_keys[c];
            }
        }
        lv_table_set_cell_value(st->table, 0, c, header);
    }
}

static bool table_resolve_binding(const TableWidgetConfig* cfg, char* out, size_t out_len) {
    if (!cfg->binding[0]) {
        out[0] = '\0';
        return false;
    }
    if (binding_template_resolve_single_token(cfg->binding, out, out_len)) {
        return true;
    }
    binding_template_resolve(cfg->binding, out, out_len);
    return true;
}

static void table_draw_cb(lv_event_t* e) {
    lv_draw_task_t* draw_task = lv_event_get_draw_task(e);
    lv_draw_dsc_base_t* base_dsc = (lv_draw_dsc_base_t*)lv_draw_task_get_draw_dsc(draw_task);
    if (base_dsc->part != LV_PART_ITEMS) return;

    uint32_t row = base_dsc->id1;
    uint32_t col = base_dsc->id2;
    TableWidgetState* st = (TableWidgetState*)lv_event_get_user_data(e);
    if (!st) return;

    if (row == 0) {
        lv_draw_label_dsc_t* label_dsc = lv_draw_task_get_label_dsc(draw_task);
        lv_draw_fill_dsc_t* fill_dsc = lv_draw_task_get_fill_dsc(draw_task);
        lv_draw_border_dsc_t* border_dsc = lv_draw_task_get_border_dsc(draw_task);
        if (st->hide_header) {
            if (label_dsc) label_dsc->opa = LV_OPA_TRANSP;
            if (fill_dsc) fill_dsc->opa = LV_OPA_TRANSP;
            if (border_dsc) border_dsc->opa = LV_OPA_TRANSP;
        } else {
            if (label_dsc) label_dsc->color = st->hdr_text_color;
            if (fill_dsc) fill_dsc->opa = LV_OPA_TRANSP;
            if (border_dsc) {
                border_dsc->color = st->hdr_border_color;
                border_dsc->width = 1;
                border_dsc->side = LV_BORDER_SIDE_BOTTOM;
                border_dsc->opa = LV_OPA_COVER;
            }
        }
        return;
    }

    uint32_t data_row = row - 1;

    lv_draw_fill_dsc_t* fill_dsc = lv_draw_task_get_fill_dsc(draw_task);
#if defined(SOC_PPA_SUPPORTED) && SOC_PPA_SUPPORTED
    // On ESP32-P4 with LV_USE_PPA, per-cell fills from the table draw callback
    // can trigger unaligned ppa_fill paths while scrolling. Keep rows transparent.
    if (fill_dsc) fill_dsc->opa = LV_OPA_TRANSP;
#else
    if (fill_dsc) {
        if (data_row < st->row_count && col < st->col_count && st->cell_bg_set[data_row][col]) {
            fill_dsc->color = st->cell_bg[data_row][col];
        } else if (data_row < st->row_count) {
            fill_dsc->color = st->row_colors[data_row];
        } else {
            fill_dsc->color = st->default_bg;
        }
        fill_dsc->opa = LV_OPA_COVER;
    }
#endif

    lv_draw_label_dsc_t* label_dsc = lv_draw_task_get_label_dsc(draw_task);
    if (label_dsc) {
        if (data_row < st->row_count && col < st->col_count && st->cell_text_set[data_row][col]) {
            label_dsc->color = st->cell_text[data_row][col];
        } else {
            label_dsc->color = st->default_text_color;
        }
    }
}

static void table_parse(const JsonObject& btn, uint8_t* data) {
    auto* cfg = reinterpret_cast<TableWidgetConfig*>(data);
    memset(cfg, 0, sizeof(TableWidgetConfig));

    strlcpy(cfg->binding, btn["widget_data_binding"] | "", sizeof(cfg->binding));
    cfg->scrollable = btn["widget_table_scrollable"] | true;

    const char* style_str = btn["widget_table_style"] | "";
    strlcpy(cfg->table_style, style_str, sizeof(cfg->table_style));
}

static void table_create(lv_obj_t* tile, const WidgetConfig* wcfg,
                         const ScreenButtonConfig* btn,
                         const PadRect* rect, const UIScaleInfo* scale,
                         lv_obj_t* icon_img, lv_obj_t* center_label,
                         WidgetState* state) {
    (void)btn;
    (void)icon_img;
    (void)center_label;

    auto* cfg = reinterpret_cast<const TableWidgetConfig*>(wcfg->data);
    auto* st = (TableWidgetState*)lv_malloc(sizeof(TableWidgetState));
    if (!st) {
        LOGE(TAG, "Failed to allocate TableWidgetState");
        return;
    }
    memset(st, 0, sizeof(TableWidgetState));
    table_set_state(state, st);

    st->default_bg = resolve_lv_color("#12122a", 0x12122a);
    st->default_text_color = resolve_lv_color("#b0b0d0", 0xb0b0d0);
    st->hdr_text_color = resolve_lv_color("#404070", 0x404070);
    st->hdr_border_color = st->hdr_text_color;
    table_reset_colors(st);

    LabelStyle ls = {};
    if (cfg->table_style[0]) {
        label_style_parse(cfg->table_style, &ls);
    }
    const lv_font_t* table_font = pad_resolve_font(ls, scale->font_small);

    lv_obj_t* tbl = lv_table_create(tile);
    st->table = tbl;

    constexpr lv_coord_t k_table_h_inset = 2;
    constexpr lv_coord_t k_table_v_inset = 2;

    lv_coord_t table_w = lv_obj_get_content_width(tile);
    lv_coord_t table_h = lv_obj_get_content_height(tile);
    if (table_w <= 0) table_w = rect->w;
    if (table_h <= 0) table_h = rect->h;

    if (table_w > (k_table_h_inset * 2)) {
        table_w -= (k_table_h_inset * 2);
    }
    if (table_h > (k_table_v_inset * 2)) {
        table_h -= (k_table_v_inset * 2);
    }

#if defined(SOC_PPA_SUPPORTED) && SOC_PPA_SUPPORTED
    // Keep width on a 32px boundary so common 16px strip invalidations remain
    // cache-line friendly on RGB565 (32 * 16 * 2 = 1024 bytes).
    if (table_w > 64) {
        table_w = (table_w / 32) * 32;
    }
#endif

    lv_obj_set_size(tbl, table_w, table_h);
    lv_obj_set_pos(tbl, k_table_h_inset, k_table_v_inset);
    // Keep explicit x/y position; don't set alignment here because it would
    // override the horizontal inset and remove the right-side margin.

    bool allow_scroll = cfg->scrollable;

    if (allow_scroll) {
        lv_obj_add_flag(tbl, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_scroll_dir(tbl, LV_DIR_VER);
        lv_obj_set_scrollbar_mode(tbl, LV_SCROLLBAR_MODE_AUTO);
    } else {
        lv_obj_remove_flag(tbl, LV_OBJ_FLAG_SCROLLABLE);
    }

    lv_obj_set_style_border_width(tbl, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(tbl, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(tbl, LV_OPA_TRANSP, LV_PART_MAIN);

    lv_style_init(&st->cell_style);
    lv_style_set_text_font(&st->cell_style, table_font);
    lv_style_set_border_width(&st->cell_style, 0);
    lv_style_set_pad_top(&st->cell_style, 2);
    lv_style_set_pad_bottom(&st->cell_style, 2);
    lv_style_set_pad_left(&st->cell_style, 3);
    lv_style_set_pad_right(&st->cell_style, 3);
    lv_style_set_bg_color(&st->cell_style, st->default_bg);
    lv_style_set_bg_opa(&st->cell_style,
#if defined(SOC_PPA_SUPPORTED) && SOC_PPA_SUPPORTED
                        LV_OPA_TRANSP
#else
                        LV_OPA_COVER
#endif
    );
    lv_obj_add_style(tbl, &st->cell_style, LV_PART_ITEMS);

    lv_table_set_column_count(tbl, 1);
    lv_table_set_column_width(tbl, 0, table_w);
    lv_table_set_row_count(tbl, 1);
    lv_table_set_cell_value(tbl, 0, 0, "Table");

    lv_obj_add_event_cb(tbl, table_draw_cb, LV_EVENT_DRAW_TASK_ADDED, st);
    lv_obj_add_flag(tbl, LV_OBJ_FLAG_SEND_DRAW_TASK_EVENTS);

    lv_obj_remove_flag(tbl, LV_OBJ_FLAG_CLICKABLE);
    if (allow_scroll) {
        lv_obj_add_flag(tbl, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_flag(tbl, LV_OBJ_FLAG_CLICK_FOCUSABLE);
    }
}

static void table_update(lv_obj_t* tile, const WidgetConfig* wcfg,
                         WidgetState* state, const char* raw_value) {
    (void)tile;
    (void)wcfg;

    auto* st = table_get_state(state);
    if (!st || !st->table) return;

    if (!raw_value || !raw_value[0]) {
        lv_table_set_row_count(st->table, 1);
        st->row_count = 0;
        lv_obj_invalidate(st->table);
        return;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, raw_value);
    if (err) {
        LOGW(TAG, "JSON parse failed: %s", err.c_str());
        lv_table_set_row_count(st->table, 1);
        st->row_count = 0;
        lv_obj_invalidate(st->table);
        return;
    }

    table_reset_colors(st);

    JsonArrayConst rows;
    JsonArrayConst columns;
    JsonObjectConst first_row;

    if (doc.is<JsonObject>()) {
        JsonObjectConst obj = doc.as<JsonObjectConst>();
        columns = obj["columns"].as<JsonArrayConst>();
        rows = obj["rows"].as<JsonArrayConst>();

        lv_color_t color;
        if (table_parse_color_str(obj["header_text_color"], &color)) {
            st->hdr_text_color = color;
            st->hdr_border_color = color;
        }
        if (table_parse_color_str(obj["row_text_color"], &color)) {
            st->default_text_color = color;
        }
        if (table_parse_color_str(obj["default_bg"], &color)) {
            st->default_bg = color;
        }
        st->hide_header = !(obj["show_header"] | true);

        if (!rows.isNull() && rows.size() > 0 && rows[0].is<JsonObjectConst>()) {
            first_row = rows[0].as<JsonObjectConst>();
        }
    } else if (doc.is<JsonArray>()) {
        rows = doc.as<JsonArrayConst>();
        st->hide_header = false;
        if (!rows.isNull() && rows.size() > 0 && rows[0].is<JsonObjectConst>()) {
            first_row = rows[0].as<JsonObjectConst>();
        }
    } else {
        LOGW(TAG, "Unsupported table payload root type");
        lv_table_set_row_count(st->table, 1);
        st->row_count = 0;
        lv_obj_invalidate(st->table);
        return;
    }

    table_apply_columns(st, columns, first_row);

    if (st->col_count == 0) {
        st->row_count = 0;
        lv_obj_invalidate(st->table);
        return;
    }

    uint16_t data_rows_max = rows.size();
    if (data_rows_max > TABLE_MAX_ROWS) data_rows_max = TABLE_MAX_ROWS;

    uint16_t valid_rows = 0;
    for (JsonVariantConst row_var : rows) {
        if (valid_rows >= data_rows_max) break;
        if (!row_var.is<JsonObjectConst>()) continue;
        valid_rows++;
    }

    lv_table_set_row_count(st->table, valid_rows + 1);

    uint16_t r = 0;
    for (JsonVariantConst row_var : rows) {
        if (r >= valid_rows) break;
        if (!row_var.is<JsonObjectConst>()) continue;
        JsonObjectConst row = row_var.as<JsonObjectConst>();

        lv_color_t row_bg = st->default_bg;
        table_parse_color_str(row["_bg"], &row_bg);
        st->row_colors[r] = row_bg;

        for (uint8_t c = 0; c < st->col_count; c++) {
            table_set_cell(st, r, c, row[st->col_keys[c]]);
        }

        r++;
    }

    st->row_count = r;

    lv_obj_invalidate(st->table);
}

static void table_destroy(WidgetState* state) {
    auto* st = table_get_state(state);
    if (st) {
        lv_style_reset(&st->cell_style);
        lv_free(st);
        table_set_state(state, nullptr);
    }
}

static void table_tick(lv_obj_t* tile, const WidgetConfig* cfg, WidgetState* state) {
    auto* st = table_get_state(state);
    if (!st || !st->table) return;

    auto* tcfg = reinterpret_cast<const TableWidgetConfig*>(cfg->data);
    char payload[TABLE_PAYLOAD_MAX];
    if (!table_resolve_binding(tcfg, payload, sizeof(payload))) return;

    uint32_t hash = table_hash_str(payload);
    if (hash == st->last_payload_hash) return;

    st->last_payload_hash = hash;
    table_update(tile, cfg, state, payload);
}

REGISTER_WIDGET(table, nullptr, true);

#endif // HAS_DISPLAY
