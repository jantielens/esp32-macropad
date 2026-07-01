// ============================================================================
// mcp_tools_shutter.cpp — Shutter Tester device-class MCP tools.
//
// Aggregated into the build via src/app/mcp_components.cpp (arduino-cli only
// compiles .cpp files in the sketch root). Doubly-gated HAS_MCP &&
// IS_SHUTTER_TESTER so non-shutter firmware never compiles a byte of this.
//
// Ownership: every shutter MCP tool lives here, inside the shutter device-class
// folder. The only hook outside this folder is the one-line #include in
// mcp_components.cpp under the IS_SHUTTER_TESTER gate.
//
// Threading contract (see mcp_tool_registry.h):
//   - READ tools run on the AsyncWebServer (web) task. The shutter snapshot
//     getters (shutter_measure_get_latest/_history, shutter_capture_get_*) are
//     mutex/portMUX protected and safe to call from any task, so reads run
//     inline. NOTE: shutter_capture_get_latest() returns a frame whose
//     waveforms[].samples point into the capture task's double-buffered PSRAM
//     (not a copy). Those buffers are never freed while parked (only the
//     `valid` flag is cleared, so get_latest returns false once the engine
//     parks — no use-after-free), but a concurrent capture can overwrite them
//     mid-read. get_shutter_waveform re-checks capture_id to flag a torn
//     snapshot rather than locking across the bulk copy.
//   - CONTROL tools build a ButtonAction and defer to the main loop via
//     mcp_control_dispatch() (exactly like system_command) — session/guide
//     start-stop dispatch user actions through action_dispatch(), which touches
//     LVGL and must not run on the web task.
//   - delete_shutter_session and set_shutter_tests are plain Storage/manifest
//     I/O (no LVGL, no action dispatch) and run inline on the web task, the same
//     way the existing portal DELETE/PUT handlers do.
// ============================================================================

#include "board_config.h"

#if HAS_MCP && IS_SHUTTER_TESTER

#include "mcp_tool_registry.h"
#include "web_mcp.h"

#include "../shutter_capture.h"
#include "../shutter_measure.h"
#include "../shutter_session.h"
#include "../shutter_test_scripts.h"
#include "../shutter_payload.h"   // ShutterPayload, ACTION_TYPE_SHUTTER

#include "action_dispatch.h"
#include "fs_indexed_store.h"
#include "storage.h"
#include "psram_json_allocator.h"
#include "web_portal_json.h"   // make_psram_json_doc
#include "log_manager.h"

#include <ArduinoJson.h>
#include <esp_heap_caps.h>
#include <math.h>
#include <memory>
#include <string.h>

#define TAG "ShutterMCP"

using PsramDoc = std::shared_ptr<BasicJsonDocument<PsramJsonAllocator>>;

// JSON-RPC error codes (canonical values in mcp_tool_registry.h).
static constexpr int SH_ERR_PARAMS   = MCP_RPC_ERR_PARAMS;
static constexpr int SH_ERR_INTERNAL = MCP_RPC_ERR_INTERNAL;
static constexpr int SH_ERR_BUSY     = MCP_RPC_ERR_CONTROL_BUSY;

static constexpr uint32_t SH_CONTROL_TIMEOUT_MS = 3000;

// Defined in list_provider_shutter_tests.cpp (same device class) — refreshes the
// cached [list:shutter_tests] provider after the test file changes.
void list_provider_shutter_tests_refresh();

// ----------------------------------------------------------------------------
// Tunable knobs — waveform tool payload size vs. fidelity.
// Min-per-bucket decimation preserves the downward exposure dip (the signal of
// interest), so the LLM still sees pulse depth/position after downsampling.
// ----------------------------------------------------------------------------
#ifndef MCP_SHUTTER_WF_MAX_POINTS
#define MCP_SHUTTER_WF_MAX_POINTS  96   // emitted points per sensor after decimation
#endif
#ifndef MCP_SHUTTER_WF_MAX_SENSORS
#define MCP_SHUTTER_WF_MAX_SENSORS SHUTTER_SENSOR_MAX
#endif

// Safety limit for the guided-test script file written by set_shutter_tests
// (mirrors the 8 KB cap in the portal PUT /api/shutter/tests handler).
#ifndef MCP_SHUTTER_TESTS_MAX_BYTES
#define MCP_SHUTTER_TESTS_MAX_BYTES 8192
#endif

static bool sh_fail(JsonObject& result, String& err, int code, const char* msg) {
    err = msg ? msg : "error";
    result[MCP_RESULT_ERRCODE_KEY] = code;
    return false;
}

static const char* sh_verdict_str(uint8_t v) {
    switch (v) {
        case SHUTTER_VERDICT_PASS:    return "pass";
        case SHUTTER_VERDICT_WARNING: return "warning";
        case SHUTTER_VERDICT_FAIL:    return "fail";
        default:                      return "unknown";
    }
}

// Emit a single measurement's summary fields into `o`.
static void sh_emit_measurement_summary(JsonObject o, const ShutterMeasurement& m) {
    o["timestamp_ms"]       = m.timestamp_ms;
    o["target_speed"]       = m.nearest_speed;
    o["target_ms"]          = m.nearest_duration_ms;
    o["avg_duration_ms"]    = m.avg_duration_ms;
    o["deviation_pct"]      = m.deviation_pct;
    o["deviation_stops"]    = m.deviation_stops;
    o["verdict"]            = sh_verdict_str(m.verdict);
    o["sensor_count"]       = m.sensor_count;
    o["valid_sensor_count"] = m.valid_sensor_count;
    if (m.valid_sensor_count >= 2) {
        o["spread_pct"] = m.spread_pct;
        o["spread_ms"]  = m.spread_ms;
    }
    if (m.capping_gradient_stops_per_mm >= 0.0f) {
        o["capping_gradient_stops_per_mm"] = m.capping_gradient_stops_per_mm;
    }
    // `m` is a heap struct the caller frees before serialization; wrap in
    // String() so ArduinoJson copies the bytes instead of linking into it.
    if (m.detected_travel[0]) o["detected_travel"] = String(m.detected_travel);
    o["speed_locked"] = m.speed_locked;
}

// Allocate + parse the guided-test script file into a PSRAM-backed result.
// Returns nullptr on OOM; the caller owns the result and must heap_caps_free()
// it. out_count (optional) receives the number of parsed scripts.
static ShutterTestParseResult* sh_tests_alloc_parse(uint8_t* out_count) {
    ShutterTestParseResult* pr = (ShutterTestParseResult*)heap_caps_malloc(
        sizeof(ShutterTestParseResult), MALLOC_CAP_SPIRAM);
    if (!pr) { if (out_count) *out_count = 0; return nullptr; }
    uint8_t n = shutter_test_scripts_parse(pr);
    if (out_count) *out_count = n;
    return pr;
}

// Parse the saved-session manifest into a PSRAM doc and expose its "entries"
// array. Returns the doc (keep it alive while iterating entries) or nullptr when
// the manifest is empty/unreadable; out_entries is set to a null array then.
static PsramDoc sh_load_session_manifest(JsonArray* out_entries) {
    if (out_entries) *out_entries = JsonArray();
    FsIndexedStore& store = shutter_session_get_store();
    String manifest = store.list();
    if (manifest.isEmpty()) return nullptr;
    auto doc = make_psram_json_doc(manifest.length() + 1024);
    if (!doc || doc->capacity() == 0) return nullptr;
    if (deserializeJson(*doc, manifest) != DeserializationError::Ok) return nullptr;
    if (out_entries) *out_entries = (*doc)["entries"].as<JsonArray>();
    return doc;
}

// ============================================================================
// Read tool: get_shutter_status — caps, target, session, alignment, latest shot
// ============================================================================

static bool tool_get_shutter_status(const JsonObject& args, JsonObject& result, String& err) {
    (void)args; (void)err;

    ShutterCaptureCaps caps = {};
    shutter_capture_get_caps(&caps);

    result["available"]      = shutter_capture_is_available();
    result["running"]        = shutter_capture_is_running();
    result["calibrating"]    = shutter_capture_is_calibrating();
    result["preset_id"]      = caps.preset_id_str ? caps.preset_id_str : "";
    result["preset_name"]    = caps.preset_name ? caps.preset_name : "";
    result["backend"]        = caps.backend_name ? caps.backend_name : "";
    result["sensor_count"]   = caps.sensor_count;
    result["sample_rate_hz"] = caps.sample_rate_hz_per_sensor;
    result["measurement_count"] = shutter_measure_get_count();

    // Comparison target + lock.
    {
        char label[20] = {};
        bool locked = false;
        shutter_measure_get_target(label, sizeof(label), &locked);
        JsonObject tgt = result.createNestedObject("target");
        // `label` is a stack buffer; assign the array (not a const char*) so
        // ArduinoJson COPIES it rather than linking a soon-dangling pointer.
        tgt["speed"]  = label;
        tgt["locked"] = locked;
    }

    // Session state.
    {
        JsonObject sess = result.createNestedObject("session");
        bool active = shutter_session_is_active();
        sess["active"] = active;
        char type[16] = {};
        shutter_session_get_type(type, sizeof(type));
        sess["type"] = type;
        sess["count"] = shutter_session_get_count();
        uint32_t id = shutter_session_get_id();
        if (id) { char sid[24]; snprintf(sid, sizeof(sid), "sess_%lu", (unsigned long)id); sess["id"] = sid; }
        else    { sess["id"] = ""; }
        if (active && shutter_session_is_guided()) {
            JsonObject g = sess.createNestedObject("guide");
            char buf[32];
            shutter_session_guide_get_name(buf, sizeof(buf));   g["name"]   = buf;
            shutter_session_guide_get_target(buf, sizeof(buf)); g["target"] = buf;
            shutter_session_guide_get_step(buf, sizeof(buf));   g["step"]   = buf;
            shutter_session_guide_get_steps(buf, sizeof(buf));  g["steps"]  = buf;
            shutter_session_guide_get_shot(buf, sizeof(buf));   g["shot"]   = buf;
            shutter_session_guide_get_shots(buf, sizeof(buf));  g["shots"]  = buf;
        }
    }

    // Alignment readout (only populated while alignment mode is active).
    {
        ShutterAlignmentReading a = {};
        JsonObject align = result.createNestedObject("alignment");
        bool active = shutter_capture_is_alignment_active();
        align["active"] = active;
        if (active && shutter_capture_get_alignment(&a) && a.valid) {
            align["status"]     = a.status ? a.status : "";
            align["hint"]       = a.hint ? a.hint : "";
            align["spread_pct"] = a.spread_pct;
            JsonArray pct = align.createNestedArray("pct");
            for (uint8_t i = 0; i < a.sensor_count && i < SHUTTER_SENSOR_MAX; i++) pct.add(a.pct[i]);
        }
    }

    // Latest measurement (per-sensor health). Heap-allocated in PSRAM to keep
    // this ~450-byte struct off the async web-task stack. Null when no live
    // capture.
    ShutterMeasurement* m = (ShutterMeasurement*)heap_caps_malloc(
        sizeof(ShutterMeasurement), MALLOC_CAP_SPIRAM);
    if (m && shutter_capture_is_running() && shutter_measure_get_latest(m) && m->valid) {
        JsonObject mo = result.createNestedObject("measurement");
        sh_emit_measurement_summary(mo, *m);
        JsonArray sensors = mo.createNestedArray("sensors");
        for (uint8_t i = 0; i < m->sensor_count && i < SHUTTER_SENSOR_MAX; i++) {
            const ShutterSensorResult& s = m->sensors[i];
            JsonObject so = sensors.createNestedObject();
            so["index"] = i;
            so["valid"] = s.valid;
            if (s.valid) {
                so["duration_ms"]  = s.duration_ms;
                so["min_adc"]      = s.min_adc;
                so["baseline_adc"] = s.baseline_adc;
                so["threshold"]    = s.threshold;
                so["depth"]        = (int)s.baseline_adc - (int)s.min_adc;
            }
        }
    } else {
        result["measurement"] = (const char*)nullptr;
    }
    if (m) heap_caps_free(m);
    return true;
}

// ============================================================================
// Read tool: get_shutter_history — rolling history of recent measurements
// ============================================================================

static bool tool_get_shutter_history(const JsonObject& args, JsonObject& result, String& err) {
    (void)args;
    ShutterMeasurement* hist = (ShutterMeasurement*)heap_caps_malloc(
        SHUTTER_HISTORY_SIZE * sizeof(ShutterMeasurement), MALLOC_CAP_SPIRAM);
    if (!hist) return sh_fail(result, err, SH_ERR_INTERNAL, "out of memory");

    uint8_t n = shutter_measure_get_history(hist, SHUTTER_HISTORY_SIZE);
    result["count"] = n;
    JsonArray arr = result.createNestedArray("measurements");
    for (uint8_t i = 0; i < n; i++) {
        JsonObject o = arr.createNestedObject();
        sh_emit_measurement_summary(o, hist[i]);
    }
    heap_caps_free(hist);
    return true;
}

// ============================================================================
// Read tool: get_shutter_waveform — decimated per-sensor capture waveform
// ============================================================================

static bool tool_get_shutter_waveform(const JsonObject& args, JsonObject& result, String& err) {
    (void)args;
    ShutterCaptureFrame* frame = (ShutterCaptureFrame*)heap_caps_malloc(
        sizeof(ShutterCaptureFrame), MALLOC_CAP_SPIRAM);
    if (!frame) return sh_fail(result, err, SH_ERR_INTERNAL, "out of memory");

    if (!shutter_capture_get_latest(frame) || !frame->valid) {
        heap_caps_free(frame);
        result["available"] = false;
        return true;
    }

    const uint32_t capture_id0 = frame->capture_id;
    result["available"]         = true;
    result["capture_id"]        = capture_id0;
    result["timestamp_ms"]      = frame->timestamp_ms;
    result["sensor_count"]      = frame->sensor_count;
    result["max_points"]        = (uint32_t)MCP_SHUTTER_WF_MAX_POINTS;

    JsonArray sensors = result.createNestedArray("sensors");
    uint8_t emit_sensors = frame->sensor_count;
    if (emit_sensors > MCP_SHUTTER_WF_MAX_SENSORS) emit_sensors = MCP_SHUTTER_WF_MAX_SENSORS;

    for (uint8_t si = 0; si < emit_sensors; si++) {
        const ShutterWaveformView& v = frame->waveforms[si];
        JsonObject so = sensors.createNestedObject();
        so["index"]          = si;
        so["raw_count"]      = v.count;
        so["trigger_index"]  = v.trigger_index;
        so["trigger_frac"]   = v.count ? ((float)v.trigger_index / (float)v.count) : 0.0f;
        so["sample_rate_hz"] = v.sample_rate_hz;
        so["threshold"]      = frame->thresholds[si];

        if (!v.samples || v.count == 0) { so.createNestedArray("samples"); continue; }

        // Min-per-bucket decimation: pull the lowest ADC value (deepest light
        // exposure) from each bucket so the pulse stays visible after shrinking.
        uint32_t bucket = (v.count + MCP_SHUTTER_WF_MAX_POINTS - 1) / MCP_SHUTTER_WF_MAX_POINTS;
        if (bucket < 1) bucket = 1;
        so["decimation"] = bucket;
        JsonArray samples = so.createNestedArray("samples");
        for (uint32_t i = 0; i < v.count; i += bucket) {
            uint16_t mn = 0xFFFF;
            uint32_t end = i + bucket;
            if (end > v.count) end = v.count;
            for (uint32_t j = i; j < end; j++) if (v.samples[j] < mn) mn = v.samples[j];
            samples.add(mn);
        }
    }

    // Torn-read detection: waveforms[].samples point into the capture task's
    // double-buffered PSRAM (not a copy). If a new capture committed while we
    // were copying, the snapshot may mix two captures — flag it so the caller
    // can re-read. No crash risk: the buffers are never freed while parked.
    if (shutter_capture_get_latest(frame) && frame->capture_id != capture_id0) {
        result["superseded"] = true;
    }
    heap_caps_free(frame);
    return true;
}

// ============================================================================
// Read tool: list_shutter_sessions — saved session manifest
// ============================================================================

static bool tool_list_shutter_sessions(const JsonObject& args, JsonObject& result, String& err) {
    (void)args; (void)err;
    JsonArray entries;
    PsramDoc doc = sh_load_session_manifest(&entries);  // keeps entries alive
    JsonArray out = result.createNestedArray("sessions");
    for (JsonObject e : entries) out.add(e);
    return true;
}

// ============================================================================
// Read tool: get_shutter_session — one session's metadata from the manifest
// ============================================================================
// Returns the cached manifest entry only (RAM, no heavy I/O). The full per-shot
// record (incl. per-sensor waveform arrays) lives in a potentially large file on
// the storage backend; parsing it here would read the file one byte at a time on
// the AsyncTCP web task and trip the task watchdog. Callers fetch the full record
// by streaming the REST endpoint (`detail_url`) instead, the same way the portal
// session viewer does.

static bool tool_get_shutter_session(const JsonObject& args, JsonObject& result, String& err) {
    const char* id = args["id"] | (const char*)nullptr;
    if (!id || !id[0]) return sh_fail(result, err, SH_ERR_PARAMS, "missing session id");

    FsIndexedStore& store = shutter_session_get_store();
    if (!store.exists(id)) return sh_fail(result, err, SH_ERR_PARAMS, "session not found");

    JsonObject session = result.createNestedObject("session");
    session["id"] = id;

    // Session-level summary from the cached manifest entry (no data-file read).
    JsonArray entries;
    PsramDoc mdoc = sh_load_session_manifest(&entries);  // keeps entries alive
    for (JsonObject e : entries) {
        if (strcmp(e["id"] | "", id) == 0) {
            for (JsonPair kv : e) {
                if (strcmp(kv.key().c_str(), "id") == 0) continue;
                session[kv.key()] = kv.value();
            }
            break;
        }
    }

    // Full per-shot results + waveforms are served by streaming this REST route.
    session["detail_url"] = String("/api/sessions/") + id;
    return true;
}

// ============================================================================
// Read tool: list_shutter_tests — guided-test script summaries
// ============================================================================

static bool tool_list_shutter_tests(const JsonObject& args, JsonObject& result, String& err) {
    (void)args;
    uint8_t count = 0;
    ShutterTestParseResult* pr = sh_tests_alloc_parse(&count);
    if (!pr) return sh_fail(result, err, SH_ERR_INTERNAL, "out of memory");

    result["count"] = count;
    JsonArray arr = result.createNestedArray("tests");
    for (uint8_t i = 0; i < count; i++) {
        JsonObject o = arr.createNestedObject();
        o["id"]              = pr->scripts[i].id;
        o["name"]            = pr->scripts[i].name;
        o["speed_count"]     = pr->scripts[i].speed_count;
        o["shots_per_speed"] = pr->scripts[i].shots_per_speed;
    }
    heap_caps_free(pr);
    return true;
}

// ============================================================================
// Read tool: get_shutter_test — one guided-test script with its speed list
// ============================================================================

static bool tool_get_shutter_test(const JsonObject& args, JsonObject& result, String& err) {
    const char* id = args["id"] | (const char*)nullptr;
    if (!id || !id[0]) return sh_fail(result, err, SH_ERR_PARAMS, "missing test id");

    ShutterTestParseResult* pr = sh_tests_alloc_parse(nullptr);
    if (!pr) return sh_fail(result, err, SH_ERR_INTERNAL, "out of memory");

    const ShutterTestScript* s = shutter_test_scripts_find(pr, id);
    if (!s) { heap_caps_free(pr); return sh_fail(result, err, SH_ERR_PARAMS, "test not found"); }

    result["id"]              = s->id;
    result["name"]            = s->name;
    result["shots_per_speed"] = s->shots_per_speed;
    result["speed_count"]     = s->speed_count;
    JsonArray speeds = result.createNestedArray("speeds");
    for (uint16_t i = 0; i < s->speed_count && i < SHUTTER_TEST_MAX_SPEEDS; i++) {
        speeds.add(s->speeds[i].speed_suffixed);
    }
    heap_caps_free(pr);
    return true;
}

// ============================================================================
// Control tool: shutter_control — deferred ButtonAction dispatch
// ============================================================================
// One consolidated tool mapping 1:1 onto the shutter ButtonAction command set
// (see shutter_actions.cpp dispatch ladder). The exec builds a ButtonAction and
// runs action_dispatch() on the main loop so binding/LVGL/session work is safe.

struct ShutterCtrlCtx {
    char command[CONFIG_TIMER_CMD_MAX_LEN];
    char value[CONFIG_BINDABLE_SHORT_LEN];
};

static const char* const kShutterCommands[] = {
    "set", "adjust", "toggle_lock",
    "sess_start", "sess_stop", "sess_toggle", "sess_discard",
    "guide_start", "guide_stop", "guide_skip", "guide_redo",
    "align_start", "align_stop", "recalibrate",
};

static bool shutter_command_known(const char* cmd) {
    for (const char* c : kShutterCommands) if (strcmp(c, cmd) == 0) return true;
    return false;
}

static void exec_shutter_control(const void* ctx, bool* ok, char* msg, size_t msg_len) {
    const ShutterCtrlCtx* c = (const ShutterCtrlCtx*)ctx;
    ButtonAction act;
    memset(&act, 0, sizeof(act));
    strlcpy(act.type, ACTION_TYPE_SHUTTER, sizeof(act.type));
    ShutterPayload& p = shutter_payload(act);
    strlcpy(p.command, c->command, sizeof(p.command));
    strlcpy(p.value, c->value, sizeof(p.value));
    action_dispatch(act, "MCP");
    *ok = true;
    snprintf(msg, msg_len, "dispatched %s", c->command);
}

static bool finish_control(McpControlResult r, bool ok, const char* msg,
                           JsonObject& result, String& err) {
    if (r == MCP_CONTROL_BUSY)    return sh_fail(result, err, SH_ERR_BUSY, "another control action is in progress");
    if (r == MCP_CONTROL_TIMEOUT) return sh_fail(result, err, SH_ERR_INTERNAL, "control action timed out");
    if (!ok)                      return sh_fail(result, err, SH_ERR_INTERNAL, msg && msg[0] ? msg : "control action failed");
    // Wrap in String() so ArduinoJson COPIES the text into the result document.
    // `msg` points at the caller's stack buffer; assigning it as a const char*
    // would only link the pointer, which dangles once the handler returns and
    // before the dispatcher serializes the result (garbage output).
    if (msg && msg[0]) result["status"] = String(msg);
    else               result["status"] = "ok";
    return true;
}

static bool tool_shutter_control(const JsonObject& args, JsonObject& result, String& err) {
    const char* cmd = args["command"] | (const char*)nullptr;
    if (!cmd || !cmd[0]) return sh_fail(result, err, SH_ERR_PARAMS, "missing command");
    if (!shutter_command_known(cmd)) return sh_fail(result, err, SH_ERR_PARAMS, "unknown shutter command");

    const char* value = args["value"] | "";

    // Per-command value validation. action_dispatch() runs on the main loop and
    // cannot report command-level failure back, so a bad speed/direction/test-id
    // would otherwise return a misleading "dispatched" with nothing happening.
    // Validate the checkable cases up front (all read-only and thread-safe).
    if (strcmp(cmd, "set") == 0) {
        if (!value[0]) return sh_fail(result, err, SH_ERR_PARAMS, "set requires value (a shutter speed, e.g. '1/125' or '1')");
        if (!shutter_measure_is_valid_speed(value)) return sh_fail(result, err, SH_ERR_PARAMS, "unknown shutter speed; use a standard speed like '1/125' or '1'");
    } else if (strcmp(cmd, "adjust") == 0) {
        if (strcmp(value, "faster") != 0 && strcmp(value, "slower") != 0)
            return sh_fail(result, err, SH_ERR_PARAMS, "adjust requires value 'faster' or 'slower'");
    } else if (strcmp(cmd, "guide_start") == 0) {
        if (!value[0]) return sh_fail(result, err, SH_ERR_PARAMS, "guide_start requires value (a test id from list_shutter_tests)");
        ShutterTestParseResult* pr = sh_tests_alloc_parse(nullptr);
        if (pr) {
            bool found = shutter_test_scripts_find(pr, value) != nullptr;
            heap_caps_free(pr);
            if (!found) return sh_fail(result, err, SH_ERR_PARAMS, "test id not found; see list_shutter_tests");
        }
    }

    ShutterCtrlCtx ctx;
    memset(&ctx, 0, sizeof(ctx));
    strlcpy(ctx.command, cmd, sizeof(ctx.command));
    strlcpy(ctx.value, value, sizeof(ctx.value));

    bool ok = false;
    char msg[96];
    msg[0] = '\0';
    McpControlResult r = mcp_control_dispatch(exec_shutter_control, &ctx, sizeof(ctx),
                                              SH_CONTROL_TIMEOUT_MS, &ok, msg, sizeof(msg));
    return finish_control(r, ok, msg, result, err);
}

// ============================================================================
// Control tool: delete_shutter_session — remove a saved session
// (plain manifest/file I/O — runs inline like the portal DELETE handler)
// ============================================================================

static bool tool_delete_shutter_session(const JsonObject& args, JsonObject& result, String& err) {
    const char* id = args["id"] | (const char*)nullptr;
    if (!id || !id[0]) return sh_fail(result, err, SH_ERR_PARAMS, "missing session id");

    FsIndexedStore& store = shutter_session_get_store();
    if (!store.exists(id)) return sh_fail(result, err, SH_ERR_PARAMS, "session not found");
    if (!store.remove(id)) return sh_fail(result, err, SH_ERR_INTERNAL, "delete failed");

    result["status"]  = "deleted";
    result["id"]      = id;
    return true;
}

// ============================================================================
// Authoring tool: set_shutter_tests — overwrite the guided-test script file
// (plain Storage write — runs inline like the portal PUT handler)
// ============================================================================

static bool tool_set_shutter_tests(const JsonObject& args, JsonObject& result, String& err) {
    const char* content = args["content"] | (const char*)nullptr;
    if (!content) return sh_fail(result, err, SH_ERR_PARAMS, "missing content");
    size_t len = strlen(content);
    if (len > MCP_SHUTTER_TESTS_MAX_BYTES) return sh_fail(result, err, SH_ERR_PARAMS, "content too large");

    File f = Storage.open(SHUTTER_TEST_FILE_PATH, "w");
    if (!f) return sh_fail(result, err, SH_ERR_INTERNAL, "cannot open test file");
    size_t written = (len > 0) ? f.write((const uint8_t*)content, len) : 0;
    f.close();
    if (written != len) return sh_fail(result, err, SH_ERR_INTERNAL, "write failed");

    list_provider_shutter_tests_refresh();
    LOGI(TAG, "Guided test definitions saved via MCP (%u bytes)", (unsigned)written);
    result["status"]        = "saved";
    result["bytes_written"] = (uint32_t)written;
    return true;
}

// ============================================================================
// Tool descriptors + registration
// ============================================================================

// Core use case surfaced to the model at MCP initialize (see mcp_tool_registry.h).
REGISTER_MCP_CLASS_SCENARIO(
    "A camera shutter-speed tester: use it to measure a film camera's shutter speeds from a "
    "light-pulse sensor array, run guided multi-speed tests, and save/review test sessions — the "
    "user physically fires the shutter over the sensors for each measurement.");

static const McpTool s_tool_get_shutter_status = {
    "get_shutter_status",
    "Get shutter-tester live state: active preset/sensors/sample rate, comparison target speed + lock, session state (incl. guided progress), alignment readout, and the latest measurement with per-sensor health.",
    "{\"type\":\"object\",\"properties\":{}}",
    tool_get_shutter_status, true, false, false, false
};
REGISTER_MCP_TOOL(s_tool_get_shutter_status);

static const McpTool s_tool_get_shutter_history = {
    "get_shutter_history",
    "Get the rolling history of recent shutter measurements (speed, deviation, verdict, spread).",
    "{\"type\":\"object\",\"properties\":{}}",
    tool_get_shutter_history, true, false, false, false
};
REGISTER_MCP_TOOL(s_tool_get_shutter_history);

static const McpTool s_tool_get_shutter_waveform = {
    "get_shutter_waveform",
    "Get the latest capture's per-sensor ADC waveform, decimated (min-per-bucket) to keep the pulse visible while bounding payload size. Includes trigger index/fraction and threshold per sensor.",
    "{\"type\":\"object\",\"properties\":{}}",
    tool_get_shutter_waveform, true, false, false, false
};
REGISTER_MCP_TOOL(s_tool_get_shutter_waveform);

static const McpTool s_tool_list_shutter_sessions = {
    "list_shutter_sessions",
    "List saved shutter test sessions (manifest: id, camera, notes, count, worst verdict, type, timestamps).",
    "{\"type\":\"object\",\"properties\":{}}",
    tool_list_shutter_sessions, true, false, false, false
};
REGISTER_MCP_TOOL(s_tool_list_shutter_sessions);

static const McpTool s_tool_get_shutter_session = {
    "get_shutter_session",
    "Get one saved session's metadata (camera, notes, shot count, worst verdict, type, timestamps) plus a detail_url. The full per-shot record with per-sensor waveforms is large and is fetched by streaming detail_url (GET /api/sessions/{id}), not inlined here. Args: id (e.g. 'sess_3').",
    "{\"type\":\"object\",\"properties\":{\"id\":{\"type\":\"string\"}},\"required\":[\"id\"]}",
    tool_get_shutter_session, true, false, false, false
};
REGISTER_MCP_TOOL(s_tool_get_shutter_session);

static const McpTool s_tool_list_shutter_tests = {
    "list_shutter_tests",
    "List guided-test script definitions (id, name, speed_count, shots_per_speed).",
    "{\"type\":\"object\",\"properties\":{}}",
    tool_list_shutter_tests, true, false, false, false
};
REGISTER_MCP_TOOL(s_tool_list_shutter_tests);

static const McpTool s_tool_get_shutter_test = {
    "get_shutter_test",
    "Get one guided-test script definition including its ordered speed list. Args: id.",
    "{\"type\":\"object\",\"properties\":{\"id\":{\"type\":\"string\"}},\"required\":[\"id\"]}",
    tool_get_shutter_test, true, false, false, false
};
REGISTER_MCP_TOOL(s_tool_get_shutter_test);

static const McpTool s_tool_shutter_control = {
    "shutter_control",
    "Run a shutter-tester control command. command (required): set|adjust|toggle_lock|sess_start|sess_stop|sess_toggle|sess_discard|guide_start|guide_stop|guide_skip|guide_redo|align_start|align_stop|recalibrate. value rules: REQUIRED for 'set' (a standard shutter speed, bare or with a trailing 's', e.g. '1/125' or '1'), 'adjust' (exactly 'faster' or 'slower'), and 'guide_start' (a test id from list_shutter_tests); OPTIONAL camera name for 'sess_start'/'sess_toggle'; ignored for all other commands. Invalid speeds, directions, and test ids are rejected with an error. "
    "IMPORTANT WORKFLOW: measuring a shutter is a hands-on optical procedure — the user mounts the camera (back open) over the sensor array with a light source behind the shutter, and each measurement happens when the USER physically fires the shutter. You do NOT trigger captures; there is no per-shot command. Coordinate like this: (1) before capturing, have the user run align_start and adjust the rig until the alignment readout in get_shutter_status shows the sensors lit, then align_stop; call recalibrate only when the sensors are in the expected light state. (2) For a freeform run, sess_start, then tell the user to fire the shutter and read each result from get_shutter_status / get_shutter_history; repeat, then sess_stop. (3) For a guided run, guide_start <test id>, then loop: read the current step (target speed + shots remaining) from get_shutter_status, ask the user to fire that many times, let the device auto-advance, and use guide_skip/guide_redo as needed. set/adjust/toggle_lock only change the comparison target speed (no physical action).",
    "{\"type\":\"object\",\"properties\":{\"command\":{\"type\":\"string\",\"enum\":[\"set\",\"adjust\",\"toggle_lock\",\"sess_start\",\"sess_stop\",\"sess_toggle\",\"sess_discard\",\"guide_start\",\"guide_stop\",\"guide_skip\",\"guide_redo\",\"align_start\",\"align_stop\",\"recalibrate\"]},\"value\":{\"type\":\"string\"}},\"required\":[\"command\"]}",
    tool_shutter_control, false, false, true, false
};
REGISTER_MCP_TOOL(s_tool_shutter_control);

static const McpTool s_tool_delete_shutter_session = {
    "delete_shutter_session",
    "Delete a saved shutter test session by id (destructive).",
    "{\"type\":\"object\",\"properties\":{\"id\":{\"type\":\"string\"}},\"required\":[\"id\"]}",
    tool_delete_shutter_session, false, true, true, false
};
REGISTER_MCP_TOOL(s_tool_delete_shutter_session);

static const McpTool s_tool_set_shutter_tests = {
    "set_shutter_tests",
    "Overwrite the ENTIRE guided-test definition file (replaces all existing tests). Use this tool directly; do not POST/PUT to any HTTP endpoint. `content` is a plain-text DSL (NOT JSON), one test per block:\n"
    "name: <id>|<Display Name>\n"
    "shots_per_speed: <n>\n"
    "<speed>\n"
    "<speed>\n"
    "Repeat the whole block for more tests. Speeds are bare standard shutter speeds, one per line: whole seconds like '1' or '2', or fractions like '1/2', '1/60', '1/1000' (a trailing 's' is accepted and stripped). Only speeds in the standard table are kept. Lines starting with '#' are comments. To see valid output, call get_shutter_test after saving. Example content:\n"
    "name: leicam6|Leica M6 (x3)\n"
    "shots_per_speed: 3\n"
    "1\n"
    "1/2\n"
    "1/60\n"
    "1/1000",
    "{\"type\":\"object\",\"properties\":{\"content\":{\"type\":\"string\",\"description\":\"Full test-definition file text in the DSL described in this tool's description (plain text, not JSON).\"}},\"required\":[\"content\"]}",
    tool_set_shutter_tests, false, false, false, true
};
REGISTER_MCP_TOOL(s_tool_set_shutter_tests);

#endif // HAS_MCP && IS_SHUTTER_TESTER
