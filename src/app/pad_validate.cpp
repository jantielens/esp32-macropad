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
#include "psram_json_allocator.h"
#include "widgets/widget_registry.h"
#include "binding_template.h"
#include "action_registry.h"
#include "music_command.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

// A color is valid if it parses as hex or carries a binding template ([..]).
static bool color_ok(const char* s) {
    if (!s || !s[0]) return true;  // empty => default
    uint32_t tmp;
    if (parse_hex_color(s, &tmp)) return true;
    return strchr(s, '[') != nullptr;  // binding template
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
static const char* LEN_MSG_CONFIRM = "confirm_text too long (max 127 chars)";
// Single in-flight validation (web task), so a static buffer for a formatted
// validation message is safe (and the validator returns const char*).
static char s_len_err[96];

// Validate every [scheme:params] token in a string: the scheme must be a
// registered binding scheme, and (if that scheme provides a validate hook) its
// params must pass. This is generic across all schemes — health keys, list
// providers, timer ids, etc. are each checked by the scheme's own hook (open
// schemes like mqtt/expr/time have no hook and accept anything). nullptr = ok.
static const char* validate_binding_tokens(const char* s) {
    if (!s) return nullptr;
    const char* p = s;
    while ((p = strchr(p, '[')) != nullptr) {
        const char* scheme = p + 1;
        const char* c = scheme;
        while ((*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z')) c++;
        if (*c != ':' || c == scheme) { p++; continue; }  // not a [scheme:...] token
        size_t schemelen = (size_t)(c - scheme);
        if (!binding_template_scheme_known(scheme, schemelen)) {
            char sbuf[16];
            size_t nn = schemelen < sizeof(sbuf) - 1 ? schemelen : sizeof(sbuf) - 1;
            memcpy(sbuf, scheme, nn); sbuf[nn] = '\0';
            snprintf(s_len_err, sizeof(s_len_err), "unknown binding scheme '%s'", sbuf);
            return s_len_err;
        }
        // Simple params (up to ; ] |) for the scheme's validate hook. Schemes with
        // nested params (expr) have no hook, so this approximation is unused there.
        const char* pp = c + 1;
        char pbuf[48];
        size_t pn = 0;
        while (*pp && *pp != ';' && *pp != ']' && *pp != '|' && pn < sizeof(pbuf) - 1) pbuf[pn++] = *pp++;
        pbuf[pn] = '\0';
        const char* e = binding_template_validate_params(scheme, schemelen, pbuf);
        if (e) return e;
        p++;  // keep scanning (nested tokens too)
    }
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
    return validate_binding_tokens(value);
}

// Validate action-specific authoring contracts. Other action types are a no-op.
static const char* validate_action(JsonObjectConst action) {
    const char* type = action["type"] | "";
    if (strcmp(type, ACTION_TYPE_MUSIC) == 0) {
#if HAS_SOUND_PLAYER
        if (!action.containsKey("music_command")) return "music missing music_command";
        if (!action["music_command"].is<const char*>()) return "music_command must be a string";
        MusicCommand command;
        return music_command_parse(action["music_command"].as<const char*>(), &command)
            ? nullptr : "music_command must be play_pause, next, previous, or stop";
#else
        return "music is unavailable on this board";
#endif
    }
    if (strcmp(type, ACTION_TYPE_CYCLE_PAD) == 0) {
        if (action.containsKey("direction")) {
            if (!action["direction"].is<const char*>()) {
                return "cycle_pad direction must be a string";
            }
            const char* direction = action["direction"].as<const char*>();
            if (strcmp(direction, "next") != 0 && strcmp(direction, "previous") != 0) {
                return "cycle_pad direction must be 'next' or 'previous'";
            }
        }
        if (action.containsKey("wrap") && !action["wrap"].is<bool>()) {
            return "cycle_pad wrap must be boolean";
        }
        if (action.containsKey("excluded_pads")
                && !action["excluded_pads"].is<const char*>()) {
            return "cycle_pad excluded_pads must be a string";
        }
        return nullptr;
    }
    if (strcmp(type, ACTION_TYPE_HA_SERVICE) != 0) return nullptr;

    if (!action.containsKey("entity_id")) return "ha_service missing entity_id";
    if (!action["entity_id"].is<const char*>()) return "ha_service entity_id must be a string";
    const char* entity_id = action["entity_id"].as<const char*>();
    if (!entity_id[0]) return "ha_service entity_id must not be empty";
    if (strlen(entity_id) >= sizeof(((HaServicePayload*)nullptr)->entity_id)) {
        return "ha_service entity_id too long";
    }
    const char* separator = strchr(entity_id, '.');
    if (!separator || separator == entity_id || !separator[1] || strchr(separator + 1, '.')) {
        return "ha_service entity_id must have nonempty domain and object portions";
    }
    for (const char* cursor = entity_id; *cursor; ++cursor) {
        if (isspace((unsigned char)*cursor)) return "ha_service entity_id must not contain whitespace";
    }

    if (!action.containsKey("service")) return "ha_service missing service";
    if (!action["service"].is<const char*>()) return "ha_service service must be a string";
    const char* service = action["service"].as<const char*>();
    if (!service[0]) return "ha_service service must not be empty";
    const char* service_separator = strrchr(service, '.');
    if (service_separator) {
        snprintf(s_len_err, sizeof(s_len_err), "service must be bare; use '%s'", service_separator + 1);
        return s_len_err;
    }
    if (strlen(service) >= sizeof(((HaServicePayload*)nullptr)->service)) {
        return "ha_service service too long";
    }
    for (const char* cursor = service; *cursor; ++cursor) {
        unsigned char character = (unsigned char)*cursor;
        if (!islower(character) && !isdigit(character) && character != '_') {
            return "ha_service service must contain only lowercase letters, digits, and '_'";
        }
    }

    if (!action.containsKey("data_json")) return nullptr;
    if (!action["data_json"].is<const char*>()) return "ha_service data_json must be a string";
    const char* data_json = action["data_json"].as<const char*>();
    if (!data_json[0]) return nullptr;
    if (strlen(data_json) >= sizeof(((HaServicePayload*)nullptr)->data_json)) {
        return "ha_service data_json too long";
    }
    JsonDocument data;
    if (deserializeJson(data, data_json)) return "ha_service data_json must contain valid JSON";
    if (!data.is<JsonObjectConst>()) return "ha_service data_json root must be an object";
    return nullptr;
}

static bool action_type_known(const char* type) {
    if (!type || !type[0] || strcmp(type, "none") == 0) return true;
    static const char* const builtins[] = {
        ACTION_TYPE_SCREEN, ACTION_TYPE_MQTT, ACTION_TYPE_BACK,
        ACTION_TYPE_KEY, ACTION_TYPE_BLE_PAIR, ACTION_TYPE_MUSIC,
        ACTION_TYPE_VOLUME, ACTION_TYPE_BRIGHTNESS, ACTION_TYPE_TIMER,
        ACTION_TYPE_SOUND_ALERT, ACTION_TYPE_NOTIFY, ACTION_TYPE_SYSTEM,
        ACTION_TYPE_HA_SERVICE, ACTION_TYPE_VISUAL_ALERT, ACTION_TYPE_CYCLE_PAD,
    };
    for (const char* builtin : builtins) {
        if (strcmp(type, builtin) == 0) return true;
    }
    return action_type_find(type) != nullptr;
}

static const char* validate_action_array(JsonArrayConst arr, bool require_known_type = true) {
    if (arr.size() > MAX_BUTTON_ACTIONS) return "too many actions (max 3)";
    for (JsonObjectConst a : arr) {
        const char* t = a["type"] | "";
        if (!t[0]) return "action missing type";
        if (require_known_type && !action_type_known(t)) return "unknown action type";
        const char* action_error = validate_action(a);
        if (action_error) return action_error;
        // visual_alert.color is bindable and stored in a CONFIG_BINDABLE_SHORT_LEN
        // buffer; reject unknown schemes / non-color values and over-long tokens
        // up front (they would otherwise truncate on save and fall back to red).
        if (strcmp(t, ACTION_TYPE_VISUAL_ALERT) == 0) {
            const char* color = a["color"] | "";
            if (strlen(color) >= CONFIG_BINDABLE_SHORT_LEN) return "visual_alert color too long (max 63 chars)";
            if (!color_ok(color)) return "visual_alert color must be #RRGGBB or a binding";
            const char* e = validate_binding_tokens(color);
            if (e) return e;
        }
        if (strcmp(t, ACTION_TYPE_TIMER) == 0) {
            if (strlen(a["timer_command"] | "") >= CONFIG_TIMER_CMD_MAX_LEN) {
                return "timer command too long";
            }
                if (strlen(a["timer_mode"] | "")
                    >= sizeof(((TimerPayload*)nullptr)->timer_mode)) {
                return "timer mode too long";
            }
            if (strlen(a["timer_value"] | "") >= CONFIG_VALUE_MAX_LEN) {
                return "timer value too long";
            }
            const char* value = a["timer_value"] | "";
            if (strchr(value, '[')) {
                const char* error = validate_exact_binding_token(value);
                if (error) return error;
            }
        }
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
    if (!color_ok(b["bg_color"] | "")) return "bad bg_color";
    if (!color_ok(b["fg_color"] | "")) return "bad fg_color";
    if (!color_ok(b["border_color"] | "")) return "bad border_color";
    const char* wt = b["widget_type"] | "";
    const WidgetType* wtype = wt[0] ? widget_find(wt) : nullptr;
    if (wt[0] && !wtype) return "unknown widget type";
    if (wt[0] && (b["confirm"] | false)) return "confirm is only supported on normal buttons";
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
                    const char* action_error = validate_action(av.as<JsonObjectConst>());
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
    if (b.containsKey("confirm_text") && !b["confirm_text"].is<const char*>()) return "confirm_text must be a string";
    if ((e = check_max(b, "confirm_text", CONFIG_CONFIRM_TEXT_MAX_LEN, LEN_MSG_CONFIRM))) return e;
    if (b.containsKey("confirm") && !b["confirm"].is<bool>()) return "confirm must be boolean";
    if ((e = check_max(b, "widget_data_binding",   CONFIG_LABEL_MAX_LEN, LEN_MSG_LABEL))) return e;
    if ((e = check_max(b, "widget_data_binding_2", CONFIG_LABEL_MAX_LEN, LEN_MSG_LABEL))) return e;
    if ((e = check_max(b, "widget_data_binding_3", CONFIG_LABEL_MAX_LEN, LEN_MSG_LABEL))) return e;
    if ((e = check_max(b, "widget_data_binding_4", CONFIG_LABEL_MAX_LEN, LEN_MSG_LABEL))) return e;
    if (b["actions"].is<JsonArrayConst>()) { const char* ae = validate_action_array(b["actions"]); if (ae) return ae; }
    if (b["lp_actions"].is<JsonArrayConst>()) { const char* ae = validate_action_array(b["lp_actions"]); if (ae) return ae; }
    // Reject invalid binding tokens (unknown scheme, bad health key / list
    // provider / timer id) in any string field (labels, widget bindings...).
    for (JsonPairConst kv : b) {
        if (kv.value().is<const char*>()) {
            const char* he = validate_binding_tokens(kv.value().as<const char*>());
            if (he) return he;
        }
    }
    return nullptr;
}

// Validate a complete pad JSON (grid + buttons + collisions). nullptr = ok.
const char* pad_validate(JsonObjectConst pad, bool tolerate_offgrid) {
    const char* layout = pad["layout"] | "grid";
    int cols = pad["cols"] | 0;
    int rows = pad["rows"] | 0;
    bool is_grid = strcmp(layout, "grid") == 0;
    if (is_grid) {
        if (cols < 1 || cols > MAX_GRID_COLS) return "cols must be 1-8";
        if (rows < 1 || rows > MAX_GRID_ROWS) return "rows must be 1-8";
    } else {
        cols = MAX_GRID_COLS; rows = MAX_GRID_ROWS;  // skip overflow checks for curated
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
            const char* be = validate_binding_tokens(val);
            if (be) return be;
        }
    }
    JsonArrayConst btns = pad["buttons"];
    if (btns.isNull()) return nullptr;
    if (btns.size() > MAX_PAD_BUTTONS) return "too many buttons";
    // Per-button checks. NOTE: grid-cell overlaps are intentionally NOT rejected
    // — the firmware (and the portal save path) tolerate them (a later button
    // renders over / hides an earlier one). Rejecting overlaps here was stricter
    // than the device and broke incremental set_buttons edits.
    for (JsonObjectConst b : btns) {
        const char* e = validate_button(b, cols, rows, tolerate_offgrid);
        if (e) return e;
    }
    return nullptr;
}

#endif // HAS_DISPLAY
