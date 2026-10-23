// ============================================================================
// pad_validate.cpp — shared pad JSON validator (see pad_validate.h).
//
// Compiled directly (sketch root). Extracted from mcp_tools_pads.cpp so the web
// portal save path and the MCP tools validate through identical logic (no
// drift). Gated HAS_DISPLAY — pads require a display; independent of HAS_MCP.
// ============================================================================

#include "pad_validate.h"

#if HAS_DISPLAY

#include "pad_config.h"
#include "display_geometry.h"
#include "pad_track_layout.h"
#include "psram_json_allocator.h"
#include "widgets/widget_registry.h"
#include "action_validate.h"
#if HAS_IMAGE_LIBRARY
#include "image_library.h"
#endif

#include <ctype.h>
#include <stdio.h>
#include <string.h>

uint32_t pad_icon_render_key(JsonObjectConst pad, JsonObjectConst button) {
    uint32_t hash = 2166136261u;
    const auto text = [&hash](const char* value) {
        do {
            hash ^= (uint8_t)*value;
            hash *= 16777619u;
        } while (*value++);
    };
    const auto number = [&text](int value) {
        char buffer[16];
        snprintf(buffer, sizeof(buffer), "%d", value);
        text(buffer);
    };
    const int cols = pad["cols"] | 3;
    const int rows = pad["rows"] | 3;
    number(cols);
    number(rows);
    number(button["col"] | 0);
    number(button["row"] | 0);
    number(button["col_span"] | 1);
    number(button["row_span"] | 1);
    number(button["icon_scale_pct"] | 0);
    number((button["label_top"] | "")[0] ? 1 : 0);
    number((button["label_bottom"] | "")[0] ? 1 : 0);
    const char* fields[] = {"border_width", "content_pad", "widget_type"};
    for (const char* field : fields) {
        JsonVariantConst value = button[field];
        if (value.isNull()) text("");
        else if (value.is<const char*>()) text(value.as<const char*>());
        else {
            char buffer[32];
            serializeJson(value, buffer, sizeof(buffer));
            text(buffer);
        }
    }
    for (int index = 0; index < cols && index < MAX_GRID_COLS; ++index)
        number(pad["column_widths_px"][index] | 0);
    for (int index = 0; index < rows && index < MAX_GRID_ROWS; ++index)
        number(pad["row_heights_px"][index] | 0);
    return hash ? hash : 1;
}

bool pad_icon_source_id_valid(const char* source_id) {
    if (!source_id || strlen(source_id) != 26 || source_id[0] != 's' || source_id[1] != '_')
        return false;
    for (size_t index = 2; index < 26; ++index) {
        const char value = source_id[index];
        if (!((value >= '0' && value <= '9') || (value >= 'a' && value <= 'f')))
            return false;
    }
    return true;
}

bool pad_icon_catalog_kind_valid(const char* catalog_id, const char* kind) {
    if (!catalog_id || !kind) return false;
    const size_t length = strlen(catalog_id);
    if (!length || length > 128) return false;

    const char* name = nullptr;
    bool material = false;
    if (strncmp(catalog_id, "material-symbols:", 17) == 0) {
        name = catalog_id + 17;
        material = true;
    } else if (strncmp(catalog_id, "noto:", 5) == 0) {
        name = catalog_id + 5;
    } else {
        return false;
    }
    if (!name[0]) return false;
    for (const char* cursor = name; *cursor; ++cursor) {
        if (!((*cursor >= 'a' && *cursor <= 'z') ||
              (*cursor >= '0' && *cursor <= '9') || *cursor == '-'))
            return false;
    }
    return strcmp(kind, material ? "mono" : "color") == 0;
}

// A color is valid if it parses as hex or carries a binding template ([..]).
static bool color_ok(const char* s) {
    if (!s || !s[0]) return true;  // empty => default
    uint32_t tmp;
    if (parse_hex_color(s, &tmp)) return true;
    return strchr(s, '[') != nullptr;  // binding template
}

static const char* validate_icon(JsonObjectConst button) {
    if (button.containsKey("icon_id")) return "icon_id is no longer supported; use icon";
    if (!button.containsKey("icon")) return nullptr;
    if (!button["icon"].is<JsonObjectConst>()) return "icon must be an object";

    JsonObjectConst icon = button["icon"].as<JsonObjectConst>();
    const char* source_id = icon["source_id"] | "";
    if (!pad_icon_source_id_valid(source_id))
        return "icon source_id must be s_ followed by 24 lowercase hexadecimal digits";

    const char* catalog_id = icon["catalog_id"] | "";
    const char* kind = icon["kind"] | "";
    if (!pad_icon_catalog_kind_valid(catalog_id, kind))
        return "icon catalog_id or kind is invalid";
    return nullptr;
}

// Return `msg` if b[key] would be truncated on store (>= cap, since strlcpy
// needs room for the NUL). Fields that hold binding templates can overflow when
// an LLM inlines a long [expr:..]/[mqtt:..]; the message steers it to factor the
// expression into a reusable pad-level [pad:name] binding.
static const char* check_max(JsonObjectConst b, const char* key, size_t cap, const char* msg) {
    const char* v = b[key] | "";
    return (strlen(v) >= cap) ? msg : nullptr;
}
static const char* LEN_MSG_LABEL = "field too long (max 191 chars); factor long binding expressions into a pad-level [pad:name] binding and reference it";
static const char* LEN_MSG_SHORT = "field too long (max 63 chars)";
// Single in-flight validation (web task), so a static buffer for a formatted
// validation message is safe (and the validator returns const char*).
static char s_len_err[96];

static const char* validate_track_sizes(JsonObjectConst pad, const char* field,
                                        uint8_t count, uint16_t safe_size,
                                        uint16_t gap) {
    if (!pad.containsKey(field)) return nullptr;
    JsonArrayConst values = pad[field].as<JsonArrayConst>();
    if (values.isNull()) return "track sizes must be an array";
    if (values.size() != count) return "track size count must match grid dimensions";

    const uint8_t track_capacity = MAX_GRID_COLS > MAX_GRID_ROWS
        ? MAX_GRID_COLS : MAX_GRID_ROWS;
    uint16_t overrides[track_capacity] = {};
    uint16_t computed[track_capacity] = {};
    bool has_auto = false;
    for (uint8_t track = 0; track < count; track++) {
        JsonVariantConst value = values[track];
        if (!value.is<uint32_t>()) return "track sizes must be positive whole pixels or 0 for Auto";
        const uint32_t size = value.as<uint32_t>();
        if (size > UINT16_MAX) return "track size exceeds supported pixel range";
        overrides[track] = (uint16_t)size;
        if (!size) has_auto = true;
    }
    if (!has_auto) return "track sizes must leave at least one Auto position";
    if (!pad_compute_axis_track_sizes(count, safe_size, gap, overrides, computed))
        return "track sizes leave less than one pixel for an Auto position";
    return nullptr;
}

static const char* validate_exact_binding_token(const char* value) {
    if (!value || value[0] != '[') return "timer value binding must use [scheme:params]";
    const char* scheme = value + 1;
    const char* colon = scheme;
    while ((*colon >= 'a' && *colon <= 'z')
            || (*colon >= 'A' && *colon <= 'Z')) colon++;
    if (colon == scheme || *colon != ':') {
        return "timer value binding must use [scheme:params]";
    }
    int depth = 1;
    const char* cursor = colon + 1;
    for (; *cursor && depth > 0; cursor++) {
        if (*cursor == '[') depth++;
        else if (*cursor == ']') depth--;
    }
    if (depth != 0) return "timer value binding has an unclosed bracket";
    if (*cursor) return "timer value must be one complete binding token";
    return action_validate_binding_tokens(value);
}

static const char* validate_action_constraints(JsonObjectConst action) {
    const char* type = action["type"] | "";
    if (strcmp(type, ACTION_TYPE_VISUAL_ALERT) == 0) {
        const char* color = action["color"] | "";
        if (strlen(color) >= CONFIG_BINDABLE_SHORT_LEN) return "visual_alert color too long (max 63 chars)";
        if (!color_ok(color)) return "visual_alert color must be #RRGGBB or a binding";
    }
    if (strcmp(type, ACTION_TYPE_TIMER) == 0) {
        if (strlen(action["timer_command"] | "") >= CONFIG_TIMER_CMD_MAX_LEN) return "timer command too long";
        if (strlen(action["timer_mode"] | "") >= sizeof(((TimerPayload*)nullptr)->timer_mode)) return "timer mode too long";
        const char* value = action["timer_value"] | "";
        if (strlen(value) >= CONFIG_VALUE_MAX_LEN) return "timer value too long";
        if (strchr(value, '[')) return validate_exact_binding_token(value);
    }
    return nullptr;
}

static const char* validate_action_array(JsonArrayConst arr, bool require_known_type = true) {
    if (arr.size() > MAX_BUTTON_ACTIONS) return "too many actions (max 3)";
    for (JsonObjectConst a : arr) {
        const char* action_error = action_validate_json(a, require_known_type);
        if (action_error) return action_error;
        const char* constraint_error = validate_action_constraints(a);
        if (constraint_error) return constraint_error;
    }
    return nullptr;
}

static const char* validate_button(JsonObjectConst b, int cols, int rows, bool tolerate_offgrid) {
    int col = b["col"] | 0;
    int row = b["row"] | 0;
    int cspan = b["col_span"] | 1;
    int rspan = b["row_span"] | 1;
    if (col < 0 || row < 0) return "button col/row negative";
    if (cspan < 1 || rspan < 1) return "span must be >= 1";
    if (!tolerate_offgrid) {
        if (col + cspan > cols) return "button overflows grid columns";
        if (row + rspan > rows) return "button overflows grid rows";
    }
    const char* icon_error = validate_icon(b);
    if (icon_error) return icon_error;
    if (!color_ok(b["bg_color"] | "")) return "bad bg_color";
    if (!color_ok(b["fg_color"] | "")) return "bad fg_color";
    if (!color_ok(b["border_color"] | "")) return "bad border_color";
    const char* fill_colors[] = {"bg_pattern_color", "widget_bar_pattern_color", "widget_bar_pattern_color_2",
        "widget_bar_pattern_color_3", "widget_bar_pattern_color_4", "widget_bar_bg_pattern_color", "widget_track_outline_color"};
    for (unsigned index = 0; index < 7; ++index) {
        const char* color = b[fill_colors[index]] | "";
        const size_t capacity = index < 5 ? CONFIG_COLOR_MAX_LEN : CONFIG_BINDABLE_SHORT_LEN;
        if (strlen(color) >= capacity) return index < 5 ? LEN_MSG_LABEL : LEN_MSG_SHORT;
        if (!color_ok(color)) return "pattern/outline color must be hex or a binding";
    }
    const char* shadow = b["button_shadow"] | "";
    if (shadow[0] && strcmp(shadow, "enabled") != 0 && strcmp(shadow, "disabled") != 0)
        return "bad button_shadow";
    const char* wt = b["widget_type"] | "";
    const WidgetType* wtype = wt[0] ? widget_find(wt) : nullptr;
    if (wt[0] && !wtype) return "unknown widget type";
    if (b.containsKey("widget_disable_pad_swipes")) {
        if (!widget_supports_pad_swipe_control(wt))
            return "widget_disable_pad_swipes is only supported on mousepad, scrollpad, and gamepad widgets";
        if (!b["widget_disable_pad_swipes"].is<bool>())
            return "widget_disable_pad_swipes must be boolean";
    }
    if (wtype && wtype->validateConfig) {
        const char* widget_error = wtype->validateConfig(b);
        if (widget_error) return widget_error;
    }
    // Widget config field length limits, enforced from the widget's own
    // describeSchema (single source): each field may declare its own "max"
    // (e.g. caption fields are 63, others differ or declare none). Only fields
    // that declare a "max" are length-checked, so an over-long value is rejected
    // here instead of silently truncating on the device.
    if (wtype && wtype->describeSchema) {
        BasicJsonDocument<PsramJsonAllocator> sd(4096);
        JsonObject so = sd.to<JsonObject>();
        wtype->describeSchema(so);
        JsonArrayConst cf = so["config_fields"];
        for (JsonObjectConst f : cf) {
            const char* fname = f["name"] | "";
            if (!fname[0]) continue;
            // Action-typed fields (e.g. numericrocker's adjust action) are nested
            // action OBJECTS, not strings. Catch the common LLM mistake of passing
            // a bare string/number here so validate_pad rejects it up front,
            // instead of the device silently ignoring it at render time.
            if (strcmp(f["type"] | "", "action") == 0) {
                JsonVariantConst av = b[fname];
                if (!av.isNull()) {
                    if (!av.is<JsonObjectConst>()) {
                        snprintf(s_len_err, sizeof(s_len_err),
                                 "widget field '%s' must be a nested action object {type,...}, not a bare value", fname);
                        return s_len_err;
                    }
                    const char* at = av["type"] | "";
                    if (!at[0]) {
                        snprintf(s_len_err, sizeof(s_len_err),
                                 "widget field '%s' action is missing 'type'", fname);
                        return s_len_err;
                    }
                    const char* action_error = action_validate_json(av.as<JsonObjectConst>());
                    if (action_error) return action_error;
                }
                continue;  // action fields carry no length cap
            }
            int mx = f["max"] | 0;
            if (mx <= 0) continue;
            const char* fname_val = b[fname] | "";
            if ((int)strlen(fname_val) > mx) {
                snprintf(s_len_err, sizeof(s_len_err),
                         "widget field '%s' too long (max %d); factor a long binding into a [pad:name] binding",
                         fname, mx);
                return s_len_err;
            }
        }
    }
    // Length limits (truncation guard). Labels/bindings/state: 192-byte buffers;
    // border/radius: 64-byte; colors: 192-byte.
    const char* e;
    if ((e = check_max(b, "label_top",    CONFIG_LABEL_MAX_LEN, LEN_MSG_LABEL))) return e;
    if ((e = check_max(b, "label_center", CONFIG_LABEL_MAX_LEN, LEN_MSG_LABEL))) return e;
    if ((e = check_max(b, "label_bottom", CONFIG_LABEL_MAX_LEN, LEN_MSG_LABEL))) return e;
    if ((e = check_max(b, "btn_state",    CONFIG_BTN_STATE_MAX_LEN, LEN_MSG_LABEL))) return e;
    if ((e = check_max(b, "bg_color",     CONFIG_COLOR_MAX_LEN, LEN_MSG_LABEL))) return e;
    if ((e = check_max(b, "fg_color",     CONFIG_COLOR_MAX_LEN, LEN_MSG_LABEL))) return e;
    if ((e = check_max(b, "border_color", CONFIG_COLOR_MAX_LEN, LEN_MSG_LABEL))) return e;
    if ((e = check_max(b, "border_width", CONFIG_BINDABLE_SHORT_LEN, LEN_MSG_SHORT))) return e;
    if ((e = check_max(b, "corner_radius",CONFIG_BINDABLE_SHORT_LEN, LEN_MSG_SHORT))) return e;
    if ((e = check_max(b, "content_pad",  CONFIG_BINDABLE_SHORT_LEN, LEN_MSG_SHORT))) return e;
    if ((e = check_max(b, "bg_image_path", CONFIG_BG_IMAGE_PATH_MAX_LEN,
                       "bg_image_path too long (max 191 chars)"))) return e;
#if HAS_IMAGE_LIBRARY
    const char* bg_image_path = b["bg_image_path"] | "";
    if (bg_image_path[0] && !strchr(bg_image_path, '[') &&
        !ImageLibraryCatalog::is_image_path(bg_image_path)) {
        return "bg_image_path must be a canonical image library path or binding";
    }
#endif
    if ((e = check_max(b, "widget_data_binding",   CONFIG_LABEL_MAX_LEN, LEN_MSG_LABEL))) return e;
    if ((e = check_max(b, "widget_data_binding_2", CONFIG_LABEL_MAX_LEN, LEN_MSG_LABEL))) return e;
    if ((e = check_max(b, "widget_data_binding_3", CONFIG_LABEL_MAX_LEN, LEN_MSG_LABEL))) return e;
    if ((e = check_max(b, "widget_data_binding_4", CONFIG_LABEL_MAX_LEN, LEN_MSG_LABEL))) return e;
    if (b.containsKey("extension_config") && !b["extension_config"].is<const char*>()) return "extension_config must be a string";
    if ((e = check_max(b, "extension_config", CONFIG_EXTENSION_CONFIG_MAX_LEN, "extension_config is too long"))) return e;
    if (b.containsKey("extension_upscale")) {
        if (!b["extension_upscale"].is<int>() || b["extension_upscale"].as<int>() < 1 ||
            b["extension_upscale"].as<int>() > 4) return "extension_upscale must be an integer from 1 to 4";
    }
    if (b["actions"].is<JsonArrayConst>()) { const char* ae = validate_action_array(b["actions"]); if (ae) return ae; }
    if (b["lp_actions"].is<JsonArrayConst>()) { const char* ae = validate_action_array(b["lp_actions"]); if (ae) return ae; }
    // Reject invalid binding tokens (unknown scheme, bad health key / list
    // provider / timer id) in any string field (labels, widget bindings...).
    for (JsonPairConst kv : b) {
        if (strcmp(kv.key().c_str(), "extension_config") == 0) continue;
        if (kv.value().is<const char*>()) {
            const char* he = action_validate_binding_tokens(kv.value().as<const char*>());
            if (he) return he;
        }
    }
    return nullptr;
}

static const char* validate_button_layer(JsonArrayConst buttons, int cols, int rows,
                                         bool tolerate_offgrid) {
    for (size_t index = 0; index < buttons.size(); ++index) {
        JsonObjectConst button = buttons[index];
        const char* error = validate_button(button, cols, rows, tolerate_offgrid);
        if (error) return error;

        const int col = button["col"] | 0;
        const int row = button["row"] | 0;
        const int col_span = button["col_span"] | 1;
        const int row_span = button["row_span"] | 1;
        for (size_t prior = 0; prior < index; ++prior) {
            JsonObjectConst other = buttons[prior];
            const int other_col = other["col"] | 0;
            const int other_row = other["row"] | 0;
            const int other_col_span = other["col_span"] | 1;
            const int other_row_span = other["row_span"] | 1;
            if (col < other_col + other_col_span &&
                other_col < col + col_span &&
                row < other_row + other_row_span &&
                other_row < row + row_span) {
                return "buttons overlap within a layer";
            }
        }
    }
    return nullptr;
}

// Validate a complete pad JSON (grid + buttons + collisions). nullptr = ok.
const char* pad_validate(JsonObjectConst pad, bool tolerate_offgrid,
                         const PadValidationGeometry* geometry) {
    const char* shadow = pad["button_shadow"] | "";
    if (shadow[0] && strcmp(shadow, "enabled") != 0 && strcmp(shadow, "disabled") != 0)
        return "bad button_shadow";
    const char* layout = pad["layout"] | "grid";
    int cols = pad["cols"] | 0;
    int rows = pad["rows"] | 0;
    bool is_grid = strcmp(layout, "grid") == 0;
    if (is_grid) {
        if (cols < 1 || cols > MAX_GRID_COLS) return "cols must be 1-8";
        if (rows < 1 || rows > MAX_GRID_ROWS) return "rows must be 1-8";
    } else {
        if (pad.containsKey("column_widths_px") || pad.containsKey("row_heights_px"))
            return "track sizes are only supported for grid layouts";
        cols = MAX_GRID_COLS; rows = MAX_GRID_ROWS;  // skip overflow checks for curated
    }
    if (is_grid && (pad.containsKey("column_widths_px") || pad.containsKey("row_heights_px"))) {
        uint16_t safe_width = UINT16_MAX;
        uint16_t safe_height = UINT16_MAX;
        uint16_t gap_x = 0;
        uint16_t gap_y = 0;
        if (geometry) {
            const DisplayContentRect content = display_safe_content_rect(
                geometry->display_width, geometry->display_height);
            const uint32_t horizontal_inset = 2U * geometry->pixel_shift_distance_px +
                geometry->inset_left_px + geometry->inset_right_px;
            const uint32_t vertical_inset = 2U * geometry->pixel_shift_distance_px +
                geometry->inset_top_px + geometry->inset_bottom_px;
            safe_width = content.w > horizontal_inset ? content.w - horizontal_inset : 0;
            safe_height = content.h > vertical_inset ? content.h - vertical_inset : 0;
            gap_x = gap_y = geometry->spacing_px;
        }
        const char* track_error = validate_track_sizes(
            pad, "column_widths_px", cols, safe_width, gap_x);
        if (track_error) return track_error;
        track_error = validate_track_sizes(
            pad, "row_heights_px", rows, safe_height, gap_y);
        if (track_error) return track_error;
    }
    if (pad.containsKey("template_pad")) {
        int tp = pad["template_pad"] | -1;
        if (tp < -1 || tp >= MAX_PADS) return "template_pad out of range";
    }
    if (pad.containsKey("pad_actions")) {
        if (!pad["pad_actions"].is<JsonArrayConst>()) return "pad_actions must be an array";
        const char* action_error = validate_action_array(
            pad["pad_actions"].as<JsonArrayConst>(), true);
        if (action_error) return action_error;
    }
    if (pad["bindings"].size() > PAD_MAX_BINDINGS) return "too many bindings";
    // Pad-level binding name/value length limits.
    JsonObjectConst pbind = pad["bindings"];
    if (!pbind.isNull()) {
        for (JsonPairConst kv : pbind) {
            if (strlen(kv.key().c_str()) >= PAD_BINDING_NAME_MAX_LEN) return "binding name too long (max 31 chars)";
            const char* val = kv.value().as<const char*>();
            if (val && strlen(val) >= CONFIG_LABEL_MAX_LEN) return "binding value too long (max 191 chars)";
            // A pad binding's value may not itself contain a [pad:...] token: the
            // engine refuses to resolve one pad binding through another (guards
            // against recursion) and renders blank. Catch it here instead of
            // letting it fail silently at render time. Inline the underlying
            // binding (or reference the base pad binding directly from the field).
            if (val && strstr(val, "[pad:")) return "pad binding value cannot reference another [pad:...] binding (renders blank); inline the underlying binding instead";
            const char* be = action_validate_binding_tokens(val);
            if (be) return be;
        }
    }
    JsonArrayConst btns = pad["buttons"].as<JsonArrayConst>();
    JsonArrayConst overlays;
    if (pad.containsKey("buttons") && btns.isNull()) return "buttons must be an array";
    if (pad.containsKey("overlay_buttons")) {
        overlays = pad["overlay_buttons"].as<JsonArrayConst>();
        if (overlays.isNull()) return "overlay_buttons must be an array";
    }
    if (btns.size() + overlays.size() > MAX_PAD_BUTTONS) return "too many buttons across layers";
    const char* button_error = validate_button_layer(btns, cols, rows, tolerate_offgrid);
    if (button_error) return button_error;
    button_error = validate_button_layer(overlays, cols, rows, tolerate_offgrid);
    if (button_error) return button_error;
    return nullptr;
}

#endif // HAS_DISPLAY
