// ============================================================================
// mcp_tools_pads.cpp — MCP pad capability manifest + authoring (write) tools.
//
// Aggregated into the build via mcp_components.cpp (arduino-cli only compiles
// .cpp in the sketch root). Gated HAS_MCP && HAS_DISPLAY.
//
//   get_capabilities  — read tool (bearer token only). Manifest is generated:
//                       widget list + per-widget fields come from the registry
//                       (widget_at + describeSchema); no hand-maintained table.
//   validate_pad      — read tool: dry-run validate a pad JSON, no save.
//   set_button / set_buttons / remove_button / clear_pad — authoring tools.
//
// Authoring tools require mcp_authoring_enabled (gated by web_mcp via the
// requires_authoring flag). They run on the web task: load the pad raw, splice
// the button(s) into the JSON, VALIDATE, then hand a PSRAM buffer (pointer+len)
// to the main loop via mcp_control_dispatch (D6) which calls pad_config_save_raw
// + pad_config_rebuild_all_caches and frees it. The 256-byte ctx is never
// widened and the pad JSON is never inlined into it. save_raw truncates in
// place, so validation runs before every write. Concurrent portal/LLM edits are
// last-write-wins per pad.
// ============================================================================

#include "board_config.h"

#if HAS_MCP && HAS_DISPLAY

#include "mcp_tool_registry.h"
#include "mcp_tool_util.h"
#include "web_mcp.h"
#include "pad_config.h"
#include "psram_json_allocator.h"
#include "widgets/widget.h"
#include "binding_template.h"
#include "action_registry.h"
#include "health_binding.h"
#include "list_provider.h"
#include "pad_block.h"

#include <ArduinoJson.h>
#include <esp_heap_caps.h>
#include <stdio.h>
#include <string.h>

// JSON-RPC error codes used by the pad tools (canonical values in
// mcp_tool_registry.h).
static constexpr int PAD_ERR_PARAMS   = MCP_RPC_ERR_PARAMS;
static constexpr int PAD_ERR_INTERNAL = MCP_RPC_ERR_INTERNAL;
static constexpr int PAD_ERR_BUSY     = MCP_RPC_ERR_CONTROL_BUSY;
static constexpr uint32_t PAD_WRITE_TIMEOUT_MS = 4000;

// Thin adapter over the shared mcp_tool_fail (mcp_tool_util.h).
static bool pad_fail(JsonObject& result, String& err, int code, const char* msg) {
    return mcp_tool_fail(result, err, code, msg);
}

// Built-in action types + their flat JSON fields, mirroring the action_parse.cpp
// strcmp ladder (built-ins are not in the action registry). Device-class action
// types are enumerated separately from the registry.
static void emit_builtin_action_fields(JsonObject acts) {
    auto add = [&](const char* type, const char* fields) { acts[type] = fields; };
    add("screen",     "target (screen id)");
    add("mqtt",       "topic, payload");
    add("key",        "sequence (key DSL)");
    add("beep",       "beep_pattern, beep_volume");
    add("volume",     "volume_mode (set|adjust), volume_value");
    add("brightness", "brightness_mode (set|adjust), brightness_value");
    add("timer",      "timer_id (1-3), timer_command, timer_value");
    add("sound",      "sound_file, sound_volume");
    add("notify",     "notify_text, notify_duration_ms, notify_text_color, notify_bg_color, notify_border_color, notify_opacity, notify_font_size, notify_location");
    add("system",     "system_command (reboot|wifi_reconnect|screensaver)");
    add("ha_service", "entity_id, service, data_json");
    add("back",       "(no fields)");
    add("ble_pair",   "(no fields)");
}

// ============================================================================
// get_capabilities — registry-generated manifest
// ============================================================================
static bool tool_get_capabilities(const JsonObject& args, JsonObject& result, String& err) {
    (void)args; (void)err;

    // Widget types — generated from the live registry; per-widget config fields
    // come from each type's describeSchema hook. Adding a widget auto-appears.
    JsonArray widgets = result.createNestedArray("widgets");
    for (uint8_t i = 0; i < widget_count(); ++i) {
        const WidgetType* wt = widget_at(i);
        if (!wt) continue;
        JsonObject w = widgets.createNestedObject();
        w["type"] = wt->name;
        if (wt->describeSchema) wt->describeSchema(w);
    }
    result["widget_common"] = "all widgets: widget_type + widget_data_binding (and _2.._4 for extra binding templates); widget config fields are flat on the button. A config field whose \"type\" is \"action\" is a NESTED action object (same {type, <mode>, <value>} shape as a button action, and supports the {step} token), NOT a string — e.g. numericrocker's widget_numericrocker_action.";

    JsonObject grid = result.createNestedObject("grid");
    grid["max_buttons"] = MAX_PAD_BUTTONS;
    grid["max_cols"] = MAX_GRID_COLS;
    grid["max_rows"] = MAX_GRID_ROWS;
    grid["max_pads"] = MAX_PADS;
    grid["max_actions"] = MAX_BUTTON_ACTIONS;
    grid["max_bindings"] = PAD_MAX_BINDINGS;

    // Pad-level schema (set via set_pad; read via get_pad). Distinct from the
    // per-button schema below.
    JsonObject pad = result.createNestedObject("pad");
    JsonObject pf = pad.createNestedObject("fields");
    pf["name"] = "optional friendly label for the pad (shown in the UI; returned by get_pad/list_pads). The canonical id is still 'pad_N'. To SET it, call set_pad with the 'pad_name' argument (the key 'name' is avoided because some MCP clients reserve it).";
    pf["layout"] = "'grid' (default) or a curated layout name";
    pf["cols"] = "grid columns 1-8";
    pf["rows"] = "grid rows 1-8";
    pf["wake_screen"] = "screen id to navigate to on screensaver wake ('' = stay)";
    pf["bg_color"] = "pad background color #RRGGBB (default #000000)";
    pf["template_pad"] = "int pad index 0..MAX_PADS-1 to inherit buttons into EMPTY cells (-1 = none). Inherited buttons render but are not stored in this pad.";
    pf["bindings"] = "object of name->binding-template, referenced elsewhere as [pad:name]. Names: [a-zA-Z][a-zA-Z0-9_]*";
    pad["bindings_example"] = "{\"power\":\"[mqtt:home/solar/power;watts;%.0f]\",\"hot\":\"[expr:[pad:power]>3000?1:0]\"}";

    JsonObject btn = result.createNestedObject("button");
    JsonArray bf = btn.createNestedArray("fields");
    const char* fields[] = {
        "col", "row", "col_span", "row_span",
        "label_top", "label_center", "label_bottom",
        "label_top_style", "label_center_style", "label_bottom_style",
        "bg_color", "fg_color", "border_color", "border_width", "corner_radius", "content_pad",
        "icon_id", "btn_state", "widget_type", "widget_data_binding", "actions", "lp_actions"
    };
    for (const char* f : fields) bf.add(f);
    btn["widget_note"] = "widget keys are flat: widget_type + widget_data_binding[_2.._4]; widget config fields (e.g. min/max/color) are flat on the button too";
    btn["labels_note"] = "labels render with LVGL bitmap fonts (Latin text, digits, basic punctuation only). Do NOT use emoji or Unicode symbols — they render as blank/tofu. Use plain text, or an icon_id for graphics.";
    btn["limits_note"] = "max field lengths: button labels/colors/btn_state/widget_data_binding = 191 chars, border_width/corner_radius = 63, pad binding name = 31. Widget caption/text fields are shorter (see each field's \"max\" in widgets[].config_fields, typically 63). If a binding expression is too long, declare it once as a pad-level [pad:name] binding and reference [pad:name] instead of inlining it.";
    JsonArray states = btn.createNestedArray("btn_state");
    states.add("enabled"); states.add("disabled"); states.add("hidden");
    btn["btn_state_note"] = "accepts a binding for conditional visibility, e.g. [expr:[mqtt:printer;state]==\"online\"?\"enabled\":\"hidden\"] (unresolved -> enabled)";

    JsonObject style = result.createNestedObject("label_style");
    style["format"] = "font:24;font_family:dseg7;font_upscale:1.4;align:right;y:-3;mode:scroll;color:#FF0000";
    style["color_note"] = "color: accepts either a #RRGGBB hex value or a binding token (e.g. [expr:...] or [net:...]) that resolves live, like fg_color and the other color fields; a per-label color overrides fg_color. Style DSL max length 127 chars.";
    JsonArray fam = style.createNestedArray("font_family");
    fam.add("dseg7"); fam.add("bebas"); fam.add("doto");
    JsonArray al = style.createNestedArray("align");
    al.add("left"); al.add("right"); al.add("center");
    JsonArray md = style.createNestedArray("mode");
    md.add("clip"); md.add("scroll"); md.add("dot"); md.add("wrap");

    // Action types: built-in fields (static, mirrors action_parse) + device-class
    // action types enumerated from the registry (generated, board-specific).
    JsonObject acts = result.createNestedObject("action_types");
    emit_builtin_action_fields(acts);
    for (uint8_t i = 0; i < action_type_count(); ++i) {
        const ActionTypeDef* d = action_type_at(i);
        if (!d || !d->type_name) continue;
        if (d->describe) {
            JsonObject ao = acts.createNestedObject(d->type_name);
            d->describe(ao);
        } else {
            acts[d->type_name] = "device-class action; flat fields {command, value} (value bindable)";
        }
    }
    result["actions_note"] = "button.actions (tap) / button.lp_actions (long-press): arrays of {type, ...fields above}";
    result["position_note"] = "set_button/set_buttons 'position' is the 0-based index in the pad's button array, NOT a grid cell (grid placement is col/row). Use 0,1,2,...; a position at or past the end appends. To rebuild a pad: clear_pad then add buttons from position 0.";
    result["screen_ref_note"] = "pad tools accept the 'screen' arg as either the canonical id 'pad_N' or a pad's friendly name (case-insensitive). Names may be unset or non-unique; an ambiguous name is refused with the matching ids so you can pick one. Creating a new pad requires the 'pad_N' id. list_pads shows each pad's name.";

    // Bindings: ONE generated block. Each scheme is enumerated from the live
    // registry (so device-class schemes auto-appear) and described in place via
    // emit_binding_detail — the single source of binding docs (no separate
    // examples + help blocks).
    JsonObject bindings = result.createNestedObject("bindings");
    bindings["_about"] = "A [scheme:params] token resolves to live data at runtime; usable in any label/color/state/widget field, mixable with literal text and multiple tokens. Optional '|fallback' at the OUTER bracket level when unresolved/error. Tokens nest inside [expr:..]. Pad-level [pad:name] bindings are declared in pad.bindings.";
    for (uint8_t i = 0; i < binding_template_scheme_count(); ++i) {
        const char* name = binding_template_scheme_name(i);
        if (!name || !name[0]) continue;
        JsonObject so = bindings.createNestedObject(name);
        // Each scheme describes itself (hook lives in its own .cpp). Schemes with
        // no hook (e.g. device-class) fall back to a generic shape here.
        if (!binding_template_describe_scheme(i, &so)) {
            char ex[40];
            snprintf(ex, sizeof(ex), "[%s:params]", name);
            so["example"] = ex;
            so["note"] = "device-class scheme";
        }
    }

    // Complementary enumerations referenced by the binding detail above.
    JsonArray hk = result.createNestedArray("health_keys");
    for (uint8_t i = 0; i < health_binding_key_count(); ++i) {
        const char* k = health_binding_key_at(i);
        if (!k) continue;
        JsonObject ko = hk.createNestedObject();
        ko["name"] = k;
        const char* d = health_binding_key_desc_at(i);
        if (d) ko["desc"] = d;
    }
    JsonArray lp = result.createNestedArray("list_providers");
    for (uint8_t i = 0; i < list_provider_count(); ++i) {
        const ListProvider* p = list_provider_at(i);
        if (p && p->id) lp.add(p->id);
    }

    JsonObject fmt = result.createNestedObject("formats");
    fmt["color"] = "#RRGGBB";
    fmt["size"] = "integer pixels or binding template";
    result["icon_note"] = "icon_id must reference an icon already uploaded via the portal (material symbols are stored as 'mi_<name>'). MCP cannot upload icons; setting an unknown icon_id renders blank. For symbols without an upload, put a font glyph or a [time:..] binding in a label instead.";

    // Visual inspection: the device cannot hand the model an image directly, but
    // a Playwright-driven browser can. /api/screenshot is the live framebuffer as
    // a large 24-bit BMP — too big and not directly understandable as text/data,
    // so it must be rendered in a browser and captured as an <img>, never fetched
    // inline. This is the one reliable way to verify on-panel rendering.
    JsonObject vis = result.createNestedObject("visual_inspection");
    vis["endpoint"] = "GET /api/screenshot";
    vis["format"] = "24-bit BMP of the current on-device screen; large and image-only — do NOT fetch it as text/data, render it in a browser instead";
    vis["why"] = "the only reliable way to verify how a pad/button/widget actually renders on the panel (colors, resolved bindings, overflow, layout) without the physical device";
    JsonArray steps = vis.createNestedArray("playwright_steps");
    steps.add("To verify a specific pad, first set_screen('pad_N') so it is on-screen (requires control tools enabled).");
    steps.add("page.goto('http://<device-ip>/api/screenshot')");
    steps.add("page.waitForTimeout(1000)  // let the image load");
    steps.add("screenshot_page with selector 'img'  // captures just the framebuffer, no browser chrome");
    vis["device_ip"] = "use get_device_status.wifi.ip for <device-ip>";
    vis["auth_note"] = "if portal Basic Auth is enabled, embed credentials in the URL (http://user:pass@host/api/screenshot); the MCP bearer token does not apply to /api/screenshot";

    // Device-settings surface (get_config/set_config + get/set_component_config),
    // defined in mcp_tools_config.cpp so the component list stays a single source
    // of truth. Lets an assistant discover the non-pad config it can read/write.
    extern void mcp_config_capabilities(JsonObject& out);
    JsonObject device_config = result.createNestedObject("device_config");
    mcp_config_capabilities(device_config);
    return true;
}

// ============================================================================
// Validation — shared by validate_pad and every write tool. Runs before any
// save: pad_config_save_raw truncates in place, so a bad pass would corrupt the
// pad. Returns nullptr on success or a short error string.
// ============================================================================

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
// Single in-flight MCP request, so a static buffer for a formatted validation
// message is safe (and the validator returns const char*).
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

static const char* validate_button(JsonObjectConst b, int cols, int rows) {
    int col = b["col"] | 0;
    int row = b["row"] | 0;
    int cspan = b["col_span"] | 1;
    int rspan = b["row_span"] | 1;
    if (col < 0 || row < 0) return "button col/row negative";
    if (cspan < 1 || rspan < 1) return "span must be >= 1";
    if (col + cspan > cols) return "button overflows grid columns";
    if (row + rspan > rows) return "button overflows grid rows";
    if (!color_ok(b["bg_color"] | "")) return "bad bg_color";
    if (!color_ok(b["fg_color"] | "")) return "bad fg_color";
    if (!color_ok(b["border_color"] | "")) return "bad border_color";
    const char* wt = b["widget_type"] | "";
    const WidgetType* wtype = wt[0] ? widget_find(wt) : nullptr;
    if (wt[0] && !wtype) return "unknown widget type";
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
static const char* validate_pad_doc(JsonObjectConst pad) {
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
        const char* e = validate_button(b, cols, rows);
        if (e) return e;
    }
    return nullptr;
}

// ============================================================================
// Deferred save (ptr+len in ctx; main loop saves+rebuilds+frees) — D6
// ============================================================================
struct PadWriteCtx { uint8_t page; uint8_t* buf; size_t len; };

static void exec_pad_save(const void* ctx, bool* ok, char* msg, size_t msg_len) {
    const PadWriteCtx* c = (const PadWriteCtx*)ctx;
    *ok = false;
    if (!c->buf) { strlcpy(msg, "no buffer", msg_len); return; }
    bool saved = pad_config_save_raw(c->page, c->buf, c->len);
    heap_caps_free(c->buf);
    if (!saved) { strlcpy(msg, "save failed", msg_len); return; }
    pad_config_rebuild_all_caches();
    *ok = true;
    strlcpy(msg, "saved", msg_len);
}

// Serialize `doc` into a PSRAM buffer and defer the save. Validates first.
static bool commit_pad(uint8_t page, JsonDocument& doc, JsonObject& result, String& err) {
    const char* verr = validate_pad_doc(doc.as<JsonObjectConst>());
    if (verr) return pad_fail(result, err, PAD_ERR_PARAMS, verr);

    size_t need = measureJson(doc) + 1;
    uint8_t* buf = (uint8_t*)mcp_psram_alloc(need);
    if (!buf) return pad_fail(result, err, PAD_ERR_INTERNAL, "out of memory");
    size_t len = serializeJson(doc, buf, need);

    PadWriteCtx ctx; ctx.page = page; ctx.buf = buf; ctx.len = len;
    bool ok = false; char msg[MCP_TOOL_MSG_LEN] = {0};
    McpControlResult r = mcp_control_dispatch(exec_pad_save, &ctx, sizeof(ctx),
                                              PAD_WRITE_TIMEOUT_MS, &ok, msg, sizeof(msg));
    if (r == MCP_CONTROL_BUSY) { heap_caps_free(buf); return pad_fail(result, err, PAD_ERR_BUSY, "busy, retry"); }
    // On TIMEOUT the deferred job may still run and free buf, so do not free here.
    return mcp_finish_control(r, ok, msg, result, err);
}

// Resolve a pad reference (id 'pad_N' or friendly name) to a page index. On
// failure, s_pad_err holds a clarifying reason (unknown name, or ambiguous name
// listing the candidate ids).
static char s_pad_err[160];
static int pad_index(const char* screen) {
    return pad_config_resolve_ref(screen, s_pad_err, sizeof(s_pad_err));
}

// Load pad raw into doc; if missing, seed a minimal grid pad. Returns false on OOM.
static bool load_or_seed(uint8_t page, JsonDocument& doc) {
    size_t len = 0;
    char* raw = pad_config_read_raw(page, &len);
    if (raw) {
        DeserializationError e = deserializeJson(doc, raw, len);
        free(raw);
        if (!e) return true;
    }
    doc.clear();
    doc["layout"] = "grid";
    doc["cols"] = 4;
    doc["rows"] = 4;
    doc.createNestedArray("buttons");
    return true;
}

// Insert/replace at position; appends when pos is at or past the end. Position
// is the 0-based ARRAY index (not a grid cell — grid placement is col/row), so a
// sparse/grid-style position past the current size simply appends rather than
// failing. Only fails when pos<0 or the array is already full.
static bool splice_button(JsonArray btns, int pos, JsonVariantConst button) {
    if (pos < 0 || (int)btns.size() >= MAX_PAD_BUTTONS) return false;
    if (pos < (int)btns.size()) btns[pos] = button;  // replace existing slot
    else btns.add(button);                           // append (lenient)
    return true;
}

// ============================================================================
// validate_pad (read tool) — dry-run, no save
// ============================================================================
static bool tool_validate_pad(const JsonObject& args, JsonObject& result, String& err) {
    JsonObjectConst pad = args["pad"];
    if (pad.isNull()) return pad_fail(result, err, PAD_ERR_PARAMS, "missing pad object");
    const char* verr = validate_pad_doc(pad);
    if (verr) { result["valid"] = false; result["error"] = verr; return true; }
    result["valid"] = true;
    return true;
}

// get_pad_blocks — list pre-built button groups (building blocks) the client can
// drop onto a pad instead of hand-building. Generated from the live catalog.
static bool tool_get_pad_blocks(const JsonObject& args, JsonObject& result, String& err) {
    (void)args; (void)err;
    JsonArray arr = result.createNestedArray("blocks");
    const PadBlock* const* cat = pad_block_catalog();
    uint8_t n = pad_block_catalog_count();
    for (uint8_t i = 0; i < n; ++i) {
        const PadBlock* blk = cat ? cat[i] : nullptr;
        if (!blk || !blk->id) continue;
        JsonObject o = arr.createNestedObject();
        o["id"] = blk->id;
        if (blk->name) o["name"] = blk->name;
        if (blk->desc) o["desc"] = blk->desc;
        o["min_cols"] = blk->min_cols;
        o["min_rows"] = blk->min_rows;
        o["min_free_cells"] = blk->min_free_cells;
        o["button_count"] = blk->button_count;
    }
    return true;
}

// ============================================================================
// set_button — replace/insert one button by position
// ============================================================================
static bool tool_set_button(const JsonObject& args, JsonObject& result, String& err) {
    int pg = pad_index(args["screen"] | "");
    if (pg < 0) return pad_fail(result, err, PAD_ERR_PARAMS, s_pad_err);
    if (!args.containsKey("position")) return pad_fail(result, err, PAD_ERR_PARAMS, "missing position");
    int pos = args["position"] | -1;
    JsonObjectConst button = args["button"];
    if (button.isNull()) return pad_fail(result, err, PAD_ERR_PARAMS, "missing button object");

    BasicJsonDocument<PsramJsonAllocator> doc(48 * 1024);
    if (!load_or_seed((uint8_t)pg, doc)) return pad_fail(result, err, PAD_ERR_INTERNAL, "out of memory");
    JsonArray btns = doc["buttons"];
    if (btns.isNull()) btns = doc.createNestedArray("buttons");
    if (!splice_button(btns, pos, button)) return pad_fail(result, err, PAD_ERR_PARAMS, "position out of range");
    return commit_pad((uint8_t)pg, doc, result, err);
}

static bool tool_set_buttons(const JsonObject& args, JsonObject& result, String& err) {
    int pg = pad_index(args["screen"] | "");
    if (pg < 0) return pad_fail(result, err, PAD_ERR_PARAMS, s_pad_err);
    JsonArrayConst items = args["buttons"];
    if (items.isNull()) return pad_fail(result, err, PAD_ERR_PARAMS, "missing buttons array");

    BasicJsonDocument<PsramJsonAllocator> doc(48 * 1024);
    if (!load_or_seed((uint8_t)pg, doc)) return pad_fail(result, err, PAD_ERR_INTERNAL, "out of memory");
    JsonArray btns = doc["buttons"];
    if (btns.isNull()) btns = doc.createNestedArray("buttons");
    for (JsonObjectConst it : items) {
        int pos = it["position"] | -1;
        JsonObjectConst button = it["button"];
        if (button.isNull()) return pad_fail(result, err, PAD_ERR_PARAMS, "item missing button");
        if (!splice_button(btns, pos, button)) return pad_fail(result, err, PAD_ERR_PARAMS, "position out of range");
    }
    return commit_pad((uint8_t)pg, doc, result, err);
}

static bool tool_remove_button(const JsonObject& args, JsonObject& result, String& err) {
    int pg = pad_index(args["screen"] | "");
    if (pg < 0) return pad_fail(result, err, PAD_ERR_PARAMS, s_pad_err);
    int pos = args["position"] | -1;

    BasicJsonDocument<PsramJsonAllocator> doc(48 * 1024);
    if (!load_or_seed((uint8_t)pg, doc)) return pad_fail(result, err, PAD_ERR_INTERNAL, "out of memory");
    JsonArray btns = doc["buttons"];
    if (btns.isNull() || pos < 0 || pos >= (int)btns.size())
        return pad_fail(result, err, PAD_ERR_PARAMS, "position out of range");
    btns.remove(pos);
    return commit_pad((uint8_t)pg, doc, result, err);
}

static bool tool_clear_pad(const JsonObject& args, JsonObject& result, String& err) {
    int pg = pad_index(args["screen"] | "");
    if (pg < 0) return pad_fail(result, err, PAD_ERR_PARAMS, s_pad_err);
    BasicJsonDocument<PsramJsonAllocator> doc(48 * 1024);
    if (!load_or_seed((uint8_t)pg, doc)) return pad_fail(result, err, PAD_ERR_INTERNAL, "out of memory");
    JsonArray btns = doc["buttons"];
    if (btns.isNull()) doc.createNestedArray("buttons");
    else btns.clear();
    return commit_pad((uint8_t)pg, doc, result, err);
}

// set_pad — merge pad-level fields (layout/cols/rows/wake_screen/bg_color/
// template_pad/bindings) into the pad, preserving existing buttons. Only keys
// present in args are changed.
static bool tool_set_pad(const JsonObject& args, JsonObject& result, String& err) {
    int pg = pad_index(args["screen"] | "");
    if (pg < 0) return pad_fail(result, err, PAD_ERR_PARAMS, s_pad_err);

    BasicJsonDocument<PsramJsonAllocator> doc(48 * 1024);
    if (!load_or_seed((uint8_t)pg, doc)) return pad_fail(result, err, PAD_ERR_INTERNAL, "out of memory");

    if (args.containsKey("layout"))       doc["layout"] = args["layout"];
    // Friendly label. The argument is 'pad_name' (not 'name') because some MCP
    // clients reserve/strip an argument literally called 'name' — it collides
    // with the tools/call 'name' field. Stored as the pad's 'name' key.
    if (args.containsKey("pad_name"))     doc["name"] = args["pad_name"];
    if (args.containsKey("cols"))         doc["cols"] = (int)(args["cols"] | 0);
    if (args.containsKey("rows"))         doc["rows"] = (int)(args["rows"] | 0);
    if (args.containsKey("wake_screen"))  doc["wake_screen"] = args["wake_screen"];
    if (args.containsKey("bg_color"))     doc["bg_color"] = args["bg_color"];
    if (args.containsKey("template_pad")) doc["template_pad"] = (int)(args["template_pad"] | -1);
    if (args.containsKey("bindings"))     doc["bindings"] = args["bindings"];  // object copy

    return commit_pad((uint8_t)pg, doc, result, err);
}

// ============================================================================
// Registration
// ============================================================================
static const McpTool s_tool_get_capabilities = {
    "get_capabilities",
    "Get the pad authoring manifest: widget types + config fields, button schema, label-style DSL, binding schemes (incl. [pad:name] + template_pad), grid limits, action targets, color/size formats. Read-only.",
    "{\"type\":\"object\",\"properties\":{}}",
    tool_get_capabilities, true, false, false, false
};
REGISTER_MCP_TOOL(s_tool_get_capabilities);

static const McpTool s_tool_validate_pad = {
    "validate_pad",
    "Dry-run validate a pad JSON (grid bounds, span overflow, widget types, colors, binding tokens) without saving. Args: pad (object). Read-only.",
    "{\"type\":\"object\",\"properties\":{\"pad\":{\"type\":\"object\"}},\"required\":[\"pad\"]}",
    tool_validate_pad, true, false, false, false
};
REGISTER_MCP_TOOL(s_tool_validate_pad);

static const McpTool s_tool_get_pad_blocks = {
    "get_pad_blocks",
    "List pre-built button groups (building blocks) that can be dropped onto a pad: id, name, description, size requirements, button count. Read-only.",
    "{\"type\":\"object\",\"properties\":{}}",
    tool_get_pad_blocks, true, false, false, false
};
REGISTER_MCP_TOOL(s_tool_get_pad_blocks);

static const McpTool s_tool_set_button = {
    "set_button",
    "Create/replace one button. position = 0-based index in the button array (NOT a grid cell; placement is col/row). A position at/past the end appends. Args: screen (pad id or friendly name), position (int), button (JSON, portal pad schema). Requires authoring.",
    "{\"type\":\"object\",\"properties\":{\"screen\":{\"type\":\"string\"},\"position\":{\"type\":\"integer\"},\"button\":{\"type\":\"object\"}},\"required\":[\"screen\",\"position\",\"button\"]}",
    tool_set_button, false, true, false, true
};
REGISTER_MCP_TOOL(s_tool_set_button);

static const McpTool s_tool_set_buttons = {
    "set_buttons",
    "Create/replace many buttons in one save (processed in array order). Each item.position is a 0-based array index (NOT a grid cell); positions past the end append. To rebuild a pad, clear_pad first then use positions 0,1,2,... Args: screen (pad id or friendly name), buttons (array of {position, button}). Requires authoring.",
    "{\"type\":\"object\",\"properties\":{\"screen\":{\"type\":\"string\"},\"buttons\":{\"type\":\"array\",\"items\":{\"type\":\"object\",\"properties\":{\"position\":{\"type\":\"integer\"},\"button\":{\"type\":\"object\"}},\"required\":[\"position\",\"button\"]}}},\"required\":[\"screen\",\"buttons\"]}",
    tool_set_buttons, false, true, false, true
};
REGISTER_MCP_TOOL(s_tool_set_buttons);

static const McpTool s_tool_remove_button = {
    "remove_button",
    "Remove the button at a position. Args: screen (pad id or friendly name), position (int). Requires authoring.",
    "{\"type\":\"object\",\"properties\":{\"screen\":{\"type\":\"string\"},\"position\":{\"type\":\"integer\"}},\"required\":[\"screen\",\"position\"]}",
    tool_remove_button, false, true, false, true
};
REGISTER_MCP_TOOL(s_tool_remove_button);

static const McpTool s_tool_clear_pad = {
    "clear_pad",
    "Remove all buttons from a pad. Args: screen (pad id or friendly name). Requires authoring.",
    "{\"type\":\"object\",\"properties\":{\"screen\":{\"type\":\"string\"}},\"required\":[\"screen\"]}",
    tool_clear_pad, false, true, false, true
};
REGISTER_MCP_TOOL(s_tool_clear_pad);

static const McpTool s_tool_set_pad = {
    "set_pad",
    "Set pad-level fields (preserves buttons): pad_name (friendly label; arg is 'pad_name' not 'name'), layout, cols, rows, wake_screen, bg_color, template_pad (inherit buttons into empty cells), and bindings (object of [pad:name] templates). Only provided keys change. 'screen' may be a 'pad_N' id or friendly name. Requires authoring.",
    "{\"type\":\"object\",\"properties\":{\"screen\":{\"type\":\"string\"},\"pad_name\":{\"type\":\"string\"},\"layout\":{\"type\":\"string\"},\"cols\":{\"type\":\"integer\"},\"rows\":{\"type\":\"integer\"},\"wake_screen\":{\"type\":\"string\"},\"bg_color\":{\"type\":\"string\"},\"template_pad\":{\"type\":\"integer\"},\"bindings\":{\"type\":\"object\"}},\"required\":[\"screen\"]}",
    tool_set_pad, false, true, false, true
};
REGISTER_MCP_TOOL(s_tool_set_pad);

#endif // HAS_MCP && HAS_DISPLAY
