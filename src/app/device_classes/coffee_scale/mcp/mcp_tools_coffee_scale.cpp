// ============================================================================
// mcp_tools_coffee_scale.cpp — Coffee Scale device-class MCP tools.
//
// Aggregated into the build via src/app/mcp_components.cpp (arduino-cli only
// compiles .cpp files in the sketch root). Doubly-gated HAS_MCP &&
// IS_COFFEE_SCALE so non-scale firmware never compiles a byte of this.
//
// Ownership: every coffee-scale MCP tool lives here, inside the coffee_scale
// device-class folder. The only hook outside this folder is the one-line
// #include in mcp_components.cpp under the IS_COFFEE_SCALE gate.
//
// Threading contract (see mcp_tool_registry.h):
//   - READ tools run on the AsyncWebServer (web) task. The scale getters return
//     cached EMA values (no blocking ADC read) and are already read cross-task
//     by the existing GET /api/scale handler, so they run inline. The brew
//     status getters read main-loop-owned scalars/strings; values are copied
//     into local buffers immediately to bound the (rare, authoring-only)
//     template-reload window. The live brew series is read via
//     brew_series_copy()/brew_markers_copy(), which snapshot under a spinlock so
//     a concurrent deferred free in brew_tick() cannot use-after-free (mirrors
//     the shutter capture-snapshot getters).
//   - CONTROL tools build a ButtonAction and defer to the main loop via
//     mcp_control_dispatch() (exactly like shutter_control) — scale/brew command
//     dispatch runs through action_dispatch(), which may touch LVGL/audio and
//     must not run on the web task.
//   - delete_brew, delete_brew_template and set_brew_template are plain
//     LittleFS/template-registry I/O (no LVGL, no action dispatch) and run
//     inline on the web task, the same way the existing portal handlers do.
//
// Saved-brew series exposure mirrors the shutter saved-session pattern: the full
// per-second weight/flow series is large, so get_brew returns metadata plus a
// detail_url (GET /api/brews?id=N) that streams the raw record, rather than
// inlining it. The in-progress brew's series is exposed live and decimated by
// get_brew_series (the get_shutter_waveform analog).
// ============================================================================

#include "board_config.h"

#if HAS_MCP && IS_COFFEE_SCALE

#include "mcp_tool_registry.h"
#include "web_mcp.h"

#include "../scale_hal.h"
#include "../coffee_scale_config.h"
#include "../coffee_scale_payload.h"   // ScalePayload/BrewPayload, ACTION_TYPE_SCALE/BREW
#include "../sensors/scale_smoothing.h"  // scale_smoothing_preset_name
#include "../brew/brew_manager.h"
#include "../brew/brew_templates.h"
#include "../brew/brew_template_dsl.h"
#include "../brew/brew_template_loader.h"
#include "../brew/brew_log.h"

#include "action_dispatch.h"
#include "fs_health.h"          // fs_health_set_storage_usage
#include "web_portal_json.h"   // make_psram_json_doc
#include "log_manager.h"

#include <ArduinoJson.h>
#include <LittleFS.h>
#include <esp_heap_caps.h>
#include <math.h>
#include <string.h>
#include <stdio.h>

#define TAG "ScaleMCP"

// JSON-RPC error codes (canonical values in mcp_tool_registry.h).
static constexpr int CS_ERR_PARAMS   = MCP_RPC_ERR_PARAMS;
static constexpr int CS_ERR_INTERNAL = MCP_RPC_ERR_INTERNAL;
static constexpr int CS_ERR_BUSY     = MCP_RPC_ERR_CONTROL_BUSY;

static constexpr uint32_t CS_CONTROL_TIMEOUT_MS = 3000;

// ----------------------------------------------------------------------------
// Tunable knobs.
// ----------------------------------------------------------------------------
#ifndef MCP_BREW_SERIES_MAX_POINTS
#define MCP_BREW_SERIES_MAX_POINTS 120   // emitted points after decimation
#endif

// Cap list_brews output so the response stays inside the dispatcher's PSRAM
// result document (newest-first; older brews are reachable individually via
// get_brew). The portal's full list is paged client-side instead.
#ifndef MCP_BREW_LIST_MAX
#define MCP_BREW_LIST_MAX 40
#endif

// Safety limit for the uploaded brew-template JSON (mirrors the 8 KB cap in the
// portal POST /api/brew-templates handler).
#ifndef MCP_BREW_TEMPLATE_MAX_BYTES
#define MCP_BREW_TEMPLATE_MAX_BYTES 8192
#endif

static bool cs_fail(JsonObject& result, String& err, int code, const char* msg) {
    err = msg ? msg : "error";
    result[MCP_RESULT_ERRCODE_KEY] = code;
    return false;
}

static const char* brew_phase_str(BrewPhase p) {
    switch (p) {
        case BREW_ACTIVE: return "active";
        case BREW_DONE:   return "done";
        case BREW_IDLE:   return "idle";
        default:          return "idle";
    }
}

// ============================================================================
// Read tool: get_scale_status — live weight/flow + calibration state
// ============================================================================

static bool tool_get_scale_status(const JsonObject& args, JsonObject& result, String& err) {
    (void)args; (void)err;
    result["available"]           = scale_is_available();
    result["weight_g"]            = scale_get_weight();
    result["flow_rate_g_s"]       = scale_get_flow_rate();
    result["calibration_factor"]  = scale_get_calibration_factor();
    result["offset"]              = (double)scale_get_offset();
    result["cal_weight_g"]        = scale_get_cal_weight();
    result["status"]              = scale_get_status();
    result["smoothing"]           = coffee_scale_config.scale_smoothing;
    result["smoothing_name"]      = scale_smoothing_preset_name(coffee_scale_config.scale_smoothing);
    return true;
}

// ============================================================================
// Read tool: get_brew_status — brew state machine snapshot
// ============================================================================

static bool tool_get_brew_status(const JsonObject& args, JsonObject& result, String& err) {
    (void)args; (void)err;

    BrewPhase phase = brew_get_phase();
    result["phase"]  = brew_phase_str(phase);
    result["active"] = brew_is_active();

    // Copy strings immediately into local buffers — the const char* getters
    // return pointers into template storage that an authoring-time reload could
    // free out from under a long-lived JSON reference.
    char tmpl_name[24], tmpl_disp[48], stage_name[24], instr[128], next_label[48];
    strlcpy(tmpl_name,  brew_get_template_name(), sizeof(tmpl_name));
    strlcpy(tmpl_disp,  brew_get_display_name(),  sizeof(tmpl_disp));
    strlcpy(stage_name, brew_get_stage_name(),    sizeof(stage_name));
    strlcpy(instr,      brew_get_instruction(),   sizeof(instr));
    strlcpy(next_label, brew_get_next_label(),    sizeof(next_label));

    JsonObject tpl = result.createNestedObject("template");
    tpl["name"]         = tmpl_name;
    tpl["display_name"] = tmpl_disp;

    JsonObject st = result.createNestedObject("stage");
    st["name"]        = stage_name;
    st["index"]       = brew_get_stage_index();
    st["count"]       = brew_get_stage_count();
    st["instruction"] = instr;
    st["next_label"]  = next_label;

    result["timer_ms"]        = brew_get_timer_ms();
    result["weight_g"]        = brew_get_weight();
    result["flow_rate_g_s"]   = brew_get_flow_rate();
    result["water_weight_g"]  = brew_get_water_weight();
    result["dose_weight_g"]   = brew_get_dose_weight();

    JsonObject tgt = result.createNestedObject("targets");
    tgt["weight_g"]           = brew_get_stage_weight_target();
    tgt["weight_remaining_g"] = brew_get_stage_weight_remaining();
    tgt["flow_rate_g_s"]      = brew_get_stage_flow_target();
    tgt["time_ms"]            = brew_get_stage_time_target_ms();
    tgt["time_remaining_ms"]  = brew_get_stage_time_remaining_ms();
    tgt["time_current_ms"]    = brew_get_stage_time_current_ms();

    uint8_t cap_count = brew_get_capture_count();
    if (cap_count) {
        JsonArray caps = result.createNestedArray("captures");
        for (uint8_t i = 0; i < cap_count; i++) {
            const BrewCapture* c = brew_get_capture(i);
            if (!c) continue;
            JsonObject co = caps.createNestedObject();
            // Wrap in String() so ArduinoJson copies: c points into the static
            // s_captures array, which a concurrent brew can overwrite before the
            // result is serialized (no UAF, but a torn read) — same reason the
            // template/stage strings above are copied.
            co["key"]   = String(c->key);
            co["label"] = String(c->label);
            co["unit"]  = String(c->unit);
            co["value"] = c->value;
        }
    }
    return true;
}

// ============================================================================
// Read tool: get_brew_series — live in-progress series, decimated
// ============================================================================
// The series buffer only exists while a brew is being recorded (allocated on
// first pour, freed shortly after the brew finishes). Per bucket we emit the
// last weight (cumulative) and the peak flow so the pour shape survives
// downsampling. Markers carry the raw sample index so the caller can place stage
// transitions on the original 1 Hz timeline (raw_count = total seconds).

static bool tool_get_brew_series(const JsonObject& args, JsonObject& result, String& err) {
    (void)args;
    BrewSample* buf = (BrewSample*)heap_caps_malloc(
        BREW_SERIES_MAX_SAMPLES * sizeof(BrewSample), MALLOC_CAP_SPIRAM);
    if (!buf) return cs_fail(result, err, CS_ERR_INTERNAL, "out of memory");

    uint16_t count = brew_series_copy(buf, BREW_SERIES_MAX_SAMPLES);
    if (count == 0) {
        heap_caps_free(buf);
        result["available"] = false;
        return true;
    }

    result["available"]   = true;
    result["raw_count"]   = count;       // total 1 Hz samples == seconds recorded
    result["sample_hz"]   = 1;
    result["max_points"]  = (uint32_t)MCP_BREW_SERIES_MAX_POINTS;

    uint32_t bucket = (count + MCP_BREW_SERIES_MAX_POINTS - 1) / MCP_BREW_SERIES_MAX_POINTS;
    if (bucket < 1) bucket = 1;
    result["decimation"] = bucket;

    JsonArray samples = result.createNestedArray("samples");
    for (uint32_t i = 0; i < count; i += bucket) {
        uint32_t end = i + bucket;
        if (end > count) end = count;
        float peak_flow = 0.0f;
        for (uint32_t j = i; j < end; j++) {
            if (fabsf(buf[j].flow) > fabsf(peak_flow)) peak_flow = buf[j].flow;
        }
        JsonObject s = samples.createNestedObject();
        s["t"]      = end - 1;               // second (raw index of last sample in bucket)
        s["weight"] = buf[end - 1].weight;   // cumulative weight → last in bucket
        s["flow"]   = peak_flow;             // peak flow in bucket
    }
    heap_caps_free(buf);

    BrewMarker markers[BREW_MARKER_MAX];
    uint8_t mcount = brew_markers_copy(markers, BREW_MARKER_MAX);
    if (mcount) {
        JsonArray marr = result.createNestedArray("markers");
        for (uint8_t i = 0; i < mcount; i++) {
            JsonObject mo = marr.createNestedObject();
            mo["t"]     = markers[i].sample_index;
            mo["label"] = markers[i].label;
        }
    }
    return true;
}

// ============================================================================
// Read tool: list_brews — saved brew log manifest (newest first, capped)
// ============================================================================

static bool tool_list_brews(const JsonObject& args, JsonObject& result, String& err) {
    (void)args; (void)err;

    result["max"] = (uint32_t)BREW_LOG_MAX_BREWS;
    JsonArray out = result.createNestedArray("brews");

    File dir = LittleFS.open(BREW_LOG_DIR);
    if (!dir || !dir.isDirectory()) {
        result["count"] = 0;
        return true;
    }

    // Collect ids, then sort newest-first.
    uint16_t* ids = (uint16_t*)heap_caps_malloc(BREW_LOG_MAX_BREWS * sizeof(uint16_t),
                                                MALLOC_CAP_SPIRAM);
    if (!ids) return cs_fail(result, err, CS_ERR_INTERNAL, "out of memory");
    uint16_t n = 0;
    for (File f = dir.openNextFile(); f && n < BREW_LOG_MAX_BREWS; f = dir.openNextFile()) {
        if (f.isDirectory()) continue;
        unsigned id = 0;
        if (sscanf(f.name(), "%u.json", &id) == 1) ids[n++] = (uint16_t)id;
    }
    for (uint16_t i = 0; i < n; i++)
        for (uint16_t j = i + 1; j < n; j++)
            if (ids[j] > ids[i]) { uint16_t t = ids[i]; ids[i] = ids[j]; ids[j] = t; }

    result["count"] = n;
    uint16_t emit = (n < MCP_BREW_LIST_MAX) ? n : MCP_BREW_LIST_MAX;
    if (emit < n) result["truncated"] = true;

    for (uint16_t i = 0; i < emit; i++) {
        char path[40];
        snprintf(path, sizeof(path), "%s/%04u.json", BREW_LOG_DIR, (unsigned)ids[i]);
        File bf = LittleFS.open(path, "r");
        if (!bf) continue;
        size_t sz = bf.size();
        if (sz == 0 || sz > 8192) { bf.close(); continue; }

        auto doc = make_psram_json_doc(sz + 256);
        if (!doc || doc->capacity() == 0) { bf.close(); continue; }
        DeserializationError de = deserializeJson(*doc, bf);
        bf.close();
        if (de) continue;

        JsonObject bo = out.createNestedObject();
        bo["id"] = ids[i];
        bo["detail_url"] = String("/api/brews?id=") + (unsigned)ids[i];
        if ((*doc).containsKey("fields"))        bo["fields"]        = (*doc)["fields"];
        if ((*doc).containsKey("template_info")) bo["template_info"] = (*doc)["template_info"];
    }
    heap_caps_free(ids);
    return true;
}

// ============================================================================
// Read tool: get_brew — one saved brew's metadata + detail_url
// ============================================================================
// Returns the brew's summary fields (and template snapshot) only. The full
// per-second weight/flow series is large; stream it from detail_url
// (GET /api/brews?id=N), the same way the portal brew viewer does.

static bool tool_get_brew(const JsonObject& args, JsonObject& result, String& err) {
    long id = args["id"] | -1L;
    if (id < 0) return cs_fail(result, err, CS_ERR_PARAMS, "missing brew id");

    char path[40];
    snprintf(path, sizeof(path), "%s/%04u.json", BREW_LOG_DIR, (unsigned)id);
    if (!LittleFS.exists(path)) return cs_fail(result, err, CS_ERR_PARAMS, "brew not found");

    File bf = LittleFS.open(path, "r");
    if (!bf) return cs_fail(result, err, CS_ERR_INTERNAL, "cannot open brew file");
    size_t sz = bf.size();
    auto doc = make_psram_json_doc(sz + 512);
    if (!doc || doc->capacity() == 0) { bf.close(); return cs_fail(result, err, CS_ERR_INTERNAL, "out of memory"); }
    // Filter out the large per-second `series` array at parse time — it is fetched
    // via detail_url, not inlined here. The filter is a whitelist: only the named
    // keys are kept (series is dropped without building its 600-element tree).
    auto filter = make_psram_json_doc(256);
    if (!filter || filter->capacity() == 0) { bf.close(); return cs_fail(result, err, CS_ERR_INTERNAL, "out of memory"); }
    {
        JsonObject fo = filter->to<JsonObject>();
        fo["v"] = true;
        fo["fields"] = true;
        fo["template_info"] = true;
        fo["markers"] = true;
    }
    DeserializationError de = deserializeJson(*doc, bf, DeserializationOption::Filter(*filter));
    bf.close();
    if (de) return cs_fail(result, err, CS_ERR_INTERNAL, "brew file parse error");

    result["id"] = (uint32_t)id;
    result["detail_url"] = String("/api/brews?id=") + (unsigned)id;
    if ((*doc).containsKey("v"))             result["v"]             = (*doc)["v"];
    if ((*doc).containsKey("fields"))        result["fields"]        = (*doc)["fields"];
    if ((*doc).containsKey("template_info")) result["template_info"] = (*doc)["template_info"];
    if ((*doc).containsKey("markers"))       result["markers"]       = (*doc)["markers"];
    // The full per-second series is intentionally omitted (filtered above);
    // stream it from detail_url.
    return true;
}

// ============================================================================
// Read tool: list_brew_templates — built-in + dynamic template summaries
// ============================================================================

static bool tool_list_brew_templates(const JsonObject& args, JsonObject& result, String& err) {
    (void)args; (void)err;
    uint8_t count = brew_template_count();
    result["count"] = count;
    JsonArray arr = result.createNestedArray("templates");
    for (uint8_t i = 0; i < count; i++) {
        const BrewTemplate* t = brew_template_get(i);
        if (!t) continue;
        JsonObject o = arr.createNestedObject();
        // Copy: dynamic templates can be freed by a concurrent
        // brew_template_loader_reload() before the result is serialized.
        o["name"]         = String(t->name);
        o["display_name"] = String(t->display_name);
        o["description"]  = String(t->description);
        o["stage_count"]  = t->stage_count;
        o["is_dynamic"]   = t->is_dynamic;
    }
    return true;
}

// ============================================================================
// Read tool: get_brew_template — one template serialized to JSON DSL
// ============================================================================

static bool tool_get_brew_template(const JsonObject& args, JsonObject& result, String& err) {
    const char* name = args["name"] | (const char*)nullptr;
    if (!name || !name[0]) return cs_fail(result, err, CS_ERR_PARAMS, "missing template name");

    const BrewTemplate* t = brew_template_find(name);
    // brew_template_find falls back to "free_pour" for unknown names.
    if (!t || strcmp(t->name, name) != 0)
        return cs_fail(result, err, CS_ERR_PARAMS, "template not found");

    char* buf = (char*)heap_caps_malloc(4096, MALLOC_CAP_SPIRAM);
    if (!buf) return cs_fail(result, err, CS_ERR_INTERNAL, "out of memory");
    int len = brew_dsl_serialize(t, buf, 4096);
    if (len < 0) { heap_caps_free(buf); return cs_fail(result, err, CS_ERR_INTERNAL, "serialize failed"); }

    auto tdoc = make_psram_json_doc(len + 256);
    if (!tdoc || tdoc->capacity() == 0) { heap_caps_free(buf); return cs_fail(result, err, CS_ERR_INTERNAL, "out of memory"); }
    DeserializationError de = deserializeJson(*tdoc, buf, len);
    heap_caps_free(buf);
    if (de) return cs_fail(result, err, CS_ERR_INTERNAL, "serialize parse error");

    // Copy the name (built-in literal or dynamic-template heap) into the result
    // doc; the template subtree is already deep-copied across documents.
    result["name"]     = String(t->name);
    result["template"] = tdoc->as<JsonObject>();
    return true;
}

// ============================================================================
// Control tool: scale_control — deferred ButtonAction dispatch
// ============================================================================

struct CsCtrlCtx {
    char type[8];                       // ACTION_TYPE_SCALE or ACTION_TYPE_BREW
    char command[SCALE_CMD_MAX_LEN];    // widest of the two command fields
    char value[CONFIG_BINDABLE_SHORT_LEN];
};

static void exec_cs_control(const void* ctx, bool* ok, char* msg, size_t msg_len) {
    const CsCtrlCtx* c = (const CsCtrlCtx*)ctx;
    ButtonAction act;
    memset(&act, 0, sizeof(act));
    strlcpy(act.type, c->type, sizeof(act.type));
    if (strcmp(c->type, ACTION_TYPE_SCALE) == 0) {
        ScalePayload& p = scale_payload(act);
        strlcpy(p.command, c->command, sizeof(p.command));
        strlcpy(p.value,   c->value,   sizeof(p.value));
    } else {
        BrewPayload& p = brew_payload(act);
        strlcpy(p.command, c->command, sizeof(p.command));
        strlcpy(p.value,   c->value,   sizeof(p.value));
    }
    action_dispatch(act, "MCP");
    *ok = true;
    snprintf(msg, msg_len, "dispatched %s", c->command);
}

static bool finish_control(McpControlResult r, bool ok, const char* msg,
                           JsonObject& result, String& err) {
    if (r == MCP_CONTROL_BUSY)    return cs_fail(result, err, CS_ERR_BUSY, "another control action is in progress");
    if (r == MCP_CONTROL_TIMEOUT) return cs_fail(result, err, CS_ERR_INTERNAL, "control action timed out");
    if (!ok)                      return cs_fail(result, err, CS_ERR_INTERNAL, msg && msg[0] ? msg : "control action failed");
    // Wrap in String() so ArduinoJson COPIES the text into the result document.
    // `msg` points at the caller's stack buffer; assigning it as a const char*
    // would only link the pointer, which dangles once the handler returns and
    // before the dispatcher serializes the result (garbage output).
    if (msg && msg[0]) result["status"] = String(msg);
    else               result["status"] = "ok";
    return true;
}

static bool dispatch_cs_control(const char* type, const char* cmd, const char* value,
                                JsonObject& result, String& err) {
    CsCtrlCtx ctx;
    memset(&ctx, 0, sizeof(ctx));
    strlcpy(ctx.type, type, sizeof(ctx.type));
    strlcpy(ctx.command, cmd, sizeof(ctx.command));
    strlcpy(ctx.value, value ? value : "", sizeof(ctx.value));

    bool ok = false;
    char msg[96];
    msg[0] = '\0';
    McpControlResult r = mcp_control_dispatch(exec_cs_control, &ctx, sizeof(ctx),
                                              CS_CONTROL_TIMEOUT_MS, &ok, msg, sizeof(msg));
    return finish_control(r, ok, msg, result, err);
}

static const char* const kScaleCommands[] = { "tare", "calibrate", "cal_weight", "cal_weight_set" };

static bool tool_scale_control(const JsonObject& args, JsonObject& result, String& err) {
    const char* cmd = args["command"] | (const char*)nullptr;
    if (!cmd || !cmd[0]) return cs_fail(result, err, CS_ERR_PARAMS, "missing command");

    bool known = false;
    for (const char* c : kScaleCommands) if (strcmp(c, cmd) == 0) { known = true; break; }
    if (!known) return cs_fail(result, err, CS_ERR_PARAMS, "unknown scale command");

    const char* value = args["value"] | "";

    if (strcmp(cmd, "cal_weight") == 0) {
        if (!value[0] || strtof(value, nullptr) == 0.0f)
            return cs_fail(result, err, CS_ERR_PARAMS, "cal_weight requires a non-zero gram delta");
    } else if (strcmp(cmd, "cal_weight_set") == 0) {
        if (strtof(value, nullptr) < 1.0f)
            return cs_fail(result, err, CS_ERR_PARAMS, "cal_weight_set requires a value >= 1 (grams)");
    }

    return dispatch_cs_control(ACTION_TYPE_SCALE, cmd, value, result, err);
}

// ============================================================================
// Control tool: brew_control — deferred ButtonAction dispatch
// ============================================================================

static const char* const kBrewCommands[] = {
    "set_template", "advance", "start", "next", "stop", "reset", "tare",
};

static bool tool_brew_control(const JsonObject& args, JsonObject& result, String& err) {
    const char* cmd = args["command"] | (const char*)nullptr;
    if (!cmd || !cmd[0]) return cs_fail(result, err, CS_ERR_PARAMS, "missing command");

    bool known = false;
    for (const char* c : kBrewCommands) if (strcmp(c, cmd) == 0) { known = true; break; }
    if (!known) return cs_fail(result, err, CS_ERR_PARAMS, "unknown brew command");

    const char* value = args["value"] | "";

    if (strcmp(cmd, "set_template") == 0) {
        if (!value[0]) return cs_fail(result, err, CS_ERR_PARAMS, "set_template requires value (a template name from list_brew_templates)");
        const BrewTemplate* t = brew_template_find(value);
        if (!t || strcmp(t->name, value) != 0)
            return cs_fail(result, err, CS_ERR_PARAMS, "template name not found; see list_brew_templates");
    }

    return dispatch_cs_control(ACTION_TYPE_BREW, cmd, value, result, err);
}

// ============================================================================
// Control tool: delete_brew — remove a saved brew (plain LittleFS I/O)
// ============================================================================

static bool tool_delete_brew(const JsonObject& args, JsonObject& result, String& err) {
    long id = args["id"] | -1L;
    if (id < 0) return cs_fail(result, err, CS_ERR_PARAMS, "missing brew id");

    char path[40];
    snprintf(path, sizeof(path), "%s/%04u.json", BREW_LOG_DIR, (unsigned)id);
    if (!LittleFS.exists(path)) return cs_fail(result, err, CS_ERR_PARAMS, "brew not found");
    if (!LittleFS.remove(path)) return cs_fail(result, err, CS_ERR_INTERNAL, "delete failed");
    fs_health_set_storage_usage(LittleFS.usedBytes(), LittleFS.totalBytes());

    LOGI(TAG, "Deleted brew %ld via MCP", id);
    result["status"] = "deleted";
    result["id"]     = (uint32_t)id;
    return true;
}

// ============================================================================
// Control tool: delete_brew_template — remove a dynamic template
// ============================================================================

static bool tool_delete_brew_template(const JsonObject& args, JsonObject& result, String& err) {
    const char* name = args["name"] | (const char*)nullptr;
    if (!name || !name[0]) return cs_fail(result, err, CS_ERR_PARAMS, "missing template name");
    if (strstr(name, "..") || strchr(name, '/') || strchr(name, '\\'))
        return cs_fail(result, err, CS_ERR_PARAMS, "invalid template name");

    char path[80];
    snprintf(path, sizeof(path), BREW_TEMPLATE_DIR "/%s.json", name);
    if (!LittleFS.exists(path)) {
        const BrewTemplate* t = brew_template_find(name);
        if (t && !t->is_dynamic && strcmp(t->name, name) == 0)
            return cs_fail(result, err, CS_ERR_PARAMS, "cannot delete a built-in template");
        return cs_fail(result, err, CS_ERR_PARAMS, "template file not found");
    }

    LittleFS.remove(path);
    brew_template_loader_reload();
    fs_health_set_storage_usage(LittleFS.usedBytes(), LittleFS.totalBytes());

    const BrewTemplate* t = brew_template_find(name);
    bool reset = (t && !t->is_dynamic && strcmp(t->name, name) == 0);
    LOGI(TAG, "Deleted template %s%s via MCP", name, reset ? " (reset to built-in)" : "");
    result["status"] = reset ? "reset_to_default" : "deleted";
    result["name"]   = name;
    return true;
}

// ============================================================================
// Authoring tool: set_brew_template — write/replace one brew template
// (plain LittleFS write — runs inline like the portal POST handler)
// ============================================================================

static bool tool_set_brew_template(const JsonObject& args, JsonObject& result, String& err) {
    const char* content = args["content"] | (const char*)nullptr;
    if (!content) return cs_fail(result, err, CS_ERR_PARAMS, "missing content");
    size_t len = strlen(content);
    if (len == 0) return cs_fail(result, err, CS_ERR_PARAMS, "empty content");
    if (len > MCP_BREW_TEMPLATE_MAX_BYTES) return cs_fail(result, err, CS_ERR_PARAMS, "content too large");

    // Validate by parsing the DSL. brew_dsl_parse heap-allocates the template
    // and stage array; free them — we persist the raw JSON, not a re-serialize.
    BrewTemplate* tmpl = nullptr;
    BrewStage* stages = nullptr;
    char perr[80] = {};
    int rc = brew_dsl_parse(content, len, &tmpl, &stages, perr, sizeof(perr));
    if (rc != BREW_DSL_OK) {
        if (stages) delete[] stages;
        if (tmpl) delete tmpl;
        return cs_fail(result, err, CS_ERR_PARAMS, perr[0] ? perr : "invalid template JSON");
    }

    char name[24];
    strlcpy(name, tmpl->name, sizeof(name));
    delete[] stages;
    delete tmpl;

    if (!name[0] || strstr(name, "..") || strchr(name, '/') || strchr(name, '\\'))
        return cs_fail(result, err, CS_ERR_PARAMS, "invalid template name");

    if (!LittleFS.exists(BREW_TEMPLATE_DIR)) LittleFS.mkdir(BREW_TEMPLATE_DIR);

    char path[80];
    snprintf(path, sizeof(path), BREW_TEMPLATE_DIR "/%s.json", name);
    File f = LittleFS.open(path, "w");
    if (!f) return cs_fail(result, err, CS_ERR_INTERNAL, "cannot open template file");
    size_t written = f.write((const uint8_t*)content, len);
    f.close();
    if (written != len) return cs_fail(result, err, CS_ERR_INTERNAL, "write failed");

    brew_template_loader_reload();
    fs_health_set_storage_usage(LittleFS.usedBytes(), LittleFS.totalBytes());
    LOGI(TAG, "Saved brew template '%s' via MCP (%u bytes)", name, (unsigned)written);
    result["status"]        = "saved";
    result["name"]          = name;
    result["bytes_written"] = (uint32_t)written;
    return true;
}

// ============================================================================
// Tool descriptors + registration
// ============================================================================

static const McpTool s_tool_get_scale_status = {
    "get_scale_status",
    "Get the coffee scale's live state: weight (g), flow rate (g/s), availability, calibration factor + raw offset, calibration reference weight, status string, and the active smoothing preset.",
    "{\"type\":\"object\",\"properties\":{}}",
    tool_get_scale_status, true, false, false, false
};
REGISTER_MCP_TOOL(s_tool_get_scale_status);

static const McpTool s_tool_get_brew_status = {
    "get_brew_status",
    "Get the brew state machine snapshot: phase (idle/active/done), active template (name + display), current stage (name, index, count, instruction, advance-button label), brew timer, live weight/flow, water + dose weight, current-stage targets (weight/flow/time with remaining), and any captured data points.",
    "{\"type\":\"object\",\"properties\":{}}",
    tool_get_brew_status, true, false, false, false
};
REGISTER_MCP_TOOL(s_tool_get_brew_status);

static const McpTool s_tool_get_brew_series = {
    "get_brew_series",
    "Get the in-progress brew's recorded weight/flow time-series (1 Hz), decimated (last-weight + peak-flow per bucket) to bound payload size. Includes stage-transition markers with their raw second index. Available only while a brew is recording; for finished/saved brews use get_brew's detail_url instead.",
    "{\"type\":\"object\",\"properties\":{}}",
    tool_get_brew_series, true, false, false, false
};
REGISTER_MCP_TOOL(s_tool_get_brew_series);

static const McpTool s_tool_list_brews = {
    "list_brews",
    "List saved brews (newest first, capped): id, summary fields, template snapshot, and a detail_url. The full per-second series is not inlined — fetch it via detail_url (GET /api/brews?id=N).",
    "{\"type\":\"object\",\"properties\":{}}",
    tool_list_brews, true, false, false, false
};
REGISTER_MCP_TOOL(s_tool_list_brews);

static const McpTool s_tool_get_brew = {
    "get_brew",
    "Get one saved brew's summary fields, template snapshot, markers, and a detail_url. The full per-second weight/flow series is large and is fetched by streaming detail_url (GET /api/brews?id=N), not inlined here. Args: id (integer).",
    "{\"type\":\"object\",\"properties\":{\"id\":{\"type\":\"integer\"}},\"required\":[\"id\"]}",
    tool_get_brew, true, false, false, false
};
REGISTER_MCP_TOOL(s_tool_get_brew);

static const McpTool s_tool_list_brew_templates = {
    "list_brew_templates",
    "List brew templates (built-in + user): name, display_name, description, stage_count, is_dynamic.",
    "{\"type\":\"object\",\"properties\":{}}",
    tool_list_brew_templates, true, false, false, false
};
REGISTER_MCP_TOOL(s_tool_list_brew_templates);

static const McpTool s_tool_get_brew_template = {
    "get_brew_template",
    "Get one brew template serialized to its JSON DSL (stages, effects, targets). Args: name (machine name from list_brew_templates).",
    "{\"type\":\"object\",\"properties\":{\"name\":{\"type\":\"string\"}},\"required\":[\"name\"]}",
    tool_get_brew_template, true, false, false, false
};
REGISTER_MCP_TOOL(s_tool_get_brew_template);

static const McpTool s_tool_scale_control = {
    "scale_control",
    "Run a scale control command. command (required): tare | calibrate | cal_weight | cal_weight_set. value rules: REQUIRED for 'cal_weight' (a non-zero gram delta applied to the calibration reference weight) and 'cal_weight_set' (an absolute reference weight in grams, >= 1); ignored for 'tare' and 'calibrate'. 'calibrate' uses the current reference weight. Smoothing preset is a config setting (see get_scale_status), not a control command.",
    "{\"type\":\"object\",\"properties\":{\"command\":{\"type\":\"string\",\"enum\":[\"tare\",\"calibrate\",\"cal_weight\",\"cal_weight_set\"]},\"value\":{\"type\":\"string\"}},\"required\":[\"command\"]}",
    tool_scale_control, false, false, true, false
};
REGISTER_MCP_TOOL(s_tool_scale_control);

static const McpTool s_tool_brew_control = {
    "brew_control",
    "Run a brew control command. command (required): set_template | advance | start | next | stop | reset | tare. value rules: REQUIRED for 'set_template' (a template machine name from list_brew_templates — primes the next brew); ignored for all other commands. 'advance' is the smart single-button action (start when idle, advance a manual stage, stop a running timer, restart when done). Unknown template names are rejected.",
    "{\"type\":\"object\",\"properties\":{\"command\":{\"type\":\"string\",\"enum\":[\"set_template\",\"advance\",\"start\",\"next\",\"stop\",\"reset\",\"tare\"]},\"value\":{\"type\":\"string\"}},\"required\":[\"command\"]}",
    tool_brew_control, false, false, true, false
};
REGISTER_MCP_TOOL(s_tool_brew_control);

static const McpTool s_tool_delete_brew = {
    "delete_brew",
    "Delete a saved brew by id (destructive). Args: id (integer).",
    "{\"type\":\"object\",\"properties\":{\"id\":{\"type\":\"integer\"}},\"required\":[\"id\"]}",
    tool_delete_brew, false, true, true, false
};
REGISTER_MCP_TOOL(s_tool_delete_brew);

static const McpTool s_tool_delete_brew_template = {
    "delete_brew_template",
    "Delete a user (dynamic) brew template by name (destructive). Built-in templates cannot be deleted; if a deleted name shadows a built-in, the built-in re-emerges. Args: name.",
    "{\"type\":\"object\",\"properties\":{\"name\":{\"type\":\"string\"}},\"required\":[\"name\"]}",
    tool_delete_brew_template, false, true, true, false
};
REGISTER_MCP_TOOL(s_tool_delete_brew_template);

static const McpTool s_tool_set_brew_template = {
    "set_brew_template",
    "Create or replace one brew template. Use this tool directly; do not POST to any HTTP endpoint. `content` is the template's JSON DSL (the same format get_brew_template returns and the portal's template editor uses). The template's `name` field is the machine id and the filename. Schema (v1): "
    "{\"v\":1,\"name\":\"my_v60\",\"display_name\":\"My V60\",\"description\":\"...\",\"stages\":[{\"name\":\"Dose\",\"instruction\":\"Add coffee\",\"type\":\"manual|auto_weight|auto_time\",\"on_enter\":[\"tare\"],\"on_exit\":[\"capture_dose\"],\"auto_threshold\":2.0,\"target_weight\":60.0,\"target_flow_rate\":6.0,\"auto_time_s\":45}]}. "
    "Stage `type` is manual (user advances), auto_weight (advances at target/auto_threshold g) or auto_time (advances after auto_time_s). Effects in on_enter/on_exit: tare, beep, capture_dose, marker, capture_weight. Max 16 stages. Validated before saving; call get_brew_template afterward to confirm.",
    "{\"type\":\"object\",\"properties\":{\"content\":{\"type\":\"string\",\"description\":\"Full brew-template JSON in the v1 DSL described in this tool's description.\"}},\"required\":[\"content\"]}",
    tool_set_brew_template, false, false, false, true
};
REGISTER_MCP_TOOL(s_tool_set_brew_template);

#endif // HAS_MCP && IS_COFFEE_SCALE
