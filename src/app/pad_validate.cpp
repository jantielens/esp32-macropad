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
#include "widgets/widget.h"
#include "binding_template.h"

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

static const char* validate_action_array(JsonArrayConst arr) {
    if (arr.size() > MAX_BUTTON_ACTIONS) return "too many actions (max 3)";
    for (JsonObjectConst a : arr) {
        const char* t = a["type"] | "";
        if (!t[0]) return "action missing type";
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
