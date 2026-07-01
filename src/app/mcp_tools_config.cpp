// ============================================================================
// mcp_tools_config.cpp — universal (board-agnostic) MCP tools for device
// settings, notifications, volume, timers, and auxiliary component config.
//
// Compiled directly (sketch root), like mcp_tools_core.cpp. These tools round
// out the core surface so an assistant can read/adjust the same knobs a user
// sets in the web portal, not just pads:
//
//   get_config            — read the (non-secret) device settings.            [read]
//   get_component_config  — read a feature's saved config JSON.               [read]
//   notify                — show a message bubble on the display.             [control]
//   set_volume            — set/adjust the speaker volume.                    [control]
//   timer_control         — start/stop/reset/set the on-screen timers.        [control]
//   set_config            — write a curated, safe subset of device settings.  [control]
//
// Threading: read tools run on the web task (config lives in RAM; component
// config is a small flash read, same as the pad read tools). Control tools NEVER
// touch action_dispatch / LVGL / NVS on the web task — they defer to the main
// loop via mcp_control_dispatch() (web_mcp.h), exactly like the core control
// tools. Secrets (passwords, tokens) are never returned or accepted here; those
// stay in the web portal.
// ============================================================================

#include "mcp_tool_registry.h"
#include "mcp_tool_util.h"
#include "web_mcp.h"

#include "board_config.h"

#if HAS_MCP

#include "config_manager.h"
#include "log_manager.h"
#include "web_portal_json.h"    // make_psram_json_doc
#include "web_portal_state.h"   // web_portal_get_current_config

#include <ArduinoJson.h>
#include <esp_heap_caps.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Building a ButtonAction to reuse the shared action dispatch path (DRY with the
// pad editor / physical taps). ACTION_TYPE_* and ButtonAction are always defined.
#include "pad_config.h"

#if HAS_DISPLAY || HAS_BUTTON
#include "action_dispatch.h"
#endif

#if HAS_DISPLAY
#include "display_manager.h"
#include "screen_saver_manager.h"
#include "timer_engine.h"       // TIMER_COUNT, timer_* control
#include "timer_config.h"       // timer_config_save_raw
#include "swipe_config.h"       // swipe_config_save_raw
#include "boot_actions.h"       // boot_actions_save_raw
#include "button_defaults.h"    // button_defaults_save_raw
#endif

#if HAS_BUTTON
#include "hw_button_config.h"   // hw_button_config_save_raw, NUM_HW_BUTTONS
#endif

#if HAS_MQTT
#include "mqtt_triggers.h"      // mqtt_triggers_save_raw, MQTT_TRIGGERS_ENABLED, MAX_MQTT_TRIGGERS
#endif

#if HAS_AUDIO
#include "audio.h"
#endif

#if HAS_DISPLAY || HAS_BUTTON || HAS_MQTT
#include "storage.h"
#endif

// JSON-RPC error codes (canonical values in mcp_tool_registry.h).
static constexpr int CFG_ERR_PARAMS   = MCP_RPC_ERR_PARAMS;
static constexpr int CFG_ERR_INTERNAL = MCP_RPC_ERR_INTERNAL;
static constexpr int CFG_ERR_BUSY     = MCP_RPC_ERR_CONTROL_BUSY;

static constexpr uint32_t CFG_CONTROL_TIMEOUT_MS = 2000;
static constexpr uint32_t CFG_WRITE_TIMEOUT_MS   = 4000;

// Set a tool error code + message and return false (thin adapter over the shared
// mcp_tool_fail in mcp_tool_util.h).
static bool cfg_fail(JsonObject& result, String& err, int code, const char* msg) {
    return mcp_tool_fail(result, err, code, msg);
}

// ============================================================================
// get_config — read the current (non-secret) device settings
// ============================================================================
// Mirrors the web portal's Setup/Display/Audio pages. Secrets are never
// returned as plaintext — instead a "<field>_set" boolean reports whether a
// value is stored, so an assistant can see what is configured without leaking
// credentials.
static bool tool_get_config(const JsonObject& args, JsonObject& result, String& err) {
    (void)args;
    DeviceConfig* c = web_portal_get_current_config();
    if (!c) return cfg_fail(result, err, CFG_ERR_INTERNAL, "config not initialized");

    // Identity + network (SSID is not secret; the password is).
    result["device_name"]        = c->device_name;
    result["wifi_ssid"]          = c->wifi_ssid;
    result["wifi_password_set"]  = (bool)(c->wifi_password[0] != '\0');
    result["fixed_ip"]           = c->fixed_ip;
    result["subnet_mask"]        = c->subnet_mask;
    result["gateway"]            = c->gateway;
    result["dns1"]               = c->dns1;
    result["dns2"]               = c->dns2;

    // MQTT / Home Assistant (host/user/url visible; password/token redacted).
    result["mqtt_host"]              = c->mqtt_host;
    result["mqtt_port"]              = c->mqtt_port;
    result["mqtt_username"]          = c->mqtt_username;
    result["mqtt_password_set"]      = (bool)(c->mqtt_password[0] != '\0');
    result["mqtt_publish_scope"]     = c->mqtt_publish_scope;
    result["ha_url"]                 = c->ha_url;
    result["ha_token_set"]           = (bool)(c->ha_token[0] != '\0');

    // Power / transport.
    result["operating_mode"]                 = c->operating_mode;
    result["duty_cycle_wake_seconds"]        = c->duty_cycle_wake_seconds;
    result["mqtt_publish_interval_seconds"]  = c->mqtt_publish_interval_seconds;
    result["portal_idle_timeout_seconds"]    = c->portal_idle_timeout_seconds;
    result["wifi_backoff_max_seconds"]       = c->wifi_backoff_max_seconds;
#if HAS_BLE
    result["ble_burst_count"]    = c->ble_burst_count;
    result["ble_adv_interval_ms"] = c->ble_adv_interval_ms;
#endif

    // Display.
    result["backlight_brightness"] = c->backlight_brightness;
#if HAS_DISPLAY
    result["screen_saver_enabled"]          = c->screen_saver_enabled;
    result["screen_saver_timeout_seconds"]  = c->screen_saver_timeout_seconds;
    result["screen_saver_fade_out_ms"]      = c->screen_saver_fade_out_ms;
    result["screen_saver_fade_in_ms"]       = c->screen_saver_fade_in_ms;
    result["screen_saver_wake_on_touch"]    = c->screen_saver_wake_on_touch;
    result["screen_saver_wake_binding"]     = c->screen_saver_wake_binding;
#endif

    // Audio.
#if HAS_AUDIO
    result["audio_volume"] = c->audio_volume;
    result["tap_beep"]     = c->tap_beep;
    result["lp_beep"]      = c->lp_beep;
#endif

    // Security (never returns the actual secrets).
    result["basic_auth_enabled"]     = c->basic_auth_enabled;
    result["basic_auth_username"]    = c->basic_auth_username;
    result["basic_auth_password_set"] = (bool)(c->basic_auth_password[0] != '\0');
#if HAS_MCP
    result["mcp_enabled"]            = c->mcp_enabled;
    result["mcp_control_enabled"]    = c->mcp_control_enabled;
    result["mcp_authoring_enabled"]  = c->mcp_authoring_enabled;
    result["mcp_token_set"]          = (bool)(c->mcp_token[0] != '\0');
#endif
#if HAS_BLE_HID
    result["ble_enabled"] = c->ble_enabled;
#endif

    result["writable_note"] =
        "set_config writes only a safe subset: device_name, backlight_brightness, "
        "the screen_saver_* group, mqtt_publish_interval_seconds, mqtt_publish_scope, "
        "audio_volume. Credentials, WiFi, operating mode, and security toggles are "
        "read-only here and must be changed in the web portal.";
    return true;
}

// ============================================================================
// Auxiliary component config — shared table (read + write)
// ============================================================================
// These features persist raw JSON to the Storage facade (LittleFS/SD). One table
// is the single source of truth for both get_component_config (read the saved
// JSON verbatim) and set_component_config (validate + persist). Each entry is
// board-accurate: only components compiled into this firmware appear, so the
// tools never advertise or accept a component the board cannot honor.
#if HAS_DISPLAY || HAS_BUTTON || MQTT_TRIGGERS_ENABLED

typedef bool (*CompSaveFn)(const uint8_t*, size_t);

struct CompDef {
    const char* name;
    const char* path;
    CompSaveFn  save;      // persists validated JSON (runs on the main loop)
    size_t      max_bytes; // reject writes larger than the loader accepts
};

static const CompDef s_comps[] = {
#if HAS_DISPLAY
    { "timers",          "/config/timers.json",         timer_config_save_raw,    4096 },
    { "swipe",           "/config/swipe_actions.json",  swipe_config_save_raw,    4096 },
    { "boot",            "/config/boot_actions.json",   boot_actions_save_raw,    4096 },
    { "button-defaults", "/config/button_defaults.json", button_defaults_save_raw, 4096 },
#endif
#if HAS_BUTTON
    { "hw-buttons",      "/config/hw_buttons.json",     hw_button_config_save_raw, 4096 },
#endif
#if MQTT_TRIGGERS_ENABLED
    { "mqtt-triggers",   "/config/mqtt_triggers.json",  mqtt_triggers_save_raw,   MQTT_TRIGGERS_JSON_CAP },
#endif
};
static constexpr size_t COMP_COUNT = sizeof(s_comps) / sizeof(s_comps[0]);

static const CompDef* comp_find(const char* name) {
    if (!name || !name[0]) return nullptr;
    for (size_t i = 0; i < COMP_COUNT; ++i) {
        if (strcmp(name, s_comps[i].name) == 0) return &s_comps[i];
    }
    return nullptr;
}

// --- get_component_config --------------------------------------------------
static bool tool_get_component_config(const JsonObject& args, JsonObject& result, String& err) {
    const char* comp = args["component"] | (const char*)nullptr;
    const CompDef* entry = comp_find(comp);
    if (!comp || !comp[0]) return cfg_fail(result, err, CFG_ERR_PARAMS, "missing 'component'");
    if (!entry) return cfg_fail(result, err, CFG_ERR_PARAMS,
                                "unknown component (see the 'component' enum in this tool's schema)");

    result["component"] = entry->name;

    if (!Storage.exists(entry->path)) {
        // Not an error: the feature simply uses its firmware defaults until saved.
        result["exists"] = false;
        result["note"]   = "not configured — firmware defaults are in effect";
        result.createNestedObject("config");
        return true;
    }

    File f = Storage.open(entry->path, "r");
    if (!f) return cfg_fail(result, err, CFG_ERR_INTERNAL, "failed to open config file");
    size_t sz = f.size();
    if (sz == 0 || sz > 16 * 1024) {
        f.close();
        return cfg_fail(result, err, CFG_ERR_INTERNAL, "config file empty or too large");
    }

    auto doc = make_psram_json_doc(sz * 2 + 512);
    if (!doc || doc->capacity() == 0) { f.close(); return cfg_fail(result, err, CFG_ERR_INTERNAL, "out of memory"); }
    DeserializationError perr = deserializeJson(*doc, f);
    f.close();
    if (perr) return cfg_fail(result, err, CFG_ERR_INTERNAL, "stored config is not valid JSON");

    result["exists"] = true;
    // Deep-copies the parsed tree into the (24KB) result document.
    result["config"] = doc->as<JsonVariantConst>();
    return true;
}

// --- set_component_config --------------------------------------------------
// Structural validation the raw save paths lack. It does not re-implement the
// binding engine (the defensive loaders tolerate unknown bindings), but it
// rejects the shape mistakes that would silently corrupt a feature: wrong root
// type, over-long action lists, out-of-range counts, and missing required keys.

// Validate an actions array: <= MAX_BUTTON_ACTIONS entries, each an object with
// a non-empty "type" (mirrors the pad tools' validate_action_array). nullptr = ok.
static const char* val_action_list(JsonVariantConst v) {
    if (v.isNull()) return nullptr;
    if (!v.is<JsonArrayConst>()) return "actions must be an array";
    JsonArrayConst a = v.as<JsonArrayConst>();
    if (a.size() > MAX_BUTTON_ACTIONS) return "too many actions (max 3 per list)";
    for (JsonVariantConst e : a) {
        if (!e.is<JsonObjectConst>()) return "each action must be an object";
        const char* t = e["type"] | "";
        if (!t[0]) return "action missing 'type'";
    }
    return nullptr;
}

// Per-component structural validator. Returns nullptr when ok, else a message.
static const char* validate_component(const char* name, JsonObjectConst cfg) {
#if HAS_DISPLAY
    if (strcmp(name, "timers") == 0) {
        for (JsonPairConst kv : cfg) {
            char* endp = nullptr;
            long id = strtol(kv.key().c_str(), &endp, 10);
            if (endp == kv.key().c_str() || *endp || id < 1 || id > TIMER_COUNT) return "timer keys must be \"1\"..\"3\"";
            if (!kv.value().is<JsonObjectConst>()) return "each timer must be an object";
            JsonObjectConst t = kv.value().as<JsonObjectConst>();
            const char* mode = t["mode"] | "";
            if (mode[0] && strcmp(mode, "up") != 0 && strcmp(mode, "down") != 0) return "timer mode must be 'up' or 'down'";
            if (t.containsKey("countdown") && (t["countdown"] | -1) < 0) return "timer countdown must be >= 0";
            const char* ae = val_action_list(t["expire_actions"]);
            if (ae) return ae;
        }
        return nullptr;
    }
    if (strcmp(name, "swipe") == 0) {
        for (JsonPairConst kv : cfg) {
            const char* k = kv.key().c_str();
            if (strcmp(k, "swipe_left") && strcmp(k, "swipe_right") &&
                strcmp(k, "swipe_up") && strcmp(k, "swipe_down"))
                return "swipe keys must be swipe_left/right/up/down";
            if (!kv.value().is<JsonObjectConst>()) return "each swipe must be an action object";
        }
        return nullptr;
    }
    if (strcmp(name, "boot") == 0) {
        return val_action_list(cfg["actions"]);
    }
    if (strcmp(name, "button-defaults") == 0) {
        return nullptr;  // all fields optional appearance strings; loader is defensive
    }
#endif
#if HAS_BUTTON
    if (strcmp(name, "hw-buttons") == 0) {
        JsonVariantConst bv = cfg["buttons"];
        if (bv.isNull()) return nullptr;
        if (!bv.is<JsonArrayConst>()) return "'buttons' must be an array";
        JsonArrayConst arr = bv.as<JsonArrayConst>();
        if (arr.size() > NUM_HW_BUTTONS) return "too many buttons for this board";
        for (JsonVariantConst e : arr) {
            if (!e.is<JsonObjectConst>()) return "each button must be an object";
            JsonObjectConst b = e.as<JsonObjectConst>();
            const char* ae;
            if ((ae = val_action_list(b["tap_actions"]))) return ae;
            if ((ae = val_action_list(b["hold_actions"]))) return ae;
        }
        return nullptr;
    }
#endif
#if MQTT_TRIGGERS_ENABLED
    if (strcmp(name, "mqtt-triggers") == 0) {
        JsonVariantConst tv = cfg["triggers"];
        if (tv.isNull()) return nullptr;
        if (!tv.is<JsonArrayConst>()) return "'triggers' must be an array";
        JsonArrayConst arr = tv.as<JsonArrayConst>();
        if (arr.size() > MAX_MQTT_TRIGGERS) return "too many triggers for this board";
        for (JsonVariantConst e : arr) {
            if (!e.is<JsonObjectConst>()) return "each trigger must be an object";
            JsonObjectConst t = e.as<JsonObjectConst>();
            const char* topic = t["topic"] | "";
            if (!topic[0]) return "trigger missing 'topic'";
            if (strchr(topic, '#') || strchr(topic, '+')) return "trigger topic must not contain wildcards (#, +)";
            const char* ae = val_action_list(t["actions"]);
            if (ae) return ae;
        }
        return nullptr;
    }
#endif
    (void)cfg;
    return "component not writable on this board";
}

// Deferred save: ptr+len in ctx; the main loop persists + frees (D6 pattern).
struct CompWriteCtx { CompSaveFn save; uint8_t* buf; size_t len; };

static void exec_comp_save(const void* ctx, bool* ok, char* msg, size_t msg_len) {
    const CompWriteCtx* c = (const CompWriteCtx*)ctx;
    *ok = false;
    if (!c->buf || !c->save) { strlcpy(msg, "no buffer", msg_len); return; }
    bool saved = c->save(c->buf, c->len);
    heap_caps_free(c->buf);
    if (!saved) { strlcpy(msg, "save failed", msg_len); return; }
    *ok = true;
    strlcpy(msg, "saved", msg_len);
}

static bool tool_set_component_config(const JsonObject& args, JsonObject& result, String& err) {
    const char* comp = args["component"] | (const char*)nullptr;
    const CompDef* entry = comp_find(comp);
    if (!comp || !comp[0]) return cfg_fail(result, err, CFG_ERR_PARAMS, "missing 'component'");
    if (!entry) return cfg_fail(result, err, CFG_ERR_PARAMS,
                                "unknown component (see the 'component' enum in this tool's schema)");
    if (!entry->save) return cfg_fail(result, err, CFG_ERR_PARAMS, "component is not writable on this board");

    JsonVariantConst cv = args["config"];
    if (!cv.is<JsonObjectConst>()) {
        return cfg_fail(result, err, CFG_ERR_PARAMS, "'config' must be a JSON object (the full replacement config)");
    }
    JsonObjectConst cfg = cv.as<JsonObjectConst>();

    const char* verr = validate_component(entry->name, cfg);
    if (verr) return cfg_fail(result, err, CFG_ERR_PARAMS, verr);

    size_t need = measureJson(cfg) + 1;
    if (need > entry->max_bytes) {
        return cfg_fail(result, err, CFG_ERR_PARAMS, "config too large for this component");
    }
    uint8_t* buf = (uint8_t*)mcp_psram_alloc(need);
    if (!buf) return cfg_fail(result, err, CFG_ERR_INTERNAL, "out of memory");
    size_t len = serializeJson(cfg, buf, need);

    result["component"] = entry->name;

    CompWriteCtx ctx; ctx.save = entry->save; ctx.buf = buf; ctx.len = len;
    bool ok = false; char msg[MCP_TOOL_MSG_LEN] = {0};
    McpControlResult r = mcp_control_dispatch(exec_comp_save, &ctx, sizeof(ctx),
                                              CFG_WRITE_TIMEOUT_MS, &ok, msg, sizeof(msg));
    if (r == MCP_CONTROL_BUSY) { heap_caps_free(buf); return cfg_fail(result, err, CFG_ERR_BUSY, "busy, retry"); }
    // On TIMEOUT the deferred job may still run and free buf, so do not free here.
    return mcp_finish_control(r, ok, msg, result, err);
}

#endif // HAS_DISPLAY || HAS_BUTTON || MQTT_TRIGGERS_ENABLED

// ============================================================================
// notify — show a message bubble on the display
// ============================================================================
#if HAS_DISPLAY

struct NotifyCtx {
    char text[128];
    char duration[12];       // ms; "" = default (3000), "0" = persistent
    char location[8];        // "top" | "center" | "bottom"
    char text_color[10];     // #RRGGBB
    char bg_color[10];
    char border_color[10];
    uint8_t opacity;         // 0 = default (85), else 1-100
    uint8_t font_size;       // 0 = auto
};

static void exec_notify(const void* ctx, bool* ok, char* msg, size_t msg_len) {
    const NotifyCtx* c = (const NotifyCtx*)ctx;
    ButtonAction act;
    memset(&act, 0, sizeof(act));
    strlcpy(act.type, ACTION_TYPE_NOTIFY, sizeof(act.type));
    NotifyPayload& n = act.payload.notify;
    strlcpy(n.notify_text,         c->text,         sizeof(n.notify_text));
    strlcpy(n.notify_duration_ms,  c->duration,     sizeof(n.notify_duration_ms));
    strlcpy(n.notify_text_color,   c->text_color,   sizeof(n.notify_text_color));
    strlcpy(n.notify_bg_color,     c->bg_color,     sizeof(n.notify_bg_color));
    strlcpy(n.notify_border_color, c->border_color, sizeof(n.notify_border_color));
    strlcpy(n.notify_location,     c->location,     sizeof(n.notify_location));
    n.notify_opacity   = c->opacity;
    n.notify_font_size = c->font_size;
    action_dispatch(act, "MCP");
    *ok = true;
    strlcpy(msg, c->text[0] ? "shown" : "dismissed", msg_len);
}

static bool tool_notify(const JsonObject& args, JsonObject& result, String& err) {
    if (!args.containsKey("text")) {
        return cfg_fail(result, err, CFG_ERR_PARAMS, "missing 'text' (empty string dismisses the bubble)");
    }
    NotifyCtx ctx;
    memset(&ctx, 0, sizeof(ctx));
    strlcpy(ctx.text, args["text"] | "", sizeof(ctx.text));

    if (args.containsKey("duration_ms")) {
        long d = args["duration_ms"] | -1;
        if (d < 0) return cfg_fail(result, err, CFG_ERR_PARAMS, "duration_ms must be >= 0 (0 = persistent)");
        snprintf(ctx.duration, sizeof(ctx.duration), "%ld", d);
    }
    strlcpy(ctx.location,     args["location"]     | "", sizeof(ctx.location));
    strlcpy(ctx.text_color,   args["text_color"]   | "", sizeof(ctx.text_color));
    strlcpy(ctx.bg_color,     args["bg_color"]     | "", sizeof(ctx.bg_color));
    strlcpy(ctx.border_color, args["border_color"] | "", sizeof(ctx.border_color));
    int op = args["opacity"]   | 0; if (op < 0) op = 0; if (op > 100) op = 100;
    int fs = args["font_size"] | 0; if (fs < 0) fs = 0; if (fs > 96)  fs = 96;
    ctx.opacity   = (uint8_t)op;
    ctx.font_size = (uint8_t)fs;

    return mcp_run_control(exec_notify, &ctx, sizeof(ctx),
                           CFG_CONTROL_TIMEOUT_MS, result, err);
}

#endif // HAS_DISPLAY

// ============================================================================
// set_volume — set or adjust the speaker volume
// ============================================================================
#if HAS_AUDIO && (HAS_DISPLAY || HAS_BUTTON)

struct VolumeCtx {
    char mode[8];                    // "set" | "adjust"
    char value[CONFIG_VALUE_MAX_LEN];
};

static void exec_set_volume(const void* ctx, bool* ok, char* msg, size_t msg_len) {
    const VolumeCtx* c = (const VolumeCtx*)ctx;
    ButtonAction act;
    memset(&act, 0, sizeof(act));
    strlcpy(act.type, ACTION_TYPE_VOLUME, sizeof(act.type));
    strlcpy(act.payload.volume.volume_mode,  c->mode,  sizeof(act.payload.volume.volume_mode));
    strlcpy(act.payload.volume.volume_value, c->value, sizeof(act.payload.volume.volume_value));
    action_dispatch(act, "MCP");
    *ok = true;
    snprintf(msg, msg_len, "volume %s %s -> %u%%", c->mode, c->value, (unsigned)audio_get_volume());
}

static bool tool_set_volume(const JsonObject& args, JsonObject& result, String& err) {
    const char* mode = args["mode"] | "set";
    if (strcmp(mode, "set") != 0 && strcmp(mode, "adjust") != 0) {
        return cfg_fail(result, err, CFG_ERR_PARAMS, "mode must be 'set' or 'adjust'");
    }
    if (!args.containsKey("value")) {
        return cfg_fail(result, err, CFG_ERR_PARAMS, "missing 'value' (set: 0-100, adjust: signed delta)");
    }
    VolumeCtx ctx;
    memset(&ctx, 0, sizeof(ctx));
    strlcpy(ctx.mode, mode, sizeof(ctx.mode));
    // Accept a number or a numeric string; the shared volume action clamps/steps.
    if (args["value"].is<const char*>()) {
        strlcpy(ctx.value, args["value"] | "0", sizeof(ctx.value));
    } else {
        snprintf(ctx.value, sizeof(ctx.value), "%ld", (long)(args["value"] | 0));
    }

    return mcp_run_control(exec_set_volume, &ctx, sizeof(ctx),
                           CFG_CONTROL_TIMEOUT_MS, result, err);
}

#endif // HAS_AUDIO && (HAS_DISPLAY || HAS_BUTTON)

// ============================================================================
// timer_control — start/stop/reset/set the on-screen timers (1..3)
// ============================================================================
#if HAS_DISPLAY

struct TimerCtx {
    uint8_t id;
    char command[CONFIG_TIMER_CMD_MAX_LEN];
    char value[CONFIG_VALUE_MAX_LEN];   // seconds for set/adjust
};

static void exec_timer_control(const void* ctx, bool* ok, char* msg, size_t msg_len) {
    const TimerCtx* c = (const TimerCtx*)ctx;
    ButtonAction act;
    memset(&act, 0, sizeof(act));
    strlcpy(act.type, ACTION_TYPE_TIMER, sizeof(act.type));
    act.payload.timer.timer_id = c->id;
    strlcpy(act.payload.timer.timer_command, c->command, sizeof(act.payload.timer.timer_command));
    strlcpy(act.payload.timer.timer_value,   c->value,   sizeof(act.payload.timer.timer_value));
    action_dispatch(act, "MCP");
    *ok = true;
    snprintf(msg, msg_len, "timer %u %s", (unsigned)c->id, c->command);
}

static bool tool_timer_control(const JsonObject& args, JsonObject& result, String& err) {
    if (!args.containsKey("timer_id")) {
        return cfg_fail(result, err, CFG_ERR_PARAMS, "missing 'timer_id' (1-3)");
    }
    int id = args["timer_id"] | 0;
    if (id < 1 || id > TIMER_COUNT) {
        return cfg_fail(result, err, CFG_ERR_PARAMS, "timer_id out of range (1-3)");
    }
    const char* cmd = args["command"] | (const char*)nullptr;
    if (!cmd || !cmd[0]) {
        return cfg_fail(result, err, CFG_ERR_PARAMS, "missing 'command'");
    }
    // Whitelist matches the shared timer action dispatch.
    static const char* kCmds[] = { "start","stop","toggle","pause","resume","reset","lap","adjust","set" };
    bool known = false;
    for (const char* k : kCmds) if (strcmp(cmd, k) == 0) { known = true; break; }
    if (!known) {
        return cfg_fail(result, err, CFG_ERR_PARAMS,
                        "command must be start|stop|toggle|pause|resume|reset|lap|adjust|set");
    }
    const bool needs_value = (strcmp(cmd, "set") == 0 || strcmp(cmd, "adjust") == 0);
    if (needs_value && !args.containsKey("value")) {
        return cfg_fail(result, err, CFG_ERR_PARAMS, "'set'/'adjust' require 'value' (seconds)");
    }

    TimerCtx ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.id = (uint8_t)id;
    strlcpy(ctx.command, cmd, sizeof(ctx.command));
    if (args.containsKey("value")) {
        if (args["value"].is<const char*>()) {
            strlcpy(ctx.value, args["value"] | "0", sizeof(ctx.value));
        } else {
            snprintf(ctx.value, sizeof(ctx.value), "%ld", (long)(args["value"] | 0));
        }
    }

    return mcp_run_control(exec_timer_control, &ctx, sizeof(ctx),
                           CFG_CONTROL_TIMEOUT_MS, result, err);
}

#endif // HAS_DISPLAY

// ============================================================================
// set_config — write a curated, safe subset of device settings
// ============================================================================
// Only fields that are safe to change live (no reboot, no dropped MCP session)
// are exposed. WiFi/MQTT/HA credentials, operating mode, and security toggles
// are intentionally NOT writable here — those stay in the web portal because
// they can disconnect the transport carrying this request or lock the assistant
// out. device_name persists immediately but its mDNS/hostname only refreshes on
// the next reboot.
//
// The request is parsed on the web task into a heap struct (wake_binding alone
// is 192 B, so it does not fit the 256 B control slot) and applied on the main
// loop: screen-saver fields take effect live because the screen saver reads the
// same DeviceConfig instance; brightness/volume are applied through their
// managers; then the whole config is persisted to NVS.
struct SetConfigReq {
    bool has_device_name;   char device_name[CONFIG_DEVICE_NAME_MAX_LEN];
    bool has_brightness;    uint8_t brightness;
    bool has_pub_interval;  uint16_t pub_interval;
    bool has_pub_scope;     char pub_scope[CONFIG_MQTT_SCOPE_MAX_LEN];
#if HAS_AUDIO
    bool has_volume;        uint8_t volume;
#endif
#if HAS_DISPLAY
    bool has_ss_enabled;    bool ss_enabled;
    bool has_ss_timeout;    uint16_t ss_timeout;
    bool has_ss_fade_out;   uint16_t ss_fade_out;
    bool has_ss_fade_in;    uint16_t ss_fade_in;
    bool has_ss_wake_touch; bool ss_wake_touch;
    bool has_ss_wake_bind;  char ss_wake_bind[CONFIG_SS_WAKE_BINDING_MAX_LEN];
#endif
};

struct SetConfigCtx { SetConfigReq* req; };

static void exec_set_config(const void* ctx, bool* ok, char* msg, size_t msg_len) {
    const SetConfigCtx* c = (const SetConfigCtx*)ctx;
    SetConfigReq* q = c->req;
    *ok = false;
    if (!q) { strlcpy(msg, "no request", msg_len); return; }

    DeviceConfig* cfg = web_portal_get_current_config();
    if (!cfg) { heap_caps_free(q); strlcpy(msg, "config not initialized", msg_len); return; }

    if (q->has_device_name) strlcpy(cfg->device_name, q->device_name, CONFIG_DEVICE_NAME_MAX_LEN);
    if (q->has_pub_interval) cfg->mqtt_publish_interval_seconds = q->pub_interval;
    if (q->has_pub_scope)    strlcpy(cfg->mqtt_publish_scope, q->pub_scope, CONFIG_MQTT_SCOPE_MAX_LEN);
#if HAS_DISPLAY
    if (q->has_ss_enabled)    cfg->screen_saver_enabled = q->ss_enabled;
    if (q->has_ss_timeout)    cfg->screen_saver_timeout_seconds = q->ss_timeout;
    if (q->has_ss_fade_out)   cfg->screen_saver_fade_out_ms = q->ss_fade_out;
    if (q->has_ss_fade_in)    cfg->screen_saver_fade_in_ms = q->ss_fade_in;
    if (q->has_ss_wake_touch) cfg->screen_saver_wake_on_touch = q->ss_wake_touch;
    if (q->has_ss_wake_bind)  strlcpy(cfg->screen_saver_wake_binding, q->ss_wake_bind, CONFIG_SS_WAKE_BINDING_MAX_LEN);
    if (q->has_brightness) {
        cfg->backlight_brightness = q->brightness;
        display_manager_set_backlight_brightness(q->brightness);
        screen_saver_manager_notify_activity(true);
    }
#else
    if (q->has_brightness) cfg->backlight_brightness = q->brightness;
#endif
#if HAS_AUDIO
    if (q->has_volume) {
        cfg->audio_volume = q->volume;
        audio_set_volume(q->volume);
    }
#endif

    cfg->magic = CONFIG_MAGIC;
    bool saved = config_manager_save(cfg);
    heap_caps_free(q);
    if (!saved) { strlcpy(msg, "save failed", msg_len); return; }
    *ok = true;
    strlcpy(msg, "saved", msg_len);
}

static bool tool_set_config(const JsonObject& args, JsonObject& result, String& err) {
    DeviceConfig* cur = web_portal_get_current_config();
    if (!cur) return cfg_fail(result, err, CFG_ERR_INTERNAL, "config not initialized");

    SetConfigReq* q = (SetConfigReq*)mcp_psram_alloc(sizeof(SetConfigReq));
    if (!q) return cfg_fail(result, err, CFG_ERR_INTERNAL, "out of memory");
    memset(q, 0, sizeof(SetConfigReq));

    JsonArray applied = result.createNestedArray("applied");
    bool reboot_note = false;

    if (args.containsKey("device_name")) {
        const char* dn = args["device_name"] | "";
        if (!dn[0]) { heap_caps_free(q); return cfg_fail(result, err, CFG_ERR_PARAMS, "device_name must not be empty"); }
        q->has_device_name = true;
        strlcpy(q->device_name, dn, CONFIG_DEVICE_NAME_MAX_LEN);
        applied.add("device_name");
        reboot_note = true;  // mDNS/hostname refresh needs a reboot
    }
    if (args.containsKey("backlight_brightness")) {
        int b = args["backlight_brightness"] | -1;
        if (b < MIN_USER_BRIGHTNESS || b > 100) {
            heap_caps_free(q);
            return cfg_fail(result, err, CFG_ERR_PARAMS, "backlight_brightness must be 5-100");
        }
        q->has_brightness = true; q->brightness = (uint8_t)b;
        applied.add("backlight_brightness");
    }
    if (args.containsKey("mqtt_publish_interval_seconds")) {
        int v = args["mqtt_publish_interval_seconds"] | -1;
        if (v < 0 || v > 65535) {
            heap_caps_free(q);
            return cfg_fail(result, err, CFG_ERR_PARAMS, "mqtt_publish_interval_seconds out of range");
        }
        q->has_pub_interval = true; q->pub_interval = (uint16_t)v;
        applied.add("mqtt_publish_interval_seconds");
    }
    if (args.containsKey("mqtt_publish_scope")) {
        const char* s = args["mqtt_publish_scope"] | "";
        if (strcmp(s, "sensors_only") != 0 && strcmp(s, "diagnostics_only") != 0 && strcmp(s, "all") != 0) {
            heap_caps_free(q);
            return cfg_fail(result, err, CFG_ERR_PARAMS, "mqtt_publish_scope must be sensors_only|diagnostics_only|all");
        }
        q->has_pub_scope = true; strlcpy(q->pub_scope, s, CONFIG_MQTT_SCOPE_MAX_LEN);
        applied.add("mqtt_publish_scope");
    }
#if HAS_AUDIO
    if (args.containsKey("audio_volume")) {
        int v = args["audio_volume"] | -1;
        if (v < 0 || v > 100) {
            heap_caps_free(q);
            return cfg_fail(result, err, CFG_ERR_PARAMS, "audio_volume must be 0-100");
        }
        q->has_volume = true; q->volume = (uint8_t)v;
        applied.add("audio_volume");
    }
#endif
#if HAS_DISPLAY
    if (args.containsKey("screen_saver_enabled")) {
        q->has_ss_enabled = true; q->ss_enabled = args["screen_saver_enabled"] | false;
        applied.add("screen_saver_enabled");
    }
    if (args.containsKey("screen_saver_timeout_seconds")) {
        int v = args["screen_saver_timeout_seconds"] | -1;
        if (v < 0 || v > 65535) { heap_caps_free(q); return cfg_fail(result, err, CFG_ERR_PARAMS, "screen_saver_timeout_seconds out of range"); }
        q->has_ss_timeout = true; q->ss_timeout = (uint16_t)v;
        applied.add("screen_saver_timeout_seconds");
    }
    if (args.containsKey("screen_saver_fade_out_ms")) {
        int v = args["screen_saver_fade_out_ms"] | -1;
        if (v < 0 || v > 65535) { heap_caps_free(q); return cfg_fail(result, err, CFG_ERR_PARAMS, "screen_saver_fade_out_ms out of range"); }
        q->has_ss_fade_out = true; q->ss_fade_out = (uint16_t)v;
        applied.add("screen_saver_fade_out_ms");
    }
    if (args.containsKey("screen_saver_fade_in_ms")) {
        int v = args["screen_saver_fade_in_ms"] | -1;
        if (v < 0 || v > 65535) { heap_caps_free(q); return cfg_fail(result, err, CFG_ERR_PARAMS, "screen_saver_fade_in_ms out of range"); }
        q->has_ss_fade_in = true; q->ss_fade_in = (uint16_t)v;
        applied.add("screen_saver_fade_in_ms");
    }
    if (args.containsKey("screen_saver_wake_on_touch")) {
        q->has_ss_wake_touch = true; q->ss_wake_touch = args["screen_saver_wake_on_touch"] | false;
        applied.add("screen_saver_wake_on_touch");
    }
    if (args.containsKey("screen_saver_wake_binding")) {
        q->has_ss_wake_bind = true;
        strlcpy(q->ss_wake_bind, args["screen_saver_wake_binding"] | "", CONFIG_SS_WAKE_BINDING_MAX_LEN);
        applied.add("screen_saver_wake_binding");
    }
#endif

    if (applied.size() == 0) {
        heap_caps_free(q);
        return cfg_fail(result, err, CFG_ERR_PARAMS,
                        "no writable field provided (see get_config's writable_note)");
    }

    result["reboot_recommended"] = reboot_note;

    SetConfigCtx ctx; ctx.req = q;
    bool ok = false; char msg[MCP_TOOL_MSG_LEN] = {0};
    McpControlResult r = mcp_control_dispatch(exec_set_config, &ctx, sizeof(ctx),
                                              CFG_WRITE_TIMEOUT_MS, &ok, msg, sizeof(msg));
    if (r == MCP_CONTROL_BUSY) { heap_caps_free(q); return cfg_fail(result, err, CFG_ERR_BUSY, "busy, retry"); }
    // On TIMEOUT the deferred job may still run and free q, so do not free here.
    return mcp_finish_control(r, ok, msg, result, err);
}

// ============================================================================
// Tool descriptors + registration
// ============================================================================

static const McpTool s_tool_get_config = {
    "get_config",
    "Read the device's current settings (the same knobs as the web portal Setup/Display/Audio pages): "
    "device name, network, MQTT/Home Assistant, power/transport, display + screen saver, and audio. "
    "Secrets are never returned — passwords/tokens are reported only as '<field>_set' booleans. "
    "Use this to see how the device is configured before changing anything; write the safe subset with set_config.",
    "{\"type\":\"object\",\"properties\":{}}",
    tool_get_config, true, false, false
};
REGISTER_MCP_TOOL(s_tool_get_config);

#if HAS_DISPLAY || HAS_BUTTON || MQTT_TRIGGERS_ENABLED
static const McpTool s_tool_get_component_config = {
    "get_component_config",
    "Read the saved JSON for one auxiliary feature so you can inspect what a user configured before "
    "suggesting or making changes: 'timers' (count-up/down timers + expire actions), 'swipe' (edge-swipe "
    "gestures), 'boot' (actions run once at startup), 'button-defaults' (device-wide button appearance), "
    "'hw-buttons' (physical button bindings), 'mqtt-triggers' (inbound MQTT -> action rules). Returns "
    "'exists=false' with firmware defaults when the feature has never been saved. Only components compiled "
    "into this board are accepted. Pair with set_component_config to write changes back.",
    "{\"type\":\"object\",\"properties\":{\"component\":{\"type\":\"string\",\"enum\":[\"timers\",\"swipe\",\"boot\",\"button-defaults\",\"hw-buttons\",\"mqtt-triggers\"]}},\"required\":[\"component\"]}",
    tool_get_component_config, true, false, false
};
REGISTER_MCP_TOOL(s_tool_get_component_config);

static const McpTool s_tool_set_component_config = {
    "set_component_config",
    "Overwrite one auxiliary feature's configuration with a full replacement 'config' object — the write "
    "counterpart of get_component_config for 'timers', 'swipe', 'boot', 'button-defaults', 'hw-buttons', or "
    "'mqtt-triggers'. This REPLACES the whole config (it is not a merge), so read the current config first, "
    "edit the returned object, and send it back. The config is structurally validated before saving (correct "
    "shape, action lists capped at 3 entries each with a 'type', counts within the board's limits, MQTT "
    "trigger topics non-wildcard) and rejected with a reason if malformed. Expected shapes: timers = object "
    "keyed \"1\"..\"3\" of {mode:up|down, countdown, expire_actions:[]}; swipe = {swipe_left|right|up|down: "
    "action}; boot = {actions:[]}; button-defaults = appearance fields; hw-buttons = {buttons:[{tap_actions:[], "
    "hold_actions:[]}]}; mqtt-triggers = {triggers:[{topic, value, actions:[]}]}. Only components compiled into "
    "this board are accepted; changes persist to flash and reload live.",
    "{\"type\":\"object\",\"properties\":{\"component\":{\"type\":\"string\",\"enum\":[\"timers\",\"swipe\",\"boot\",\"button-defaults\",\"hw-buttons\",\"mqtt-triggers\"]},\"config\":{\"type\":\"object\",\"description\":\"full replacement config for the component (see get_component_config for the current shape)\"}},\"required\":[\"component\",\"config\"]}",
    tool_set_component_config, false, false, true
};
REGISTER_MCP_TOOL(s_tool_set_component_config);
#endif

#if HAS_DISPLAY
static const McpTool s_tool_notify = {
    "notify",
    "Show a message bubble on the device screen — the direct way to get a short message in front of the "
    "person at the device (e.g. 'Coffee's ready', a reminder, or a status note). 'text' is required; an "
    "empty string dismisses the current bubble. Optional: duration_ms (0 = stay until dismissed, default "
    "~3000), location (top|center|bottom), text_color/bg_color/border_color (#RRGGBB), opacity (1-100), "
    "font_size. Wakes the screen if asleep.",
    "{\"type\":\"object\",\"properties\":{\"text\":{\"type\":\"string\"},\"duration_ms\":{\"type\":\"integer\",\"minimum\":0},\"location\":{\"type\":\"string\",\"enum\":[\"top\",\"center\",\"bottom\"]},\"text_color\":{\"type\":\"string\"},\"bg_color\":{\"type\":\"string\"},\"border_color\":{\"type\":\"string\"},\"opacity\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":100},\"font_size\":{\"type\":\"integer\"}},\"required\":[\"text\"]}",
    tool_notify, false, false, true
};
REGISTER_MCP_TOOL(s_tool_notify);
#endif

#if HAS_AUDIO && (HAS_DISPLAY || HAS_BUTTON)
static const McpTool s_tool_set_volume = {
    "set_volume",
    "Set or step the speaker volume. mode 'set' takes an absolute 0-100; mode 'adjust' takes a signed delta "
    "(e.g. 10 or -10). Persisted device volume used by beeps and sound playback. Use set_backlight for screen "
    "brightness (this tool is audio only).",
    "{\"type\":\"object\",\"properties\":{\"mode\":{\"type\":\"string\",\"enum\":[\"set\",\"adjust\"]},\"value\":{\"type\":\"integer\"}},\"required\":[\"value\"]}",
    tool_set_volume, false, false, true
};
REGISTER_MCP_TOOL(s_tool_set_volume);
#endif

#if HAS_DISPLAY
static const McpTool s_tool_timer_control = {
    "timer_control",
    "Control one of the three on-screen timers (timer_id 1-3), just like the timer buttons in a pad. "
    "command: start | stop | toggle | pause | resume | reset | lap | set | adjust. 'set' sets the countdown "
    "target and 'adjust' changes it by a signed delta — both take 'value' in SECONDS. Real-world use: start a "
    "brew/steep/exposure timer, reset it, or preset a countdown. A count-down timer fires its configured "
    "expire actions when it reaches zero (see get_component_config 'timers').",
    "{\"type\":\"object\",\"properties\":{\"timer_id\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":3},\"command\":{\"type\":\"string\",\"enum\":[\"start\",\"stop\",\"toggle\",\"pause\",\"resume\",\"reset\",\"lap\",\"set\",\"adjust\"]},\"value\":{\"type\":\"integer\",\"description\":\"seconds; required for set/adjust\"}},\"required\":[\"timer_id\",\"command\"]}",
    tool_timer_control, false, false, true
};
REGISTER_MCP_TOOL(s_tool_timer_control);
#endif

static const McpTool s_tool_set_config = {
    "set_config",
    "Write a curated, SAFE subset of device settings that apply live without a reboot or dropping this MCP "
    "session: device_name, backlight_brightness (5-100), the screen_saver_* group "
    "(screen_saver_enabled, screen_saver_timeout_seconds, screen_saver_fade_out_ms, screen_saver_fade_in_ms, "
    "screen_saver_wake_on_touch, screen_saver_wake_binding), mqtt_publish_interval_seconds, "
    "mqtt_publish_scope (sensors_only|diagnostics_only|all), and audio_volume (0-100). Send only the fields "
    "you want to change; unknown/omitted fields are left untouched. Returns the 'applied' list. "
    "device_name persists immediately but its mDNS/hostname only refreshes on the next reboot "
    "(reboot_recommended=true). WiFi/MQTT/HA credentials, operating mode, and security toggles are NOT "
    "writable here (they can disconnect this session) — change those in the web portal.",
    "{\"type\":\"object\",\"properties\":{"
    "\"device_name\":{\"type\":\"string\"},"
    "\"backlight_brightness\":{\"type\":\"integer\",\"minimum\":5,\"maximum\":100},"
    "\"mqtt_publish_interval_seconds\":{\"type\":\"integer\",\"minimum\":0},"
    "\"mqtt_publish_scope\":{\"type\":\"string\",\"enum\":[\"sensors_only\",\"diagnostics_only\",\"all\"]},"
    "\"audio_volume\":{\"type\":\"integer\",\"minimum\":0,\"maximum\":100},"
    "\"screen_saver_enabled\":{\"type\":\"boolean\"},"
    "\"screen_saver_timeout_seconds\":{\"type\":\"integer\",\"minimum\":0},"
    "\"screen_saver_fade_out_ms\":{\"type\":\"integer\",\"minimum\":0},"
    "\"screen_saver_fade_in_ms\":{\"type\":\"integer\",\"minimum\":0},"
    "\"screen_saver_wake_on_touch\":{\"type\":\"boolean\"},"
    "\"screen_saver_wake_binding\":{\"type\":\"string\"}"
    "}}",
    tool_set_config, false, false, true
};
REGISTER_MCP_TOOL(s_tool_set_config);

// ============================================================================
// Capability manifest hook — consumed by get_capabilities (mcp_tools_pads.cpp)
// ============================================================================
// Advertises the device-settings surface alongside the pad manifest so an
// authoring client that reads get_capabilities discovers set_config's writable
// fields and the read/write component list without probing each tool schema.
// Board-accurate: the component list is the same s_comps table the tools use.
void mcp_config_capabilities(JsonObject& out) {
    JsonObject sc = out.createNestedObject("set_config_fields");
    sc["device_name"] = "string (mDNS/hostname refreshes on next reboot)";
    sc["backlight_brightness"] = "int 5-100 (persisted + applied live)";
    sc["mqtt_publish_interval_seconds"] = "int seconds (0 = disabled)";
    sc["mqtt_publish_scope"] = "sensors_only | diagnostics_only | all";
#if HAS_AUDIO
    sc["audio_volume"] = "int 0-100";
#endif
#if HAS_DISPLAY
    sc["screen_saver_enabled"] = "bool";
    sc["screen_saver_timeout_seconds"] = "int seconds";
    sc["screen_saver_fade_out_ms"] = "int ms";
    sc["screen_saver_fade_in_ms"] = "int ms";
    sc["screen_saver_wake_on_touch"] = "bool";
    sc["screen_saver_wake_binding"] = "binding expression; wakes on \"ON\"";
#endif
    out["config_note"] =
        "get_config reads ALL settings (secrets redacted to *_set booleans); set_config writes ONLY the "
        "fields above, live without a reboot. WiFi/MQTT/HA credentials, operating mode, and security "
        "toggles are portal-only.";
#if HAS_DISPLAY || HAS_BUTTON || MQTT_TRIGGERS_ENABLED
    JsonArray comps = out.createNestedArray("components");
    for (size_t i = 0; i < COMP_COUNT; ++i) comps.add(s_comps[i].name);
    out["components_note"] =
        "read with get_component_config, write with set_component_config (full-replacement, structurally "
        "validated).";
#endif
}

#endif // HAS_MCP
