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
#include "pad_validate.h"
#include "pad_binding.h"
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
// Validation — the full pad validator now lives in pad_validate.{h,cpp} so the
// web portal save path and these MCP tools validate through identical logic
// (single source of truth). Call pad_validate(JsonObjectConst) — it runs before
// every write because pad_config_save_raw truncates in place.
// ============================================================================

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
    const char* verr = pad_validate(doc.as<JsonObjectConst>());
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
    const char* verr = pad_validate(pad);
    if (verr) { result["valid"] = false; result["error"] = verr; return true; }
    result["valid"] = true;
    return true;
}

#if HAS_MQTT
// ============================================================================
// resolve_bindings (read tool, authoring-gated) — resolve binding tokens
// against LIVE data on the main loop; no save. Powers debugging/preview of what
// a binding or a proposed button's fields resolve to right now. Values only
// (not a pixel render). Runs pad_resolve() on the main loop via the deferred
// control bridge (binding_template_resolve is LVGL/main-task only).
// ============================================================================
static constexpr size_t   RESOLVE_MAX        = 32;
static constexpr size_t   RESOLVE_STRIDE     = BINDING_TEMPLATE_MAX_LEN;
static constexpr uint32_t RESOLVE_TIMEOUT_MS = 2000;

// Bindable button fields resolved for the `button` form (labels/colors/state +
// widget data bindings). Names match set_button.
static const char* const kResolveButtonFields[] = {
    "label_top", "label_center", "label_bottom",
    "bg_color", "fg_color", "border_color", "btn_state",
    "widget_data_binding", "widget_data_binding_2",
    "widget_data_binding_3", "widget_data_binding_4",
};

struct ResolveCtx {
    const char* const* inputs;
    size_t             count;
    const PadBinding*  binds;
    uint8_t            bind_count;
    char*              out;
    size_t             stride;
};

static void exec_resolve(const void* ctx, bool* ok, char* msg, size_t msg_len) {
    const ResolveCtx* c = (const ResolveCtx*)ctx;
    pad_resolve(c->inputs, c->count, c->binds, c->bind_count, c->out, c->stride);
    *ok = true;
    snprintf(msg, msg_len, "resolved %u", (unsigned)c->count);
}

// Copy a pad's stored [pad:name] bindings into a heap PadBinding[]. Returns the
// count and sets *out_binds (caller frees), or 0 with *out_binds == nullptr.
static uint8_t load_pad_bindings(uint8_t page, PadBinding** out_binds) {
    *out_binds = nullptr;
    size_t len = 0;
    char* raw = pad_config_read_raw(page, &len);
    if (!raw) return 0;
    BasicJsonDocument<PsramJsonAllocator> doc(len + 512);
    DeserializationError e = deserializeJson(doc, raw, len);
    free(raw);
    if (e) return 0;
    JsonObjectConst pb = doc["bindings"];
    if (pb.isNull() || pb.size() == 0) return 0;
    PadBinding* arr = (PadBinding*)mcp_psram_alloc(sizeof(PadBinding) * PAD_MAX_BINDINGS);
    if (!arr) return 0;
    uint8_t n = 0;
    for (JsonPairConst kv : pb) {
        if (n >= PAD_MAX_BINDINGS) break;
        strlcpy(arr[n].name, kv.key().c_str(), PAD_BINDING_NAME_MAX_LEN);
        const char* v = kv.value().as<const char*>();
        strlcpy(arr[n].value, v ? v : "", CONFIG_LABEL_MAX_LEN);
        n++;
    }
    *out_binds = arr;
    return n;
}

static bool tool_resolve_bindings(const JsonObject& args, JsonObject& result, String& err) {
    JsonArrayConst  in_bindings = args["bindings"];
    JsonObjectConst in_button   = args["button"];
    const bool has_bindings = !in_bindings.isNull() && in_bindings.size() > 0;
    const bool has_button   = !in_button.isNull();
    if (!has_bindings && !has_button)
        return pad_fail(result, err, PAD_ERR_PARAMS, "provide 'bindings' (array of strings) and/or 'button' (object)");

    // Optional pad context so [pad:name] tokens resolve for the given pad.
    PadBinding* binds = nullptr;
    uint8_t bind_count = 0;
    const char* screen = args["screen"] | "";
    if (screen[0]) {
        int pg = pad_index(screen);
        if (pg < 0) return pad_fail(result, err, PAD_ERR_PARAMS, s_pad_err);
        bind_count = load_pad_bindings((uint8_t)pg, &binds);
    }

    // Collect input template strings. The pointers reference the request args
    // doc, which stays alive while this handler blocks on mcp_control_dispatch.
    const char* inputs[RESOLVE_MAX];
    const char* field_names[RESOLVE_MAX];  // non-null => came from a button field
    size_t count = 0;
    if (has_bindings) {
        for (JsonVariantConst v : in_bindings) {
            if (count >= RESOLVE_MAX) break;
            const char* s = v.as<const char*>();
            if (!s) continue;
            inputs[count] = s; field_names[count] = nullptr; count++;
        }
    }
    const size_t button_start = count;
    if (has_button) {
        for (const char* f : kResolveButtonFields) {
            if (count >= RESOLVE_MAX) break;
            const char* s = in_button[f].as<const char*>();
            if (!s || !s[0]) continue;
            inputs[count] = s; field_names[count] = f; count++;
        }
    }
    if (count == 0) {
        if (binds) heap_caps_free(binds);
        return pad_fail(result, err, PAD_ERR_PARAMS, "no resolvable string fields provided");
    }

    char* out = (char*)mcp_psram_alloc(count * RESOLVE_STRIDE);
    if (!out) {
        if (binds) heap_caps_free(binds);
        return pad_fail(result, err, PAD_ERR_INTERNAL, "out of memory");
    }

    ResolveCtx ctx = { inputs, count, binds, bind_count, out, RESOLVE_STRIDE };
    bool ok = false; char msg[MCP_TOOL_MSG_LEN] = {0};
    McpControlResult r = mcp_control_dispatch(exec_resolve, &ctx, sizeof(ctx),
                                              RESOLVE_TIMEOUT_MS, &ok, msg, sizeof(msg));
    if (r == MCP_CONTROL_BUSY) {
        heap_caps_free(out); if (binds) heap_caps_free(binds);
        return pad_fail(result, err, PAD_ERR_BUSY, "busy, retry");
    }
    if (r != MCP_CONTROL_OK) {
        // TIMEOUT: the deferred job may still run and read out/binds on the main
        // loop, so we cannot free them here (only reachable if the main loop is
        // wedged — a bounded, single-in-flight leak). Report the failure.
        return mcp_finish_control(r, ok, msg, result, err);
    }

    // Success — copy resolved values into the result (String forces ArduinoJson
    // to duplicate the bytes; `out` is freed before the response serializes).
    if (has_bindings) {
        JsonArray arr = result.createNestedArray("resolved");
        for (size_t i = 0; i < button_start; i++) {
            JsonObject o = arr.createNestedObject();
            o["input"] = String(inputs[i]);
            o["value"] = String(out + i * RESOLVE_STRIDE);
        }
    }
    if (has_button) {
        JsonObject bo = result.createNestedObject("button");
        for (size_t i = button_start; i < count; i++) {
            bo[field_names[i]] = String(out + i * RESOLVE_STRIDE);
        }
    }
    heap_caps_free(out);
    if (binds) heap_caps_free(binds);
    return true;
}
#endif // HAS_MQTT

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

#if HAS_MQTT
static const McpTool s_tool_resolve_bindings = {
    "resolve_bindings",
    "Resolve [scheme:params] binding tokens against the device's LIVE data and return the resolved text — to debug/preview what a binding or a proposed button renders to, WITHOUT saving. Args: bindings (array of template strings) and/or button (a proposed button object; its bindable fields label_*/*_color/btn_state/widget_data_binding[_2..4] are resolved), plus optional screen (pad_N or friendly name) to supply that pad's [pad:name] context. Returns resolved VALUES only — it does not render pixels (use GET /api/screenshot for a visual). Read-only; nothing is persisted. Requires pad authoring.",
    "{\"type\":\"object\",\"properties\":{\"screen\":{\"type\":\"string\"},\"bindings\":{\"type\":\"array\",\"items\":{\"type\":\"string\"}},\"button\":{\"type\":\"object\"}}}",
    tool_resolve_bindings, true, false, false, true
};
REGISTER_MCP_TOOL(s_tool_resolve_bindings);
#endif // HAS_MQTT

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
