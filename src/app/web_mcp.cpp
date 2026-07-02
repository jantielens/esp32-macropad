#include "web_mcp.h"

#include "board_config.h"

#if HAS_MCP

#include "mcp_tool_registry.h"

#include "config_manager.h"
#include "class_branding.h"
#include "log_manager.h"
#include "psram_json_allocator.h"
#include "web_portal_json.h"
#include "web_portal_routes.h"
#include "web_portal_state.h"
#include "net_activity.h"

#include "version.h"

#include <ArduinoJson.h>
#include <string.h>

#include <esp_random.h>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/portmacro.h>
#include <freertos/task.h>

static const char* TAG = "MCP";

// ----------------------------------------------------------------------------
// Protocol + limits
// ----------------------------------------------------------------------------
static constexpr const char* MCP_PROTOCOL_VERSION = "2025-06-18";
static constexpr size_t   MCP_MAX_BODY_BYTES     = 8192;   // hard cap on the request body
static constexpr size_t   MCP_MAX_BODY_BYTES_AUTHORING = 48 * 1024; // pad writes (PSRAM only)
static constexpr uint32_t MCP_BODY_TIMEOUT_MS    = 5000;   // stale-body cleanup
static constexpr uint32_t MCP_CONTROL_TIMEOUT_MS = 2000;   // bounded wait for deferred control
static constexpr uint32_t MCP_REBOOT_GRACE_MS    = 400;    // flush the response before restart

// JSON-RPC + MCP error codes (canonical values in mcp_tool_registry.h).
static constexpr int MCP_ERR_PARSE        = MCP_RPC_ERR_PARSE;
static constexpr int MCP_ERR_INVALID_REQ  = MCP_RPC_ERR_INVALID_REQ;
static constexpr int MCP_ERR_METHOD       = MCP_RPC_ERR_METHOD;
static constexpr int MCP_ERR_PARAMS       = MCP_RPC_ERR_PARAMS;
static constexpr int MCP_ERR_INTERNAL     = MCP_RPC_ERR_INTERNAL;
static constexpr int MCP_ERR_CONTROL_BUSY = MCP_RPC_ERR_CONTROL_BUSY;

// ----------------------------------------------------------------------------
// Token: hardware-RNG generation + constant-time compare
// ----------------------------------------------------------------------------

size_t mcp_token_generate(char* out, size_t out_len) {
    // 16 random bytes (128-bit) -> 32 lowercase hex chars + NUL.
    if (!out || out_len < 33) return 0;
    uint8_t raw[16];
    esp_fill_random(raw, sizeof(raw));
    static const char hexd[] = "0123456789abcdef";
    for (size_t i = 0; i < sizeof(raw); ++i) {
        out[i * 2]     = hexd[(raw[i] >> 4) & 0x0F];
        out[i * 2 + 1] = hexd[raw[i] & 0x0F];
    }
    out[32] = '\0';
    return 32;
}

// Fixed-length, branch-free, length-difference-safe comparison of two buffers
// of exactly CONFIG_MCP_TOKEN_MAX_LEN bytes. Never branches on length or content
// before completing the full compare (no timing oracle, no short-buffer over-read
// because both operands are sized to the fixed window and zero-padded).
static bool mcp_token_equal_fixed(const uint8_t* a, const uint8_t* b) {
    volatile uint8_t diff = 0;
    for (size_t i = 0; i < CONFIG_MCP_TOKEN_MAX_LEN; ++i) {
        diff |= (uint8_t)(a[i] ^ b[i]);
    }
    return diff == 0;
}

// ----------------------------------------------------------------------------
// Request gate
// ----------------------------------------------------------------------------
// Returns 0 when the request may proceed, otherwise the HTTP status to send.
static int mcp_gate(AsyncWebServerRequest* request) {
    // STA-only: behave as disabled in AP/setup mode.
    if (web_portal_is_ap_mode_active()) return 404;

    DeviceConfig* cfg = web_portal_get_current_config();
    if (!cfg) return 404;

    // Feature off by default.
    if (!cfg->mcp_enabled) return 404;

    // Origin validation: reject any present Origin (empty allow-list).
    // Native desktop MCP clients send no Origin and pass.
    if (request->hasHeader("Origin")) {
        const AsyncWebHeader* o = request->getHeader("Origin");
        if (o && o->value().length() > 0) {
            return 403;
        }
    }

    // Fail closed: enabled but no token -> refuse all.
    if (cfg->mcp_token[0] == '\0') return 401;

    // Bearer token, independent of portal Basic Auth.
    if (!request->hasHeader("Authorization")) return 401;
    const AsyncWebHeader* auth = request->getHeader("Authorization");
    if (!auth) return 401;
    const String& av = auth->value();
    static const char kBearer[] = "Bearer ";
    if (!av.startsWith(kBearer)) return 401;
    const char* presented_str = av.c_str() + (sizeof(kBearer) - 1);

    // Zero-pad both operands to the fixed compare window.
    uint8_t configured[CONFIG_MCP_TOKEN_MAX_LEN];
    uint8_t presented[CONFIG_MCP_TOKEN_MAX_LEN];
    memset(configured, 0, sizeof(configured));
    memset(presented, 0, sizeof(presented));
    strlcpy((char*)configured, cfg->mcp_token, sizeof(configured));
    strlcpy((char*)presented, presented_str, sizeof(presented));

    if (!mcp_token_equal_fixed(configured, presented)) return 401;

    return 0;  // authorized
}

static bool mcp_control_enabled() {
    DeviceConfig* cfg = web_portal_get_current_config();
    return cfg && cfg->mcp_control_enabled;
}

static bool mcp_authoring_enabled() {
    DeviceConfig* cfg = web_portal_get_current_config();
    return cfg && cfg->mcp_authoring_enabled;
}

// Effective request-body cap. Authoring (pad writes) can exceed 8 KB; raise the
// cap when authoring is enabled and PSRAM is available, else hold the default.
static size_t mcp_body_cap() {
    if (mcp_authoring_enabled() && psramFound()) return MCP_MAX_BODY_BYTES_AUTHORING;
    return MCP_MAX_BODY_BYTES;
}

// ----------------------------------------------------------------------------
// Deferred control bridge (web task -> main loop)
// ----------------------------------------------------------------------------
// The generic slot + dispatch + drain live in main_loop_bridge.{h,cpp}; MCP
// call sites use it via the mcp_control_dispatch alias (web_mcp.h). Only the
// MCP-specific deferred reboot state remains here.

// Pending reboot timestamp (0 = none). Set on the main loop; honored by loop.
static volatile uint32_t  s_reboot_at_ms   = 0;

void mcp_request_reboot() {
    uint32_t at = millis() + MCP_REBOOT_GRACE_MS;
    if (at == 0) at = 1;  // 0 is the "none" sentinel
    s_reboot_at_ms = at;
}

// ----------------------------------------------------------------------------
// POST body accumulator (chunk-safe; mirrors handlePostConfig). Single in-flight.
// ----------------------------------------------------------------------------
static portMUX_TYPE g_body_mux = portMUX_INITIALIZER_UNLOCKED;
static struct {
    bool      in_progress;
    uint32_t  started_ms;
    size_t    total;
    size_t    received;
    uint8_t*  buf;
    void*     owner;        // originating AsyncWebServerRequest* (single in-flight)
} g_body = {false, 0, 0, 0, nullptr, nullptr};

static void body_reset_locked() {
    if (g_body.buf) {
        heap_caps_free(g_body.buf);
        g_body.buf = nullptr;
    }
    g_body.in_progress = false;
    g_body.total = 0;
    g_body.received = 0;
    g_body.started_ms = 0;
    g_body.owner = nullptr;
}

// Forward decl: dispatch a fully-accumulated JSON-RPC envelope.
static void mcp_dispatch(AsyncWebServerRequest* request, uint8_t* body, size_t len);

static void mcp_send_http(AsyncWebServerRequest* request, int code) {
    // Minimal HTTP-level rejection (gate failures). Body kept short + JSON.
    request->send(code, "application/json",
                  code == 404 ? "{\"error\":\"not found\"}"
                  : code == 403 ? "{\"error\":\"forbidden\"}"
                  : "{\"error\":\"unauthorized\"}");
}

// Send a bare JSON-RPC error envelope (id: null) as HTTP 200 for failures that
// occur before or around dispatch (oversize body, busy, OOM, empty request),
// where no request id is available. `message` must be JSON-safe (no quotes or
// newlines).
static void mcp_send_rpc_http_error(AsyncWebServerRequest* request, int code, const char* message) {
    String body = "{\"jsonrpc\":\"2.0\",\"id\":null,\"error\":{\"code\":";
    body += code;
    body += ",\"message\":\"";
    body += (message ? message : "error");
    body += "\"}}";
    request->send(200, "application/json", body);
}

static void handleMcpBody(AsyncWebServerRequest* request, uint8_t* data,
                          size_t len, size_t index, size_t total) {
    if (index == 0) {
        // Gate before accepting any body.
        int gate = mcp_gate(request);
        if (gate != 0) {
            mcp_send_http(request, gate);
            // Mark so trailing chunks are ignored.
            portENTER_CRITICAL(&g_body_mux);
            body_reset_locked();
            portEXIT_CRITICAL(&g_body_mux);
            return;
        }

        // Reset a stuck prior body.
        const uint32_t now = millis();
        portENTER_CRITICAL(&g_body_mux);
        const bool stale = g_body.in_progress && g_body.started_ms &&
                           (now - g_body.started_ms > MCP_BODY_TIMEOUT_MS);
        if (stale) body_reset_locked();

        if (g_body.in_progress) {
            portEXIT_CRITICAL(&g_body_mux);
            // Another request is mid-flight; reject as a JSON-RPC error.
            mcp_send_rpc_http_error(request, MCP_ERR_INTERNAL, "server busy");
            return;
        }

        // Hard cap: reject oversize envelopes before allocation.
        if (total == 0 || total > mcp_body_cap()) {
            portEXIT_CRITICAL(&g_body_mux);
            mcp_send_rpc_http_error(request, MCP_ERR_INVALID_REQ, "request too large");
            return;
        }

        uint8_t* buf = nullptr;
        if (psramFound()) {
            buf = (uint8_t*)heap_caps_malloc(total + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        }
        if (!buf) {
            buf = (uint8_t*)heap_caps_malloc(total + 1, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        }
        if (!buf) {
            portEXIT_CRITICAL(&g_body_mux);
            mcp_send_rpc_http_error(request, MCP_ERR_INTERNAL, "out of memory");
            return;
        }

        g_body.in_progress = true;
        g_body.started_ms  = now;
        g_body.total       = total;
        g_body.received    = 0;
        g_body.buf         = buf;
        g_body.owner       = request;
        portEXIT_CRITICAL(&g_body_mux);
    }

    // Copy this chunk. Bind to the originating request so a concurrent POST that
    // was rejected at index 0 cannot splice its chunks into this buffer.
    portENTER_CRITICAL(&g_body_mux);
    const bool ok = g_body.in_progress && g_body.buf && g_body.owner == request &&
                    g_body.total == total && (index + len) <= total;
    uint8_t* dst = g_body.buf;
    portEXIT_CRITICAL(&g_body_mux);

    if (!ok) {
        // Either we rejected at index 0 (gate/oversize) or state is invalid.
        return;
    }

    memcpy(dst + index, data, len);

    portENTER_CRITICAL(&g_body_mux);
    const size_t new_received = index + len;
    if (new_received > g_body.received) g_body.received = new_received;
    const bool done = (g_body.received >= g_body.total);
    portEXIT_CRITICAL(&g_body_mux);

    if (!done) return;

    // Finalize.
    uint8_t* body = nullptr;
    size_t   body_len = 0;
    portENTER_CRITICAL(&g_body_mux);
    body = g_body.buf;
    body_len = g_body.total;
    if (body) body[body_len] = 0;
    // Detach buffer from the accumulator; we own it now and free below.
    g_body.buf = nullptr;
    body_reset_locked();
    portEXIT_CRITICAL(&g_body_mux);

    if (body) {
        mcp_dispatch(request, body, body_len);
        heap_caps_free(body);
    }
}

// ----------------------------------------------------------------------------
// JSON-RPC response helpers
// ----------------------------------------------------------------------------
static void mcp_send_error(AsyncWebServerRequest* request,
                           JsonVariantConst id, int code, const char* message) {
    const size_t msglen = message ? strlen(message) : 0;
    auto doc = make_psram_json_doc(256 + msglen);
    if (!doc || doc->capacity() == 0) {
        request->send(500, "application/json", "{\"error\":\"oom\"}");
        return;
    }
    (*doc)["jsonrpc"] = "2.0";
    if (id.isNull()) (*doc)["id"] = (char*)nullptr;
    else (*doc)["id"] = id;
    JsonObject err = doc->createNestedObject("error");
    err["code"] = code;
    // Copy the message into the document. The response is serialized lazily by
    // the async filler AFTER this handler returns, so a const char* pointing at
    // a caller's temporary String (e.g. err.c_str()) would dangle. Assigning a
    // String forces ArduinoJson to duplicate the bytes (const char* is stored by
    // reference and must NOT be used for non-static messages).
    err["message"] = String(message ? message : "error");
    web_portal_send_json_sized(request, doc);
}

// ----------------------------------------------------------------------------
// Method handlers
// ----------------------------------------------------------------------------
static void mcp_method_initialize(AsyncWebServerRequest* request, JsonVariantConst id) {
    auto doc = make_psram_json_doc(4096);
    if (!doc || doc->capacity() == 0) { mcp_send_error(request, id, MCP_ERR_INTERNAL, "oom"); return; }
    (*doc)["jsonrpc"] = "2.0";
    (*doc)["id"] = id;
    JsonObject result = doc->createNestedObject("result");
    result["protocolVersion"] = MCP_PROTOCOL_VERSION;
    JsonObject caps = result.createNestedObject("capabilities");
    caps.createNestedObject("tools");  // tools capability advertised

    // Server-level guidance surfaced to the model (MCP `instructions`). Gives a
    // board-agnostic orientation to the firmware and the core read -> control
    // tool-chaining so the model understands what the device is and how the
    // tools compose; on display boards it then appends the screenshot-via-browser
    // recipe for visual verification (also mirrored in
    // get_capabilities.visual_inspection).
    String instructions =
        "This is an ESP32 device running Macropad firmware (device class: ";
    instructions += device_class_get_display_name();
    instructions += "). ";
    // Specialized classes (darkroom, coffee scale, shutter, ...) advertise their
    // core use case in one sentence so the model knows what the device is FOR;
    // the generic macropad adds nothing here.
    const char* scenario = mcp_class_scenario();
    if (scenario && scenario[0]) {
        instructions += scenario;
        instructions += " ";
    }
    instructions +=
        "Read tools inspect live state; control tools drive the device. Orient yourself first with "
        "get_device_status (identity, current screen, WiFi) and get_health (heap, CPU, signal); "
        "get_sensors returns any attached sensor readings. General pattern: DISCOVER with the read "
        "tools, then act, then re-read to VERIFY. To operate the UI, list_screens shows navigable "
        "screens and set_screen switches to one; list_pads then get_pad reveal a pad's buttons (their "
        "array position, label, and action) so you can press_button by position or label. press_button "
        "runs the button's REAL action \u2014 it may publish MQTT, call Home Assistant, send BLE "
        "keystrokes, navigate, or reboot \u2014 exactly as a physical tap, so inspect an unfamiliar "
        "button with get_pad before pressing it. Control tools (press_button, set_screen, set_backlight, "
        "wake, system_command) appear only when the control toggle is enabled; system_command 'reboot' "
        "restarts the device and drops this connection.";
#if HAS_DISPLAY
    instructions +=
        " This device drives a touch display, so you can visually verify UI changes: GET /api/screenshot "
        "returns the live framebuffer as a large BMP image. Never fetch it as text/data \u2014 it is an "
        "image and is large. Instead point a Playwright browser at the URL and capture the rendered <img> "
        "element. Call get_capabilities for the exact recipe (visual_inspection) plus the full "
        "pad/widget/binding schema. Verifying a specific pad first needs set_screen (a control tool) to "
        "bring it on-screen.";
#endif
    result["instructions"] = instructions;

    JsonObject info = result.createNestedObject("serverInfo");
    DeviceConfig* cfg = web_portal_get_current_config();
    const char* name = (cfg && cfg->device_name[0]) ? cfg->device_name
                                                     : device_class_get_full_name();
    info["name"] = name;
    info["version"] = FIRMWARE_VERSION;
    web_portal_send_json_sized(request, doc);
}

static void mcp_append_tool_def(JsonArray tools, const McpTool* t) {
    JsonObject td = tools.createNestedObject();
    td["name"] = t->name;
    td["description"] = t->description;
    // inputSchema is a JSON Schema string literal; parse it into the response.
    bool schema_set = false;
    if (t->input_schema_json && t->input_schema_json[0]) {
        // Sized for the largest tool schemas (set_pad with all pad fields,
        // set_buttons' nested array). Allocated in PSRAM (not on the async-web
        // task stack). Too small a buffer silently degrades a tool to a
        // permissive {type:object}, dropping its declared params.
        auto sd = make_psram_json_doc(2048);
        if (sd && sd->capacity() > 0 &&
            deserializeJson(*sd, t->input_schema_json) == DeserializationError::Ok) {
            td["inputSchema"] = *sd;  // deep copy
            schema_set = true;
        } else {
            // Schema too large for the parse buffer or malformed — fall back to
            // a permissive object schema and flag it so the tool author notices.
            LOGW(TAG, "tool '%s' inputSchema unparseable; using {type:object}", t->name);
        }
    }
    if (!schema_set) {
        JsonObject schema = td.createNestedObject("inputSchema");
        schema["type"] = "object";
    }
    // Tool annotations.
    JsonObject ann = td.createNestedObject("annotations");
    ann["readOnlyHint"] = t->read_only;
    ann["destructiveHint"] = t->destructive;
    // Read tools are idempotent; among control tools, only destructive ones
    // (e.g. reboot) are not safely repeatable.
    ann["idempotentHint"] = t->read_only || !t->destructive;
}

static void mcp_method_tools_list(AsyncWebServerRequest* request, JsonVariantConst id) {
    auto doc = make_psram_json_doc(8192);
    if (!doc || doc->capacity() == 0) { mcp_send_error(request, id, MCP_ERR_INTERNAL, "oom"); return; }
    (*doc)["jsonrpc"] = "2.0";
    (*doc)["id"] = id;
    JsonObject result = doc->createNestedObject("result");
    JsonArray tools = result.createNestedArray("tools");

    const bool control_on = mcp_control_enabled();
    const bool authoring_on = mcp_authoring_enabled();
    const uint8_t n = mcp_tool_count();
    for (uint8_t i = 0; i < n; ++i) {
        const McpTool* t = mcp_tool_at(i);
        if (!t) continue;
        // Filter control tools when control is disabled.
        if (t->requires_control && !control_on) continue;
        // Filter authoring tools when authoring is disabled.
        if (t->requires_authoring && !authoring_on) continue;
        mcp_append_tool_def(tools, t);
    }
    web_portal_send_json_sized(request, doc);
}

static void mcp_method_tools_call(AsyncWebServerRequest* request, JsonVariantConst id,
                                  JsonObjectConst params) {
    if (params.isNull()) { mcp_send_error(request, id, MCP_ERR_PARAMS, "missing params"); return; }
    const char* name = params["name"];
    if (!name || !name[0]) { mcp_send_error(request, id, MCP_ERR_PARAMS, "missing tool name"); return; }

    const McpTool* tool = mcp_tool_find(name);
    if (!tool) { mcp_send_error(request, id, MCP_ERR_PARAMS, "unknown tool"); return; }

    // Control gate.
    if (tool->requires_control && !mcp_control_enabled()) {
        mcp_send_error(request, id, MCP_ERR_METHOD, "tool not available (control disabled)");
        return;
    }
    // Authoring gate.
    if (tool->requires_authoring && !mcp_authoring_enabled()) {
        mcp_send_error(request, id, MCP_ERR_METHOD, "tool not available (authoring disabled)");
        return;
    }

    // Build args object (default empty). Authoring tools may carry whole-pad
    // JSON, so size their args doc to the authoring body cap.
    size_t args_cap = (tool->requires_authoring ? mcp_body_cap() : (size_t)2048);
    auto argsDoc = make_psram_json_doc(args_cap);
    if (!argsDoc || argsDoc->capacity() == 0) { mcp_send_error(request, id, MCP_ERR_INTERNAL, "oom"); return; }
    JsonObject args = argsDoc->to<JsonObject>();
    JsonObjectConst inArgs = params["arguments"];
    if (!inArgs.isNull()) {
        for (JsonPairConst kv : inArgs) args[kv.key()] = kv.value();
    }

    // Result document the handler writes into.
    auto resultDoc = make_psram_json_doc(24576);
    if (!resultDoc || resultDoc->capacity() == 0) { mcp_send_error(request, id, MCP_ERR_INTERNAL, "oom"); return; }
    JsonObject toolResult = resultDoc->to<JsonObject>();
    String err;

    bool ok = tool->handler(args, toolResult, err);

    if (!ok) {
        // Optional handler-provided error code; default to internal error.
        int code = MCP_ERR_INTERNAL;
        if (toolResult.containsKey(MCP_RESULT_ERRCODE_KEY)) {
            code = toolResult[MCP_RESULT_ERRCODE_KEY] | MCP_ERR_INTERNAL;
        }
        mcp_send_error(request, id, code, err.length() ? err.c_str() : "tool error");
        return;
    }

    // Guard: a tool whose output exceeded the result document capacity would
    // otherwise serialize as valid-but-truncated JSON (silent data loss, e.g.
    // get_pad on a large pad). Fail loudly so the caller can narrow the request.
    if (resultDoc->overflowed()) {
        mcp_send_error(request, id, MCP_ERR_INTERNAL,
                       "tool result too large; narrow the request");
        return;
    }

    // Wrap the tool result as a single text content item.
    String resultStr;
    serializeJson(toolResult, resultStr);

    auto doc = make_psram_json_doc(resultStr.length() + 512);
    if (!doc || doc->capacity() == 0) { mcp_send_error(request, id, MCP_ERR_INTERNAL, "oom"); return; }
    (*doc)["jsonrpc"] = "2.0";
    (*doc)["id"] = id;
    JsonObject result = doc->createNestedObject("result");
    JsonArray content = result.createNestedArray("content");
    JsonObject item = content.createNestedObject();
    item["type"] = "text";
    item["text"] = resultStr;
    result["isError"] = false;
    web_portal_send_json_sized(request, doc);
}

// ----------------------------------------------------------------------------
// JSON-RPC envelope dispatch
// ----------------------------------------------------------------------------
static void mcp_dispatch(AsyncWebServerRequest* request, uint8_t* body, size_t len) {
    net_activity_mark(NET_CH_MCP);
    auto reqDoc = make_psram_json_doc(len + 1024);
    if (!reqDoc || reqDoc->capacity() == 0) {
        mcp_send_rpc_http_error(request, MCP_ERR_INTERNAL, "out of memory");
        return;
    }
    DeserializationError perr = deserializeJson(*reqDoc, body, len);
    if (perr) {
        mcp_send_error(request, JsonVariantConst(), MCP_ERR_PARSE, "parse error");
        return;
    }

    JsonObjectConst root = reqDoc->as<JsonObjectConst>();
    if (root.isNull()) {
        mcp_send_error(request, JsonVariantConst(), MCP_ERR_INVALID_REQ, "invalid request");
        return;
    }

    const char* method = root["method"];
    JsonVariantConst id = root["id"];
    const bool is_notification = !root.containsKey("id");

    if (!method) {
        if (is_notification) { request->send(202); return; }
        mcp_send_error(request, id, MCP_ERR_INVALID_REQ, "missing method");
        return;
    }

    // Notifications (no id) -> 202 Accepted, no body.
    if (is_notification) {
        request->send(202);
        return;
    }

    if (strcmp(method, "initialize") == 0) {
        mcp_method_initialize(request, id);
    } else if (strcmp(method, "ping") == 0) {
        // Empty success result.
        auto doc = make_psram_json_doc(128);
        if (!doc || doc->capacity() == 0) { mcp_send_error(request, id, MCP_ERR_INTERNAL, "oom"); return; }
        (*doc)["jsonrpc"] = "2.0";
        (*doc)["id"] = id;
        doc->createNestedObject("result");
        web_portal_send_json_sized(request, doc);
    } else if (strcmp(method, "tools/list") == 0) {
        mcp_method_tools_list(request, id);
    } else if (strcmp(method, "tools/call") == 0) {
        mcp_method_tools_call(request, id, root["params"].as<JsonObjectConst>());
    } else {
        mcp_send_error(request, id, MCP_ERR_METHOD, "method not found");
    }
}

// ----------------------------------------------------------------------------
// Registration + loop
// ----------------------------------------------------------------------------

// GET/DELETE on /mcp. This server is JSON-only and does not open a
// server->client SSE stream, so per the MCP Streamable HTTP spec it returns
// 405 Method Not Allowed (not 404) — otherwise clients like VS Code treat the
// endpoint as missing and fall back to the legacy SSE transport. When MCP is
// disabled or in AP mode the endpoint stays invisible (404), matching POST.
static void handle_mcp_other_method(AsyncWebServerRequest* request) {
    if (web_portal_is_ap_mode_active()) { mcp_send_http(request, 404); return; }
    DeviceConfig* cfg = web_portal_get_current_config();
    if (!cfg || !cfg->mcp_enabled) { mcp_send_http(request, 404); return; }
    AsyncWebServerResponse* resp = request->beginResponse(405, "application/json",
        "{\"jsonrpc\":\"2.0\",\"id\":null,\"error\":{\"code\":-32000,"
        "\"message\":\"Method Not Allowed: POST JSON-RPC only (no SSE stream)\"}}");
    resp->addHeader("Allow", "POST");
    request->send(resp);
}

// Custom handler for /mcp.
//
// MCP Streamable HTTP clients (VS Code, Cursor, the MCP SDK) always send
// `Accept: application/json, text/event-stream`. ESPAsyncWebServer flags any
// request whose Accept contains `text/event-stream` as an event-source request
// (RCT_EVENT), which makes request->isHTTP() return false. The stock
// AsyncCallbackWebHandler::canHandle() rejects such requests (`!isHTTP()`), so
// a normal server->on() route never matches them and they fall through to
// onNotFound (404). This handler bypasses that check so /mcp works for real MCP
// clients while still answering curl/browsers.
class McpWebHandler : public AsyncWebHandler {
public:
    bool canHandle(AsyncWebServerRequest* request) const override {
        const String& u = request->url();
        if (u != "/mcp" && u != "/mcp/") return false;
        const uint8_t m = (uint8_t)request->method();
        return (m == HTTP_POST || m == HTTP_GET || m == HTTP_DELETE);
    }
    void handleRequest(AsyncWebServerRequest* request) override {
        const uint8_t m = (uint8_t)request->method();
        if (m == HTTP_GET || m == HTTP_DELETE) {
            handle_mcp_other_method(request);
            return;
        }
        // POST with no body: the body handler never fires.
        if (request->contentLength() == 0) {
            mcp_send_rpc_http_error(request, MCP_ERR_INVALID_REQ, "empty request");
        }
    }
    void handleBody(AsyncWebServerRequest* request, uint8_t* data, size_t len,
                    size_t index, size_t total) override {
        handleMcpBody(request, data, len, index, total);
    }
    // Non-trivial: we must receive the request body before responding.
    bool isRequestHandlerTrivial() const override { return false; }
};

void web_mcp_register(AsyncWebServer* server) {
    if (!server) return;
    loop_bridge_init();
    // Use a custom AsyncWebHandler (not server->on) so /mcp matches requests
    // that carry `Accept: text/event-stream` — which the stock callback handler
    // rejects via isHTTP(). The handler dispatches POST (JSON-RPC) vs GET/DELETE
    // (405) internally.
    server->addHandler(new McpWebHandler());
    LOGI(TAG, "MCP endpoint registered at /mcp");
    if (mcp_tool_dropped() > 0) {
        LOGW(TAG, "MCP tool registry full at %u; %u tool(s) dropped — raise MCP_TOOL_REGISTRY_MAX",
             mcp_tool_count(), mcp_tool_dropped());
    }
}
REGISTER_ROUTES(web_mcp_register);

void web_mcp_loop() {
    // 1) Stale request-body cleanup.
    const uint32_t now = millis();
    portENTER_CRITICAL(&g_body_mux);
    const bool stale = g_body.in_progress && g_body.started_ms &&
                       (now - g_body.started_ms > MCP_BODY_TIMEOUT_MS);
    if (stale) body_reset_locked();
    portEXIT_CRITICAL(&g_body_mux);
    if (stale) LOGW(TAG, "MCP body timed out (loop cleanup)");

    // 2) The deferred-job bridge is drained by web_portal_handle()
    //    (loop_bridge_drain), so it runs regardless of HAS_MCP.

    // 3) Deferred reboot: fire after the grace period so the
    //    JSON-RPC response has flushed.
    const uint32_t at = s_reboot_at_ms;
    if (at != 0 && (int32_t)(now - at) >= 0) {
        LOGW(TAG, "MCP system_command: rebooting");
        s_reboot_at_ms = 0;
        ESP.restart();
    }
}

#else // !HAS_MCP

// Feature compiled out. Provide link stubs for the symbols other translation
// units (web_portal, web_portal_config) reference. web_mcp_register is not
// defined, so no /mcp route is registered.
void web_mcp_loop() {}
size_t mcp_token_generate(char* out, size_t out_len) {
    (void)out;
    (void)out_len;
    return 0;
}

#endif // HAS_MCP
