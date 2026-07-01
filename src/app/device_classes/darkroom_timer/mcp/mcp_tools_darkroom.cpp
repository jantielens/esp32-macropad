// ============================================================================
// mcp_tools_darkroom.cpp — Darkroom Timer device-class MCP tools.
//
// Aggregated into the build via src/app/mcp_components.cpp (arduino-cli only
// compiles .cpp files in the sketch root). Doubly-gated HAS_MCP &&
// IS_DARKROOM_TIMER so non-darkroom firmware never compiles a byte of this.
//
// Ownership: every darkroom MCP tool lives here, inside the darkroom_timer
// device-class folder. The only hook outside this folder is the one-line
// #include in mcp_components.cpp under the IS_DARKROOM_TIMER gate.
//
// Threading contract (see mcp_tool_registry.h):
//   - READ tools run on the AsyncWebServer (web) task. The engine status getters
//     (expose_timer_get_status / test_strip_get_status / the meter_get_*
//     accessors) copy scalar state in one call. The expose/strip engines keep
//     their state in plain structs updated on the main loop and already read
//     lock-free by the binding resolvers on the LVGL task, so a third (web-task)
//     reader of the same scalars is the same benign access; the meter accessors
//     take the meter's own portMUX where it already guards mag readings. The
//     print read tools (list_prints / get_print) are plain Storage I/O, the same
//     way the existing GET /api/prints handlers run on the web task.
//   - CONTROL tools (expose_control / strip_control / meter_control /
//     print_control) build a ButtonAction and defer to the main loop via
//     mcp_control_dispatch() (exactly like the coffee-scale tools) — engine
//     command dispatch runs through action_dispatch(), which may touch
//     LVGL/audio and must not run on the web task. relay_control calls
//     relay_request(), which is documented safe from any task (it sets a bitmask
//     and signals the relay task), so it runs inline.
//   - delete_print / delete_all_prints / set_relay_config are plain Storage I/O
//     (no LVGL, no action dispatch) and run inline on the web task, the same way
//     the existing portal DELETE/PUT handlers do.
//
// Saved-print exposure: print records are small (<= 4 KB), so get_print inlines
// the full record (fields, metering context, segments, notes, star) and also
// returns a detail_url (GET /api/prints?id=ID) for the raw file stream.
// ============================================================================

#include "board_config.h"

#if HAS_MCP && IS_DARKROOM_TIMER

#include "mcp_tool_registry.h"
#include "web_mcp.h"

#include "../expose_timer.h"
#include "../test_strip.h"
#include "../meter.h"
#include "../relay_controller.h"
#include "../print_log.h"
#include "../darkroom_timer_payload.h"  // ACTION_TYPE_* + payload accessors

#include "action_dispatch.h"
#include "storage.h"
#include "web_portal_json.h"   // make_psram_json_doc
#include "log_manager.h"

#include <ArduinoJson.h>
#include <esp_heap_caps.h>
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define TAG "DarkroomMCP"

// JSON-RPC error codes (canonical values in mcp_tool_registry.h).
static constexpr int DR_ERR_PARAMS   = MCP_RPC_ERR_PARAMS;
static constexpr int DR_ERR_INTERNAL = MCP_RPC_ERR_INTERNAL;
static constexpr int DR_ERR_BUSY     = MCP_RPC_ERR_CONTROL_BUSY;

static constexpr uint32_t DR_CONTROL_TIMEOUT_MS = 3000;

// Cap list_prints output so the response stays inside the dispatcher's PSRAM
// result document (newest-first; older prints are reachable individually via
// get_print). The portal's full list is streamed/paged separately.
#ifndef MCP_PRINT_LIST_MAX
#define MCP_PRINT_LIST_MAX 40
#endif

// Print id format is YYMMDD-NNN (<= 10 chars). Allow a little slack.
#define MCP_PRINT_ID_MAX_LEN 18

// Safety limit for the uploaded relay-config JSON (mirrors the 4 KB cap in the
// portal PUT /api/relay handler).
#ifndef MCP_RELAY_CONFIG_MAX_BYTES
#define MCP_RELAY_CONFIG_MAX_BYTES 4096
#endif

static bool dr_fail(JsonObject& result, String& err, int code, const char* msg) {
    err = msg ? msg : "error";
    result[MCP_RESULT_ERRCODE_KEY] = code;
    return false;
}

// True when a print id is well-formed and path-safe.
static bool dr_valid_print_id(const char* id) {
    if (!id || !id[0]) return false;
    size_t n = strlen(id);
    if (n > MCP_PRINT_ID_MAX_LEN) return false;
    if (strstr(id, "..") || strchr(id, '/') || strchr(id, '\\')) return false;
    return true;
}

// ============================================================================
// Read tool: get_expose_status — single-exposure countdown timer state
// ============================================================================

static bool tool_get_expose_status(const JsonObject& args, JsonObject& result, String& err) {
    (void)args; (void)err;
    ExposeStatus s;
    expose_timer_get_status(&s);
    result["state"]            = expose_state_str(s.state);
    result["set_time_s"]       = s.set_time_s;
    result["effective_time_s"] = s.effective_time_s;
    result["dry_down_pct"]     = s.dry_down_pct;
    result["remaining_s"]      = s.remaining_ms / 1000.0f;
    result["elapsed_s"]        = s.elapsed_ms / 1000.0f;
    result["relay_on"]         = relay_is_on();
    return true;
}

// ============================================================================
// Read tool: get_strip_status — f-stop test strip sequencer state
// ============================================================================

static bool tool_get_strip_status(const JsonObject& args, JsonObject& result, String& err) {
    (void)args; (void)err;
    StripStatus s;
    test_strip_get_status(&s);
    result["state"]         = test_strip_phase_str(s.phase);
    result["segment"]       = s.segment;
    result["segment_count"] = s.segment_count;
    result["base_time_s"]   = s.base_time_s;
    result["step_stops"]    = s.step_stops;
    result["step_label"]    = s.step_label;       // static storage, safe as const char*
    result["countdown_s"]   = s.countdown_s;
    result["pause_s"]       = s.pause_s;
    result["exposure_tick"] = s.exposure_tick;
    result["remaining_s"]   = s.phase_remaining_ms / 1000.0f;
    result["elapsed_s"]     = s.phase_elapsed_ms / 1000.0f;
    result["total_time_s"]  = s.total_time_s;
    result["relay_on"]      = relay_is_on();

    int n = s.segment_count;
    if (n > STRIP_STATUS_MAX_SEGMENTS) n = STRIP_STATUS_MAX_SEGMENTS;
    JsonArray segs = result.createNestedArray("segments");
    for (int i = 0; i < n; i++) {
        JsonObject o = segs.createNestedObject();
        o["n"]            = i + 1;
        o["cumulative_s"] = s.segments[i].cumulative_s;
        o["incremental_s"] = s.segments[i].incremental_s;
        o["offset_stops"] = s.segments[i].offset_stops;
    }
    return true;
}

// ============================================================================
// Read tool: get_meter_status — print-prep light meter state
// ============================================================================
// Sentinels: lref/zone5_time == 0 means not set; l_bright/l_dark/time_s == -1
// means not read/not computable; mag_lux_a/b == -1 means not measured.

static bool tool_get_meter_status(const JsonObject& args, JsonObject& result, String& err) {
    (void)args; (void)err;
    result["lref"]        = meter_get_lref();
    result["zone5_time"]  = meter_get_zone5_time();
    result["l_bright"]    = meter_get_bright();
    result["l_dark"]      = meter_get_dark();
    result["sbr"]         = meter_get_sbr();
    result["grade"]       = meter_get_grade();
    result["grade_label"] = meter_get_grade_label();
    result["time_s"]      = meter_get_time();
    result["has_results"] = meter_get_has_results();
    result["mag_lux_a"]   = meter_get_mag_lux_a();
    result["mag_lux_b"]   = meter_get_mag_lux_b();
    result["mag_factor"]  = meter_get_mag_factor();
    return true;
}

// ============================================================================
// Read tool: get_relay_config — enlarger/safelight relay action slots
// ============================================================================

static bool tool_get_relay_config(const JsonObject& args, JsonObject& result, String& err) {
    (void)args;
    String cfg;
    if (!relay_get_config_json(cfg)) return dr_fail(result, err, DR_ERR_INTERNAL, "failed to serialize relay config");

    auto doc = make_psram_json_doc(cfg.length() + 256);
    if (!doc || doc->capacity() == 0) return dr_fail(result, err, DR_ERR_INTERNAL, "out of memory");
    DeserializationError de = deserializeJson(*doc, cfg);
    if (de) return dr_fail(result, err, DR_ERR_INTERNAL, "relay config parse error");
    result["config"] = doc->as<JsonVariant>();
    return true;
}

// ============================================================================
// Read tool: list_prints — saved print-session manifest (newest first, capped)
// ============================================================================

static bool tool_list_prints(const JsonObject& args, JsonObject& result, String& err) {
    (void)args; (void)err;

    result["max"] = (uint32_t)DARKROOM_PRINT_LOG_MAX;
    result["count"] = print_log_get_count();
    JsonArray out = result.createNestedArray("prints");

    File dir = Storage.open(PRINT_LOG_DIR);
    if (!dir || !dir.isDirectory()) return true;

    // Collect ids (filename minus .json), then sort newest-first. IDs are
    // YYMMDD-NNN, so lexicographic-descending == chronological-descending.
    char (*ids)[MCP_PRINT_ID_MAX_LEN + 1] =
        (char(*)[MCP_PRINT_ID_MAX_LEN + 1])heap_caps_malloc(
            DARKROOM_PRINT_LOG_MAX * (MCP_PRINT_ID_MAX_LEN + 1), MALLOC_CAP_SPIRAM);
    if (!ids) return dr_fail(result, err, DR_ERR_INTERNAL, "out of memory");

    uint16_t n = 0;
    for (File f = dir.openNextFile(); f && n < DARKROOM_PRINT_LOG_MAX; f = dir.openNextFile()) {
        if (f.isDirectory()) continue;
        const char* name = f.name();
        size_t nlen = strlen(name);
        if (nlen > 5 && strcmp(name + nlen - 5, ".json") == 0) {
            size_t id_len = nlen - 5;
            if (id_len <= MCP_PRINT_ID_MAX_LEN) {
                memcpy(ids[n], name, id_len);
                ids[n][id_len] = '\0';
                n++;
            }
        }
    }
    for (uint16_t i = 0; i < n; i++)
        for (uint16_t j = i + 1; j < n; j++)
            if (strcmp(ids[j], ids[i]) > 0) {
                char tmp[MCP_PRINT_ID_MAX_LEN + 1];
                memcpy(tmp, ids[i], sizeof(tmp));
                memcpy(ids[i], ids[j], sizeof(tmp));
                memcpy(ids[j], tmp, sizeof(tmp));
            }

    uint16_t emit = (n < MCP_PRINT_LIST_MAX) ? n : MCP_PRINT_LIST_MAX;
    if (emit < n) result["truncated"] = true;

    for (uint16_t i = 0; i < emit; i++) {
        char path[48];
        snprintf(path, sizeof(path), "%s/%s.json", PRINT_LOG_DIR, ids[i]);
        File pf = Storage.open(path, "r");
        if (!pf) continue;
        size_t sz = pf.size();
        if (sz == 0 || sz > 4096) { pf.close(); continue; }

        auto doc = make_psram_json_doc(sz + 256);
        if (!doc || doc->capacity() == 0) { pf.close(); continue; }
        DeserializationError de = deserializeJson(*doc, pf);
        pf.close();
        if (de) continue;

        JsonObject po = out.createNestedObject();
        po["id"] = ids[i];
        po["detail_url"] = String("/api/prints?id=") + ids[i];
        if ((*doc).containsKey("fields"))  po["fields"]  = (*doc)["fields"];
        if ((*doc).containsKey("notes"))   po["notes"]   = (*doc)["notes"];
        po["starred"] = (*doc)["starred"] | false;
    }
    heap_caps_free(ids);
    return true;
}

// ============================================================================
// Read tool: get_print — one saved print's full record + detail_url
// ============================================================================

static bool tool_get_print(const JsonObject& args, JsonObject& result, String& err) {
    const char* id = args["id"] | (const char*)nullptr;
    if (!dr_valid_print_id(id)) return dr_fail(result, err, DR_ERR_PARAMS, "missing or invalid print id");

    char path[48];
    snprintf(path, sizeof(path), "%s/%s.json", PRINT_LOG_DIR, id);
    if (!Storage.exists(path)) return dr_fail(result, err, DR_ERR_PARAMS, "print not found");

    File pf = Storage.open(path, "r");
    if (!pf) return dr_fail(result, err, DR_ERR_INTERNAL, "cannot open print file");
    size_t sz = pf.size();
    if (sz == 0 || sz > 8192) { pf.close(); return dr_fail(result, err, DR_ERR_INTERNAL, "print file too large"); }

    auto doc = make_psram_json_doc(sz + 512);
    if (!doc || doc->capacity() == 0) { pf.close(); return dr_fail(result, err, DR_ERR_INTERNAL, "out of memory"); }
    DeserializationError de = deserializeJson(*doc, pf);
    pf.close();
    if (de) return dr_fail(result, err, DR_ERR_INTERNAL, "print file parse error");

    result["id"] = id;
    result["detail_url"] = String("/api/prints?id=") + id;
    // Print records are small; inline the whole record.
    result["record"] = doc->as<JsonVariant>();
    return true;
}

// ============================================================================
// Control bridge — build a ButtonAction and defer to the main loop
// ============================================================================

struct DrCtrlCtx {
    char type[8];      // ACTION_TYPE_EXPOSE/STRIP/METER/PRINT
    char command[24];
    char value[24];
};

static void exec_dr_control(const void* ctx, bool* ok, char* msg, size_t msg_len) {
    const DrCtrlCtx* c = (const DrCtrlCtx*)ctx;
    ButtonAction act;
    memset(&act, 0, sizeof(act));
    strlcpy(act.type, c->type, sizeof(act.type));
    if (strcmp(c->type, ACTION_TYPE_EXPOSE) == 0) {
        ExposePayload& p = expose_payload(act);
        strlcpy(p.command, c->command, sizeof(p.command));
        strlcpy(p.value,   c->value,   sizeof(p.value));
    } else if (strcmp(c->type, ACTION_TYPE_STRIP) == 0) {
        StripPayload& p = strip_payload(act);
        strlcpy(p.command, c->command, sizeof(p.command));
        strlcpy(p.value,   c->value,   sizeof(p.value));
    } else if (strcmp(c->type, ACTION_TYPE_METER) == 0) {
        MeterPayload& p = meter_payload(act);
        strlcpy(p.command, c->command, sizeof(p.command));
        strlcpy(p.value,   c->value,   sizeof(p.value));
    } else {
        PrintPayload& p = print_payload(act);
        strlcpy(p.command, c->command, sizeof(p.command));
        strlcpy(p.value,   c->value,   sizeof(p.value));
    }
    action_dispatch(act, "MCP");
    *ok = true;
    snprintf(msg, msg_len, "dispatched %s", c->command);
}

static bool finish_control(McpControlResult r, bool ok, const char* msg,
                           JsonObject& result, String& err) {
    if (r == MCP_CONTROL_BUSY)    return dr_fail(result, err, DR_ERR_BUSY, "another control action is in progress");
    if (r == MCP_CONTROL_TIMEOUT) return dr_fail(result, err, DR_ERR_INTERNAL, "control action timed out");
    if (!ok)                      return dr_fail(result, err, DR_ERR_INTERNAL, msg && msg[0] ? msg : "control action failed");
    // Wrap in String() so ArduinoJson COPIES the text into the result document;
    // `msg` points at the caller's stack buffer and would dangle otherwise.
    if (msg && msg[0]) result["status"] = String(msg);
    else               result["status"] = "ok";
    return true;
}

static bool dispatch_dr_control(const char* type, const char* cmd, const char* value,
                                JsonObject& result, String& err) {
    DrCtrlCtx ctx;
    memset(&ctx, 0, sizeof(ctx));
    strlcpy(ctx.type, type, sizeof(ctx.type));
    strlcpy(ctx.command, cmd, sizeof(ctx.command));
    strlcpy(ctx.value, value ? value : "", sizeof(ctx.value));

    bool ok = false;
    char msg[96];
    msg[0] = '\0';
    McpControlResult r = mcp_control_dispatch(exec_dr_control, &ctx, sizeof(ctx),
                                              DR_CONTROL_TIMEOUT_MS, &ok, msg, sizeof(msg));
    return finish_control(r, ok, msg, result, err);
}

static bool cmd_in(const char* cmd, const char* const* list, size_t count) {
    for (size_t i = 0; i < count; i++) if (strcmp(cmd, list[i]) == 0) return true;
    return false;
}

// ============================================================================
// Control tool: expose_control
// ============================================================================

static const char* const kExposeCommands[] = {
    "start", "stop", "toggle", "pause", "resume", "reset",
    "focus", "focus_off", "focus_toggle",
    "set_time", "adjust_seconds", "adjust_stops", "set_dry_down", "adjust_dry_down",
};

static bool tool_expose_control(const JsonObject& args, JsonObject& result, String& err) {
    const char* cmd = args["command"] | (const char*)nullptr;
    if (!cmd || !cmd[0]) return dr_fail(result, err, DR_ERR_PARAMS, "missing command");
    if (!cmd_in(cmd, kExposeCommands, sizeof(kExposeCommands) / sizeof(kExposeCommands[0])))
        return dr_fail(result, err, DR_ERR_PARAMS, "unknown expose command");
    const char* value = args["value"] | "";
    return dispatch_dr_control(ACTION_TYPE_EXPOSE, cmd, value, result, err);
}

// ============================================================================
// Control tool: strip_control
// ============================================================================

static const char* const kStripCommands[] = {
    "start", "cancel", "set_base", "adjust_base", "step_up", "step_down",
    "adjust_segments", "set_segments", "set_countdown", "adjust_countdown",
    "set_pause", "adjust_pause", "set_tick",
};

static bool tool_strip_control(const JsonObject& args, JsonObject& result, String& err) {
    const char* cmd = args["command"] | (const char*)nullptr;
    if (!cmd || !cmd[0]) return dr_fail(result, err, DR_ERR_PARAMS, "missing command");
    if (!cmd_in(cmd, kStripCommands, sizeof(kStripCommands) / sizeof(kStripCommands[0])))
        return dr_fail(result, err, DR_ERR_PARAMS, "unknown strip command");
    const char* value = args["value"] | "";
    return dispatch_dr_control(ACTION_TYPE_STRIP, cmd, value, result, err);
}

// ============================================================================
// Control tool: meter_control
// ============================================================================

static const char* const kMeterCommands[] = {
    "read_lref", "read_bright", "read_dark",
    "set_lref", "adjust_lref", "set_zone5", "adjust_zone5",
    "mag_measure_a", "mag_measure_b", "mag_clear",
};

static bool tool_meter_control(const JsonObject& args, JsonObject& result, String& err) {
    const char* cmd = args["command"] | (const char*)nullptr;
    if (!cmd || !cmd[0]) return dr_fail(result, err, DR_ERR_PARAMS, "missing command");
    if (!cmd_in(cmd, kMeterCommands, sizeof(kMeterCommands) / sizeof(kMeterCommands[0])))
        return dr_fail(result, err, DR_ERR_PARAMS, "unknown meter command");
    const char* value = args["value"] | "";
    return dispatch_dr_control(ACTION_TYPE_METER, cmd, value, result, err);
}

// ============================================================================
// Control tool: print_control — star the most recently saved print
// ============================================================================

static const char* const kPrintCommands[] = { "toggle_star", "set_star" };

static bool tool_print_control(const JsonObject& args, JsonObject& result, String& err) {
    const char* cmd = args["command"] | (const char*)nullptr;
    if (!cmd || !cmd[0]) return dr_fail(result, err, DR_ERR_PARAMS, "missing command");
    if (!cmd_in(cmd, kPrintCommands, sizeof(kPrintCommands) / sizeof(kPrintCommands[0])))
        return dr_fail(result, err, DR_ERR_PARAMS, "unknown print command");
    // set_star takes value "1" (star) or "0" (unstar); toggle_star ignores value.
    const char* value = args["value"] | "";
    return dispatch_dr_control(ACTION_TYPE_PRINT, cmd, value, result, err);
}

// ============================================================================
// Control tool: relay_control — enlarger/safelight relay on/off
// ============================================================================
// relay_request() is documented safe from any task (sets a bitmask + signals
// the relay task), so this runs inline without deferring to the main loop.

static bool tool_relay_control(const JsonObject& args, JsonObject& result, String& err) {
    // Accept either {"on": true} or {"state": "on"|"off"}.
    bool on;
    if (args.containsKey("on")) {
        on = args["on"].as<bool>();
    } else if (args.containsKey("state")) {
        const char* st = args["state"] | "";
        if (strcmp(st, "on") == 0)       on = true;
        else if (strcmp(st, "off") == 0) on = false;
        else return dr_fail(result, err, DR_ERR_PARAMS, "state must be 'on' or 'off'");
    } else {
        return dr_fail(result, err, DR_ERR_PARAMS, "missing 'on' (boolean) or 'state' ('on'/'off')");
    }

    relay_request(on);
    LOGI(TAG, "Relay %s via MCP", on ? "ON" : "OFF");
    result["status"]   = "ok";
    result["relay_on"] = on;
    return true;
}

// ============================================================================
// Control tool: delete_print — remove one saved print (plain Storage I/O)
// ============================================================================

static bool tool_delete_print(const JsonObject& args, JsonObject& result, String& err) {
    const char* id = args["id"] | (const char*)nullptr;
    if (!dr_valid_print_id(id)) return dr_fail(result, err, DR_ERR_PARAMS, "missing or invalid print id");

    char path[48];
    snprintf(path, sizeof(path), "%s/%s.json", PRINT_LOG_DIR, id);
    if (!Storage.exists(path)) return dr_fail(result, err, DR_ERR_PARAMS, "print not found");
    if (!Storage.remove(path)) return dr_fail(result, err, DR_ERR_INTERNAL, "delete failed");

    LOGI(TAG, "Deleted print %s via MCP", id);
    result["status"] = "deleted";
    result["id"]     = id;
    return true;
}

// ============================================================================
// Control tool: delete_all_prints — clear the whole print log (destructive)
// ============================================================================

static bool tool_delete_all_prints(const JsonObject& args, JsonObject& result, String& err) {
    bool confirm = args["confirm"] | false;
    if (!confirm) return dr_fail(result, err, DR_ERR_PARAMS, "set confirm=true to delete all prints");

    File dir = Storage.open(PRINT_LOG_DIR);
    if (!dir || !dir.isDirectory()) {
        result["status"]  = "deleted";
        result["removed"] = 0;
        return true;
    }

    // Collect names first — cannot delete while iterating the directory.
    char (*names)[MCP_PRINT_ID_MAX_LEN + 6] =
        (char(*)[MCP_PRINT_ID_MAX_LEN + 6])heap_caps_malloc(
            DARKROOM_PRINT_LOG_MAX * (MCP_PRINT_ID_MAX_LEN + 6), MALLOC_CAP_SPIRAM);
    if (!names) return dr_fail(result, err, DR_ERR_INTERNAL, "out of memory");

    uint16_t count = 0;
    for (File f = dir.openNextFile(); f && count < DARKROOM_PRINT_LOG_MAX; f = dir.openNextFile()) {
        if (f.isDirectory()) continue;
        strlcpy(names[count], f.name(), MCP_PRINT_ID_MAX_LEN + 6);
        count++;
    }

    uint16_t removed = 0;
    for (uint16_t i = 0; i < count; i++) {
        char path[64];
        snprintf(path, sizeof(path), "%s/%s", PRINT_LOG_DIR, names[i]);
        if (Storage.remove(path)) removed++;
    }
    heap_caps_free(names);

    LOGI(TAG, "Cleared all prints via MCP: %u removed", (unsigned)removed);
    result["status"]  = "deleted";
    result["removed"] = removed;
    return true;
}

// ============================================================================
// Authoring tool: set_print — edit a saved print's notes / starred flag
// ============================================================================
// Read-modify-write of one print record (mirrors the portal PUT /api/prints?id
// handler). Runs inline on the web task like the other print file ops; the MCP
// route and the portal handlers share the AsyncWebServer task, so writes are
// serialized with respect to each other.

#ifndef MCP_PRINT_NOTES_MAX_LEN
#define MCP_PRINT_NOTES_MAX_LEN 500
#endif

static bool tool_set_print(const JsonObject& args, JsonObject& result, String& err) {
    const char* id = args["id"] | (const char*)nullptr;
    if (!dr_valid_print_id(id)) return dr_fail(result, err, DR_ERR_PARAMS, "missing or invalid print id");

    bool has_notes   = args.containsKey("notes");
    bool has_starred = args.containsKey("starred");
    if (!has_notes && !has_starred)
        return dr_fail(result, err, DR_ERR_PARAMS, "provide 'notes' and/or 'starred' to update");

    const char* notes = has_notes ? (args["notes"] | "") : "";
    if (has_notes && strlen(notes) > MCP_PRINT_NOTES_MAX_LEN)
        return dr_fail(result, err, DR_ERR_PARAMS, "notes too long");

    char path[48];
    snprintf(path, sizeof(path), "%s/%s.json", PRINT_LOG_DIR, id);
    if (!Storage.exists(path)) return dr_fail(result, err, DR_ERR_PARAMS, "print not found");

    // Read + parse the existing record.
    File f = Storage.open(path, "r");
    if (!f) return dr_fail(result, err, DR_ERR_INTERNAL, "cannot open print file");
    size_t sz = f.size();
    if (sz == 0 || sz > 8192) { f.close(); return dr_fail(result, err, DR_ERR_INTERNAL, "print file too large"); }

    auto doc = make_psram_json_doc(sz + 512);
    if (!doc || doc->capacity() == 0) { f.close(); return dr_fail(result, err, DR_ERR_INTERNAL, "out of memory"); }
    DeserializationError de = deserializeJson(*doc, f);
    f.close();
    if (de) return dr_fail(result, err, DR_ERR_INTERNAL, "print file parse error");

    // Apply updates (empty notes / starred=false remove the key, matching the
    // portal PUT handler so the file stays compact).
    JsonObject root = doc->as<JsonObject>();
    if (has_notes) {
        if (notes[0]) root["notes"] = notes;
        else          root.remove("notes");
    }
    if (has_starred) {
        if (args["starred"] | false) root["starred"] = true;
        else                         root.remove("starred");
    }

    // Write back.
    f = Storage.open(path, "w");
    if (!f) return dr_fail(result, err, DR_ERR_INTERNAL, "cannot open print file for write");
    serializeJson(*doc, f);
    f.close();

    LOGI(TAG, "Updated print %s via MCP", id);
    result["status"]  = "saved";
    result["id"]      = id;
    if (has_notes)   result["notes"]   = root.containsKey("notes") ? notes : "";
    if (has_starred) result["starred"] = root.containsKey("starred");
    return true;
}

// ============================================================================
// Authoring tool: set_relay_config — write/replace the relay action slots
// ============================================================================

static bool tool_set_relay_config(const JsonObject& args, JsonObject& result, String& err) {
    const char* content = args["content"] | (const char*)nullptr;
    if (!content) return dr_fail(result, err, DR_ERR_PARAMS, "missing content");
    size_t len = strlen(content);
    if (len == 0) return dr_fail(result, err, DR_ERR_PARAMS, "empty content");
    if (len > MCP_RELAY_CONFIG_MAX_BYTES) return dr_fail(result, err, DR_ERR_PARAMS, "content too large");

    // Validate that the body is well-formed JSON before persisting — the
    // underlying relay loader silently tolerates malformed input (empties the
    // slots), so catch it here at the boundary.
    {
        auto vdoc = make_psram_json_doc(len + 256);
        if (!vdoc || vdoc->capacity() == 0) return dr_fail(result, err, DR_ERR_INTERNAL, "out of memory");
        DeserializationError de = deserializeJson(*vdoc, content, len);
        if (de) return dr_fail(result, err, DR_ERR_PARAMS, "invalid relay config JSON");
        if (!vdoc->is<JsonObject>()) return dr_fail(result, err, DR_ERR_PARAMS, "relay config must be a JSON object");
    }

    if (!relay_save_config_from_json((const uint8_t*)content, len))
        return dr_fail(result, err, DR_ERR_INTERNAL, "failed to write relay config");

    LOGI(TAG, "Saved relay config via MCP (%u bytes)", (unsigned)len);
    result["status"]        = "saved";
    result["bytes_written"] = (uint32_t)len;
    return true;
}

// ============================================================================
// Tool descriptors + registration
// ============================================================================

// Core use case surfaced to the model at MCP initialize (see mcp_tool_registry.h).
REGISTER_MCP_CLASS_SCENARIO(
    "A darkroom enlarger timer for black-and-white printing: use it to meter prints, run f-stop "
    "test strips, time enlarger exposures, and log finished prints — the *_control tools drive a "
    "real enlarger lamp/relay that exposes photographic paper, so confirm with the user before "
    "starting an exposure or test strip.");

static const McpTool s_tool_get_expose_status = {
    "get_expose_status",
    "Get the darkroom single-exposure timer state: state (stopped/running/paused/focus), exposure time setting (s), dry-down-compensated effective time (s), dry-down percent, countdown remaining/elapsed (s), and the relay on/off state.",
    "{\"type\":\"object\",\"properties\":{}}",
    tool_get_expose_status, true, false, false, false
};
REGISTER_MCP_TOOL(s_tool_get_expose_status);

static const McpTool s_tool_get_strip_status = {
    "get_strip_status",
    "Get the f-stop test strip sequencer state: state (idle/countdown/exposing/pausing), current segment + total count, base time (s), step interval (stops + label e.g. 1/3), initial countdown + inter-segment pause settings (s), exposure-tick flag, current-phase remaining/elapsed (s), estimated total sequence time (s), relay state, and the per-segment table (cumulative/incremental seconds + f-stop offset).",
    "{\"type\":\"object\",\"properties\":{}}",
    tool_get_strip_status, true, false, false, false
};
REGISTER_MCP_TOOL(s_tool_get_strip_status);

static const McpTool s_tool_get_meter_status = {
    "get_meter_status",
    "Get the print-prep light meter state: Lref + Zone V time inputs, bright/dark spot lux readings, computed Subject Brightness Range (SBR), recommended paper grade + label, recommended exposure time (s), and magnification-compensation readings (lux A/B + factor). Sentinels: lref/zone5_time=0 means not set; l_bright/l_dark/time_s/mag_lux_*=-1 means not read/not computable.",
    "{\"type\":\"object\",\"properties\":{}}",
    tool_get_meter_status, true, false, false, false
};
REGISTER_MCP_TOOL(s_tool_get_meter_status);

static const McpTool s_tool_get_relay_config = {
    "get_relay_config",
    "Get the enlarger/safelight relay action configuration: a JSON object with four ButtonAction slots keyed enlarger_on, enlarger_off, safelight_on, safelight_off (each typically type=shelly with host + relay index + on/off).",
    "{\"type\":\"object\",\"properties\":{}}",
    tool_get_relay_config, true, false, false, false
};
REGISTER_MCP_TOOL(s_tool_get_relay_config);

static const McpTool s_tool_list_prints = {
    "list_prints",
    "List saved darkroom prints (newest first, capped): id (YYMMDD-NNN), summary fields, notes, starred flag, and a detail_url (GET /api/prints?id=ID) for the full record. Use get_print for one print's full data.",
    "{\"type\":\"object\",\"properties\":{}}",
    tool_list_prints, true, false, false, false
};
REGISTER_MCP_TOOL(s_tool_list_prints);

static const McpTool s_tool_get_print = {
    "get_print",
    "Get one saved print's full record (exposure fields, metering context, test-strip segments, notes, star) plus a detail_url. Args: id (string, YYMMDD-NNN form from list_prints).",
    "{\"type\":\"object\",\"properties\":{\"id\":{\"type\":\"string\"}},\"required\":[\"id\"]}",
    tool_get_print, true, false, false, false
};
REGISTER_MCP_TOOL(s_tool_get_print);

static const McpTool s_tool_expose_control = {
    "expose_control",
    "Run a single-exposure timer command. command (required): start | stop | toggle | pause | resume | reset | focus | focus_off | focus_toggle | set_time | adjust_seconds | adjust_stops | set_dry_down | adjust_dry_down. value rules: REQUIRED for set_time (seconds), adjust_seconds (+/- seconds), adjust_stops (+/- f-stops, multiply by 2^N), set_dry_down (percent 0-15), adjust_dry_down (+/- percentage points); ignored otherwise. dry-down changes are rejected while running/paused. "
    "IMPORTANT WORKFLOW: start begins a REAL exposure — it switches the enlarger lamp ON and counts down, exposing photographic paper on the easel. Before issuing start, set the exposure time (set_time) if needed, then confirm with the user that the paper is placed on the easel and they are ready; do NOT start an exposure unprompted. focus switches the enlarger lamp ON continuously with no countdown (for composing/focusing) — warn the user it will fog/expose any paper left on the easel, and use focus_off to turn it back off. pause/resume/stop apply to an exposure already under way. Report the timer state from get_expose_status when the user needs to see progress.",
    "{\"type\":\"object\",\"properties\":{\"command\":{\"type\":\"string\",\"enum\":[\"start\",\"stop\",\"toggle\",\"pause\",\"resume\",\"reset\",\"focus\",\"focus_off\",\"focus_toggle\",\"set_time\",\"adjust_seconds\",\"adjust_stops\",\"set_dry_down\",\"adjust_dry_down\"]},\"value\":{\"type\":\"string\"}},\"required\":[\"command\"]}",
    tool_expose_control, false, false, true, false
};
REGISTER_MCP_TOOL(s_tool_expose_control);

static const McpTool s_tool_strip_control = {
    "strip_control",
    "Run an f-stop test strip command. command (required): start | cancel | set_base | adjust_base | step_up | step_down | adjust_segments | set_segments | set_countdown | adjust_countdown | set_pause | adjust_pause | set_tick. value rules: REQUIRED for set_base/adjust_base (seconds), set_segments/adjust_segments (count, clamped 3-11 odd), set_countdown/adjust_countdown (2-10 s), set_pause/adjust_pause (3-15 s), set_tick ('on'/'off'); ignored otherwise. Configuration commands are rejected while a sequence is active. "
    "IMPORTANT WORKFLOW: start runs a REAL automated multi-segment exposure that the user physically participates in. They place paper on the easel with a mask/card covering all but the first strip, then during each between-segment pause (the device beeps) they slide the mask to uncover the next strip; the device handles all timing and advances segments ON ITS OWN, so you issue start exactly ONCE — do NOT call a command per segment. Before start: (1) set the parameters (set_base, step_up/step_down, set_segments, set_countdown, set_pause) — these only take effect while idle; (2) optionally confirm the plan with the user via get_strip_status (shows the per-segment time table); (3) confirm the paper and mask are positioned and the user understands the uncover-per-pause routine. Use cancel to abort a running sequence.",
    "{\"type\":\"object\",\"properties\":{\"command\":{\"type\":\"string\",\"enum\":[\"start\",\"cancel\",\"set_base\",\"adjust_base\",\"step_up\",\"step_down\",\"adjust_segments\",\"set_segments\",\"set_countdown\",\"adjust_countdown\",\"set_pause\",\"adjust_pause\",\"set_tick\"]},\"value\":{\"type\":\"string\"}},\"required\":[\"command\"]}",
    tool_strip_control, false, false, true, false
};
REGISTER_MCP_TOOL(s_tool_strip_control);

static const McpTool s_tool_meter_control = {
    "meter_control",
    "Run a print-prep meter command. command (required): read_lref | read_bright | read_dark | set_lref | adjust_lref | set_zone5 | adjust_zone5 | mag_measure_a | mag_measure_b | mag_clear. value rules: REQUIRED for set_lref/adjust_lref and set_zone5/adjust_zone5; ignored otherwise. "
    "IMPORTANT WORKFLOW: read_lref, read_bright, read_dark, mag_measure_a and mag_measure_b each take a live reading from the ONE physical TSL2591 sensor probe, which the user must move to a different location for each reading. Do these ONE AT A TIME, never in a batch: before every read_*/mag_measure_* command, tell the user exactly where to place the probe and WAIT for them to confirm it is in position before you issue the command. Placement: read_lref = bare bulb / incident light at the easel; read_bright = the brightest highlight of the projected image (deepest negative density); read_dark = the deepest shadow (thinnest negative density); mag_measure_a / mag_measure_b = the two magnifications being compared. After each reading, report the returned lux value, then ask the user to reposition for the next one. set_lref/set_zone5/adjust_* are manual entries (no sensor) and need no repositioning. A full grade+time result needs read_bright, read_dark, read_lref and a Zone V time (set_zone5).",
    "{\"type\":\"object\",\"properties\":{\"command\":{\"type\":\"string\",\"enum\":[\"read_lref\",\"read_bright\",\"read_dark\",\"set_lref\",\"adjust_lref\",\"set_zone5\",\"adjust_zone5\",\"mag_measure_a\",\"mag_measure_b\",\"mag_clear\"]},\"value\":{\"type\":\"string\"}},\"required\":[\"command\"]}",
    tool_meter_control, false, false, true, false
};
REGISTER_MCP_TOOL(s_tool_meter_control);

static const McpTool s_tool_print_control = {
    "print_control",
    "Star or unstar the most recently saved print. command (required): toggle_star (flip) | set_star (value '1' to star, '0' to unstar). No-op if no print has been saved yet. To star an arbitrary print, edit it via the portal.",
    "{\"type\":\"object\",\"properties\":{\"command\":{\"type\":\"string\",\"enum\":[\"toggle_star\",\"set_star\"]},\"value\":{\"type\":\"string\"}},\"required\":[\"command\"]}",
    tool_print_control, false, false, true, false
};
REGISTER_MCP_TOOL(s_tool_print_control);

static const McpTool s_tool_relay_control = {
    "relay_control",
    "Switch the enlarger/safelight relay. Provide on (boolean) or state ('on'/'off'). on=true fires the configured enlarger-ON + safelight-OFF actions; on=false fires the inverse. CAUTION: on=true turns the enlarger lamp ON, which floods the easel with light and will fog/expose any photographic paper sitting on it — confirm with the user before switching the enlarger on outside of a metered exposure. For timed exposures use expose_control (start); for test strips use strip_control (start) rather than driving the relay directly.",
    "{\"type\":\"object\",\"properties\":{\"on\":{\"type\":\"boolean\"},\"state\":{\"type\":\"string\",\"enum\":[\"on\",\"off\"]}}}",
    tool_relay_control, false, false, true, false
};
REGISTER_MCP_TOOL(s_tool_relay_control);

static const McpTool s_tool_delete_print = {
    "delete_print",
    "Delete one saved print by id (destructive). Args: id (string, YYMMDD-NNN form).",
    "{\"type\":\"object\",\"properties\":{\"id\":{\"type\":\"string\"}},\"required\":[\"id\"]}",
    tool_delete_print, false, true, true, false
};
REGISTER_MCP_TOOL(s_tool_delete_print);

static const McpTool s_tool_delete_all_prints = {
    "delete_all_prints",
    "Delete ALL saved prints (destructive, irreversible). Requires confirm=true.",
    "{\"type\":\"object\",\"properties\":{\"confirm\":{\"type\":\"boolean\"}},\"required\":[\"confirm\"]}",
    tool_delete_all_prints, false, true, true, false
};
REGISTER_MCP_TOOL(s_tool_delete_all_prints);

static const McpTool s_tool_set_print = {
    "set_print",
    "Edit a saved print's notes and/or star. Args: id (string, YYMMDD-NNN form) plus at least one of notes (string; empty clears it) or starred (boolean). Unlike print_control (which only stars the most recent print), this edits any print by id.",
    "{\"type\":\"object\",\"properties\":{\"id\":{\"type\":\"string\"},\"notes\":{\"type\":\"string\"},\"starred\":{\"type\":\"boolean\"}},\"required\":[\"id\"]}",
    tool_set_print, false, false, false, true
};
REGISTER_MCP_TOOL(s_tool_set_print);

static const McpTool s_tool_set_relay_config = {
    "set_relay_config",
    "Create or replace the enlarger/safelight relay action configuration. Use this tool directly; do not POST to any HTTP endpoint. content is the relay-config JSON (the same shape get_relay_config returns): a JSON object with four ButtonAction slots keyed enlarger_on, enlarger_off, safelight_on, safelight_off. For Shelly relays each slot is {\"type\":\"shelly\",\"host\":\"192.168.1.x\",\"relay\":0,\"on\":true}. Call get_relay_config afterward to confirm.",
    "{\"type\":\"object\",\"properties\":{\"content\":{\"type\":\"string\",\"description\":\"Full relay-config JSON object (keys enlarger_on/enlarger_off/safelight_on/safelight_off) in the shape get_relay_config returns.\"}},\"required\":[\"content\"]}",
    tool_set_relay_config, false, false, false, true
};
REGISTER_MCP_TOOL(s_tool_set_relay_config);

#endif // HAS_MCP && IS_DARKROOM_TIMER
