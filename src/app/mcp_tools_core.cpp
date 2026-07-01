// ============================================================================
// mcp_tools_core.cpp — universal (board-agnostic) MCP tools.
//
// Compiled directly (sketch root). Read tools are always available (subject to
// the bearer token); control tools self-gate on mcp_control_enabled and defer
// all action_dispatch / LVGL / display work to the main loop via
// mcp_control_dispatch() (web_mcp.h) — never inline on the web task.
// ============================================================================

#include "mcp_tool_registry.h"
#include "mcp_tool_util.h"
#include "web_mcp.h"

#include "board_config.h"

#if HAS_MCP
#include "class_branding.h"
#include "config_manager.h"
#include "device_telemetry.h"
#include "log_manager.h"
#include "sensors/sensor_manager.h"
#include "version.h"
#include "web_portal_json.h"
#include "web_portal_state.h"

#include <ArduinoJson.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <string.h>

#if HAS_DISPLAY
#include "display_manager.h"
#include "screen_saver_manager.h"
#include "pad_config.h"
#endif

#if HAS_DISPLAY || HAS_BUTTON
#include "action_dispatch.h"
#include "action_list.h"
#include "action_parse.h"
#include "pad_config.h"
#endif

#if !(HAS_DISPLAY || HAS_BUTTON)
#include "wifi_manager.h"
#endif

// JSON-RPC error codes used by tool handlers (canonical values in
// mcp_tool_registry.h).
static constexpr int TOOL_ERR_PARAMS   = MCP_RPC_ERR_PARAMS;
static constexpr int TOOL_ERR_INTERNAL = MCP_RPC_ERR_INTERNAL;

static constexpr uint32_t TOOL_CONTROL_TIMEOUT_MS = 2000;

// Thin adapter over the shared mcp_tool_fail (mcp_tool_util.h): keeps the local
// call sites readable while the fail logic lives in one place.
static bool tool_fail(JsonObject& result, String& err, int code, const char* msg) {
    return mcp_tool_fail(result, err, code, msg);
}

// ============================================================================
// Read tools (always available)
// ============================================================================

static bool tool_get_device_status(const JsonObject& args, JsonObject& result, String& err) {
    (void)args; (void)err;
    result["firmware_version"] = FIRMWARE_VERSION;
    result["device_class"] = device_class_get_display_name();
    result["device_class_full"] = device_class_get_full_name();
#ifdef BUILD_BOARD_NAME
    result["board"] = BUILD_BOARD_NAME;
#else
    result["board"] = "unknown";
#endif
    DeviceConfig* cfg = web_portal_get_current_config();
    result["device_name"] = (cfg && cfg->device_name[0]) ? cfg->device_name : "";
    result["uptime_seconds"] = (uint32_t)(millis() / 1000);
    result["has_display"] = (bool)HAS_DISPLAY;

#if HAS_DISPLAY
    const char* cs = display_manager_get_current_screen_id();
    result["current_screen"] = cs ? cs : "";
#endif

    JsonObject wifi = result.createNestedObject("wifi");
    const bool connected = (WiFi.status() == WL_CONNECTED);
    wifi["connected"] = connected;
    wifi["ssid"] = WiFi.SSID();
    wifi["rssi"] = (int)WiFi.RSSI();
    wifi["ip"] = WiFi.localIP().toString();
    wifi["hostname"] = WiFi.getHostname() ? WiFi.getHostname() : "";
    return true;
}

static bool tool_get_health(const JsonObject& args, JsonObject& result, String& err) {
    (void)args; (void)err;
    // Reuse the same telemetry source as /api/health. device_telemetry_fill_api
    // fills a JsonDocument (top-level keys), so it cannot write into the handler's
    // `result` object directly — we fill a small temp doc and copy the keys over.
    auto td = make_psram_json_doc(2048);
    if (!td || td->capacity() == 0) {
        return tool_fail(result, err, TOOL_ERR_INTERNAL, "out of memory");
    }
    device_telemetry_fill_api(*td);
    JsonObjectConst src = td->as<JsonObjectConst>();
    for (JsonPairConst kv : src) {
        result[kv.key()] = kv.value();
    }
    return true;
}

static bool tool_get_sensors(const JsonObject& args, JsonObject& result, String& err) {
    (void)args; (void)err;
    // sensor_manager_append_api writes one key per sensor reading; empty on
    // boards without sensors.
    JsonObject readings = result.createNestedObject("sensors");
    sensor_manager_append_api(readings);
    return true;
}

#if HAS_DISPLAY

static bool tool_list_screens(const JsonObject& args, JsonObject& result, String& err) {
    (void)args; (void)err;
    size_t n = 0;
    const ScreenInfo* screens = display_manager_get_available_screens(&n);
    JsonArray arr = result.createNestedArray("screens");
    for (size_t i = 0; i < n; ++i) {
        JsonObject o = arr.createNestedObject();
        o["id"] = screens[i].id;
        o["name"] = screens[i].display_name;
        // For pad screens, surface the editable friendly name (consistent with
        // list_pads and the portal); display_manager only holds the default
        // "Pad N" label.
        const char* sid = screens[i].id;
        if (sid && strncmp(sid, "pad_", 4) == 0) {
            char nm[64];
            if (pad_config_read_name((uint8_t)atoi(sid + 4), nm, sizeof(nm))) o["name"] = nm;
        }
    }
    const char* cs = display_manager_get_current_screen_id();
    result["current_screen"] = cs ? cs : "";
    return true;
}

static bool tool_get_current_screen(const JsonObject& args, JsonObject& result, String& err) {
    (void)args; (void)err;
    const char* cs = display_manager_get_current_screen_id();
    result["screen"] = cs ? cs : "";
    return true;
}

// First non-empty label (center > top > bottom).
static const char* button_label(const ScreenButtonConfig& b) {
    if (b.label_center[0]) return b.label_center;
    if (b.label_top[0]) return b.label_top;
    if (b.label_bottom[0]) return b.label_bottom;
    return "";
}

// Append a pad's buttons into `po` (a JSON object) under "buttons".
// Strings are wrapped in String() so ArduinoJson COPIES them into the result
// document: they point into `cfg`, which the caller reuses for the next pad and
// then frees before the dispatcher serializes the result. Assigning the raw
// const char* would only link the pointer and dangle (garbage / wrong labels).
static void append_pad_buttons(JsonObject po, const PadConfig* cfg) {
    JsonArray btns = po.createNestedArray("buttons");
    for (uint8_t b = 0; b < cfg->button_count; ++b) {
        const ScreenButtonConfig& btn = cfg->buttons[b];
        JsonObject bo = btns.createNestedObject();
        bo["position"] = b;
        bo["col"] = btn.col;
        bo["row"] = btn.row;
        bo["label"] = String(button_label(btn));
        const char* at = (btn.action_count > 0 && btn.actions[0].type[0]) ? btn.actions[0].type : "";
        bo["action_type"] = String(at);
        if (btn.widget.type[0]) bo["widget"] = String(btn.widget.type);
    }
}

static bool tool_list_pads(const JsonObject& args, JsonObject& result, String& err) {
    // Enumerates configured pads on the web task: each existing pad is loaded
    // from flash into a single reused PadConfig buffer (PSRAM-preferred). Work is
    // bounded by MAX_PADS and skipped for non-existent pads; pass the optional
    // "screen" filter (e.g. "pad_2") to inspect a single pad and avoid loading
    // every pad on large multi-pad configs.
    const char* filter = args["screen"] | (const char*)nullptr;
    int filter_page = -1;
    if (filter && strncmp(filter, "pad_", 4) == 0) {
        filter_page = atoi(filter + 4);
    }

    PadConfig* cfg = (PadConfig*)mcp_psram_alloc(sizeof(PadConfig));
    if (!cfg) return tool_fail(result, err, TOOL_ERR_INTERNAL, "out of memory");

    JsonArray pads = result.createNestedArray("pads");
    char sid[16];
    for (uint8_t pg = 0; pg < MAX_PADS; ++pg) {
        if (filter_page >= 0 && pg != filter_page) continue;
        if (!pad_config_exists(pg)) continue;
        if (!pad_config_load(pg, cfg)) continue;

        snprintf(sid, sizeof(sid), "pad_%u", (unsigned)pg);
        JsonObject po = pads.createNestedObject();
        po["screen"] = sid;
        char nm[64];
        if (pad_config_read_name(pg, nm, sizeof(nm))) po["name"] = nm;  // friendly label
        po["button_count"] = cfg->button_count;
        append_pad_buttons(po, cfg);
    }
    free(cfg);
    return true;
}

static bool tool_get_pad(const JsonObject& args, JsonObject& result, String& err) {
    const char* screen = args["screen"] | (const char*)nullptr;
    // Accept either the canonical id 'pad_N' or a pad's friendly name.
    char rerr[160];
    int pg = pad_config_resolve_ref(screen ? screen : "", rerr, sizeof(rerr));
    if (pg < 0) {
        return tool_fail(result, err, TOOL_ERR_PARAMS, rerr);
    }
    if (!pad_config_exists((uint8_t)pg)) {
        return tool_fail(result, err, TOOL_ERR_PARAMS, "pad not configured");
    }

    // Return the raw stored pad JSON verbatim. This is the single source of
    // truth and round-trips losslessly: widget type-specific knobs live in an
    // opaque blob in the parsed PadConfig and cannot be reconstructed, but the
    // raw JSON still holds every field (widget_*, label_*_style, icon_id,
    // actions/lp_actions, template_pad, bindings, ...). Keys here match exactly
    // what set_button / set_pad accept.
    size_t len = 0;
    char* raw = pad_config_read_raw((uint8_t)pg, &len);
    if (!raw) return tool_fail(result, err, TOOL_ERR_INTERNAL, "failed to read pad");

    auto doc = make_psram_json_doc(len * 2 + 1024);
    if (!doc || doc->capacity() == 0) {
        free(raw);
        return tool_fail(result, err, TOOL_ERR_INTERNAL, "out of memory");
    }
    DeserializationError perr = deserializeJson(*doc, raw, len);
    free(raw);
    if (perr) return tool_fail(result, err, TOOL_ERR_INTERNAL, "stored pad JSON is invalid");

    // Copy every stored key through unchanged, then add a few read-only helpers.
    JsonObjectConst src = doc->as<JsonObjectConst>();
    for (JsonPairConst kv : src) result[kv.key()] = kv.value();
    char sid[16];
    snprintf(sid, sizeof(sid), "pad_%d", pg);
    result["screen"] = sid;  // canonical id (the arg may have been a friendly name)

    JsonArray btns = result["buttons"];
    if (!btns.isNull()) {
        result["button_count"] = btns.size();
        uint8_t i = 0;
        for (JsonObject b : btns) b["position"] = i++;  // array index = set_button position
    } else {
        result["button_count"] = 0;
    }
    return true;
}

#endif // HAS_DISPLAY

// ============================================================================
// Control tools (gated by mcp_control_enabled; deferred to the main loop)
// ============================================================================

#if HAS_DISPLAY

// --- press_button ----------------------------------------------------------
struct PressCtx {
    uint8_t page;
    int16_t position;       // -1 = match by label
    char label[CONFIG_LABEL_MAX_LEN];
};

static void exec_press_button(const void* ctx, bool* ok, char* msg, size_t msg_len) {
    const PressCtx* c = (const PressCtx*)ctx;
    *ok = false;

    PadConfig* cfg = (PadConfig*)mcp_psram_alloc(sizeof(PadConfig));
    if (!cfg) { strlcpy(msg, "out of memory", msg_len); return; }

    if (!pad_config_load(c->page, cfg)) {
        free(cfg);
        strlcpy(msg, "pad not found", msg_len);
        return;
    }

    int idx = -1;
    if (c->position >= 0) {
        if (c->position < cfg->button_count) idx = c->position;
    } else {
        for (uint8_t b = 0; b < cfg->button_count; ++b) {
            const char* lbl = button_label(cfg->buttons[b]);
            if (lbl[0] && strcasecmp(lbl, c->label) == 0) { idx = b; break; }
        }
    }

    if (idx < 0) {
        free(cfg);
        strlcpy(msg, "button not found", msg_len);
        return;
    }

    const ScreenButtonConfig& btn = cfg->buttons[idx];
    if (btn.action_count == 0) {
        free(cfg);
        strlcpy(msg, "button has no action", msg_len);
        *ok = true;  // pressing a no-action button is a valid no-op
        return;
    }

    // Dispatch the primary tap action(s) exactly as a physical tap would.
    action_list_dispatch(btn.actions, btn.action_count, "MCP");
    screen_saver_manager_notify_activity(true);
    free(cfg);
    *ok = true;
    strlcpy(msg, "pressed", msg_len);
}

static bool tool_press_button(const JsonObject& args, JsonObject& result, String& err) {
    const char* screen = args["screen"] | (const char*)nullptr;
    const char* label = args["label"] | (const char*)nullptr;
    const bool has_pos = args.containsKey("position");

    if (!screen || strncmp(screen, "pad_", 4) != 0) {
        return tool_fail(result, err, TOOL_ERR_PARAMS, "screen must be a pad id like 'pad_0'");
    }
    int pg = atoi(screen + 4);
    if (pg < 0 || pg >= MAX_PADS) {
        return tool_fail(result, err, TOOL_ERR_PARAMS, "pad index out of range");
    }
    if (!has_pos && (!label || !label[0])) {
        return tool_fail(result, err, TOOL_ERR_PARAMS, "provide either position or label");
    }

    PressCtx ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.page = (uint8_t)pg;
    ctx.position = has_pos ? (int16_t)(args["position"] | 0) : -1;
    if (label) strlcpy(ctx.label, label, sizeof(ctx.label));

    return mcp_run_control(exec_press_button, &ctx, sizeof(ctx),
                           TOOL_CONTROL_TIMEOUT_MS, result, err);
}

// --- set_screen ------------------------------------------------------------
struct ScreenCtx { char screen[CONFIG_SCREEN_ID_MAX_LEN]; };

static void exec_set_screen(const void* ctx, bool* ok, char* msg, size_t msg_len) {
    const ScreenCtx* c = (const ScreenCtx*)ctx;
    bool success = false;
    display_manager_show_screen(c->screen, &success);
    if (success) {
        screen_saver_manager_notify_activity(true);
        *ok = true;
        strlcpy(msg, "screen changed", msg_len);
    } else {
        *ok = false;
        strlcpy(msg, "screen not found", msg_len);
    }
}

static bool tool_set_screen(const JsonObject& args, JsonObject& result, String& err) {
    const char* screen = args["screen"] | (const char*)nullptr;
    if (!screen || !screen[0]) {
        return tool_fail(result, err, TOOL_ERR_PARAMS, "missing screen id");
    }
    ScreenCtx ctx;
    memset(&ctx, 0, sizeof(ctx));
    strlcpy(ctx.screen, screen, sizeof(ctx.screen));

    return mcp_run_control(exec_set_screen, &ctx, sizeof(ctx),
                           TOOL_CONTROL_TIMEOUT_MS, result, err);
}

// --- set_backlight ---------------------------------------------------------
struct BacklightCtx { uint8_t brightness; };

static void exec_set_backlight(const void* ctx, bool* ok, char* msg, size_t msg_len) {
    const BacklightCtx* c = (const BacklightCtx*)ctx;
    display_manager_set_backlight_brightness(c->brightness);
    screen_saver_manager_notify_activity(true);
    *ok = true;
    snprintf(msg, msg_len, "brightness set to %u%%", (unsigned)c->brightness);
}

static bool tool_set_backlight(const JsonObject& args, JsonObject& result, String& err) {
    if (!args.containsKey("brightness")) {
        return tool_fail(result, err, TOOL_ERR_PARAMS, "missing brightness (0-100)");
    }
    int b = args["brightness"] | -1;
    if (b < 0 || b > 100) {
        return tool_fail(result, err, TOOL_ERR_PARAMS, "brightness must be 0-100");
    }
    BacklightCtx ctx;
    ctx.brightness = (uint8_t)b;

    return mcp_run_control(exec_set_backlight, &ctx, sizeof(ctx),
                           TOOL_CONTROL_TIMEOUT_MS, result, err);
}

// --- wake ------------------------------------------------------------------
static void exec_wake(const void* ctx, bool* ok, char* msg, size_t msg_len) {
    (void)ctx;
    screen_saver_manager_notify_activity(true);
    *ok = true;
    strlcpy(msg, "awake", msg_len);
}

static bool tool_wake(const JsonObject& args, JsonObject& result, String& err) {
    (void)args;
    return mcp_run_control(exec_wake, nullptr, 0,
                           TOOL_CONTROL_TIMEOUT_MS, result, err);
}

#endif // HAS_DISPLAY

// --- system_command --------------------------------------------------------
struct SysCtx { char command[CONFIG_ACTION_TYPE_MAX_LEN]; };

static void exec_system_command(const void* ctx, bool* ok, char* msg, size_t msg_len) {
    const SysCtx* c = (const SysCtx*)ctx;

    if (strcmp(c->command, "reboot") == 0) {
        // Defer the restart so the JSON-RPC response flushes first.
        mcp_request_reboot();
        *ok = true;
        strlcpy(msg, "reboot scheduled", msg_len);
        return;
    }

#if HAS_DISPLAY || HAS_BUTTON
    // Forward through the shared System Command path (DRY) for the safe
    // commands; running on the main loop makes LVGL/I/O safe.
    ButtonAction act;
    memset(&act, 0, sizeof(act));
    strlcpy(act.type, ACTION_TYPE_SYSTEM, sizeof(act.type));
    strlcpy(act.payload.system.system_command, c->command, sizeof(act.payload.system.system_command));
    action_dispatch(act, "MCP");
    *ok = true;
    strlcpy(msg, "dispatched", msg_len);
#else
    if (strcmp(c->command, "wifi_reconnect") == 0) {
        wifi_manager_request_reconnect();
        *ok = true;
        strlcpy(msg, "dispatched", msg_len);
    } else {
        *ok = false;
        strlcpy(msg, "command unavailable on this board", msg_len);
    }
#endif
}

static bool tool_system_command(const JsonObject& args, JsonObject& result, String& err) {
    const char* cmd = args["command"] | (const char*)nullptr;
    if (!cmd || !cmd[0]) {
        return tool_fail(result, err, TOOL_ERR_PARAMS, "missing command");
    }
    if (strcmp(cmd, "reboot") != 0 &&
        strcmp(cmd, "wifi_reconnect") != 0 &&
        strcmp(cmd, "screensaver") != 0) {
        return tool_fail(result, err, TOOL_ERR_PARAMS, "command must be reboot, wifi_reconnect, or screensaver");
    }

    SysCtx ctx;
    memset(&ctx, 0, sizeof(ctx));
    strlcpy(ctx.command, cmd, sizeof(ctx.command));

    return mcp_run_control(exec_system_command, &ctx, sizeof(ctx),
                           TOOL_CONTROL_TIMEOUT_MS, result, err);
}

// ============================================================================
// Tool descriptors + registration
// ============================================================================

static const McpTool s_tool_get_device_status = {
    "get_device_status",
    "Get firmware version, board/device-class, uptime, current screen, and WiFi state. Good first call to orient yourself (identity + what kind of device this is) before using other tools.",
    "{\"type\":\"object\",\"properties\":{}}",
    tool_get_device_status, true, false, false
};
REGISTER_MCP_TOOL(s_tool_get_device_status);

static const McpTool s_tool_get_health = {
    "get_health",
    "Get device health: heap (internal/PSRAM), CPU usage/temperature, and WiFi RSSI.",
    "{\"type\":\"object\",\"properties\":{}}",
    tool_get_health, true, false, false
};
REGISTER_MCP_TOOL(s_tool_get_health);

static const McpTool s_tool_get_sensors = {
    "get_sensors",
    "Get current sensor readings (e.g. temperature, humidity, presence). Empty on boards without sensors.",
    "{\"type\":\"object\",\"properties\":{}}",
    tool_get_sensors, true, false, false
};
REGISTER_MCP_TOOL(s_tool_get_sensors);

#if HAS_DISPLAY

static const McpTool s_tool_list_screens = {
    "list_screens",
    "List all available screens (id and name) and report the active one.",
    "{\"type\":\"object\",\"properties\":{}}",
    tool_list_screens, true, false, false
};
REGISTER_MCP_TOOL(s_tool_list_screens);

static const McpTool s_tool_get_current_screen = {
    "get_current_screen",
    "Report the currently active screen id.",
    "{\"type\":\"object\",\"properties\":{}}",
    tool_get_current_screen, true, false, false
};
REGISTER_MCP_TOOL(s_tool_get_current_screen);

static const McpTool s_tool_list_pads = {
    "list_pads",
    "List configured pads with their friendly name and buttons (position, label, action type). Use this to discover what can be pressed, then get_pad for one pad's full detail or press_button to activate a button. Optional 'screen' filter (e.g. 'pad_0').",
    "{\"type\":\"object\",\"properties\":{\"screen\":{\"type\":\"string\",\"description\":\"Pad id like 'pad_0' to limit output\"}}}",
    tool_list_pads, true, false, false
};
REGISTER_MCP_TOOL(s_tool_list_pads);

static const McpTool s_tool_get_pad = {
    "get_pad",
    "Get one pad's full configuration: layout/cols/rows, wake_screen, bg_color, template_pad, named bindings, and every button's labels, styles, colors, widget, and resolved tap/long-press actions with targets. Each button's 'position' is the 0-based array index that press_button/set_button use. Inspect a button here to see what it does before pressing it. Args: screen (pad id 'pad_0' or the pad's friendly name).",
    "{\"type\":\"object\",\"properties\":{\"screen\":{\"type\":\"string\"}},\"required\":[\"screen\"]}",
    tool_get_pad, true, false, false
};
REGISTER_MCP_TOOL(s_tool_get_pad);

static const McpTool s_tool_press_button = {
    "press_button",
    "Activate a pad button, running its tap action(s) exactly as a physical tap would. The action is REAL and may have side effects (publish MQTT, call Home Assistant, send BLE keystrokes, navigate screens, or run a system command like reboot) — inspect an unfamiliar button with get_pad first. Discover buttons via list_pads/get_pad; 'position' is the 0-based index in the pad's button array, NOT a grid cell. Args: screen (pad id like 'pad_0') plus either position or label (case-insensitive).",
    "{\"type\":\"object\",\"properties\":{\"screen\":{\"type\":\"string\"},\"position\":{\"type\":\"integer\"},\"label\":{\"type\":\"string\"}},\"required\":[\"screen\"]}",
    tool_press_button, false, false, true
};
REGISTER_MCP_TOOL(s_tool_press_button);

static const McpTool s_tool_set_screen = {
    "set_screen",
    "Navigate the display to a screen by id (e.g. 'pad_1', 'info'). Call list_screens for valid ids (pads are 'pad_N'); get_current_screen confirms the active one afterward.",
    "{\"type\":\"object\",\"properties\":{\"screen\":{\"type\":\"string\"}},\"required\":[\"screen\"]}",
    tool_set_screen, false, false, true
};
REGISTER_MCP_TOOL(s_tool_set_screen);

static const McpTool s_tool_set_backlight = {
    "set_backlight",
    "Set display backlight brightness (0-100%).",
    "{\"type\":\"object\",\"properties\":{\"brightness\":{\"type\":\"integer\",\"minimum\":0,\"maximum\":100}},\"required\":[\"brightness\"]}",
    tool_set_backlight, false, false, true
};
REGISTER_MCP_TOOL(s_tool_set_backlight);

static const McpTool s_tool_wake = {
    "wake",
    "Wake the display (cancel the screen saver).",
    "{\"type\":\"object\",\"properties\":{}}",
    tool_wake, false, false, true
};
REGISTER_MCP_TOOL(s_tool_wake);

#endif // HAS_DISPLAY

static const McpTool s_tool_system_command = {
    "system_command",
    "Run a system command: 'reboot' (destructive — restarts the device and DROPS this connection/MCP session), 'wifi_reconnect' (re-establish the WiFi link), or 'screensaver' (trigger the display screen saver).",
    "{\"type\":\"object\",\"properties\":{\"command\":{\"type\":\"string\",\"enum\":[\"reboot\",\"wifi_reconnect\",\"screensaver\"]}},\"required\":[\"command\"]}",
    tool_system_command, false, true, true
};
REGISTER_MCP_TOOL(s_tool_system_command);

#endif // HAS_MCP
