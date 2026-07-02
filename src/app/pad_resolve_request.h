#pragma once

#include "board_config.h"

#if HAS_DISPLAY && HAS_MQTT

#include <ArduinoJson.h>

// ============================================================================
// pad_resolve_request — shared "resolve bindings against live data" request
// handler, used by BOTH front-ends:
//   - MCP tool  resolve_bindings   (mcp_tools_pads.cpp)
//   - Web portal POST /api/pad/resolve (web_portal_pad.cpp)
//
// Resolves [scheme:params] tokens in the request against the device's LIVE
// data and fills `result`. It runs pad_resolve() on the main loop via the
// deferred main-loop bridge (binding_template_resolve is LVGL/main-task only),
// so it is safe to call from any web/async request handler. Nothing is saved.
//
// Request (`args`):
//   screen   (optional) — "pad_N" or friendly name; supplies that pad's
//                         [pad:name] binding context.
//   bindings (optional) — array of template strings to resolve.
//   button   (optional) — a proposed button object; its bindable fields
//                         (labels / *_color / btn_state / widget_data_binding[_2..4])
//                         are resolved. At least one of bindings/button required.
//
// Result on OK:
//   resolved — array of { input, value } for `bindings`.
//   button   — object of field -> value for `button`.
// ============================================================================

enum PadResolveStatus {
    PAD_RESOLVE_OK = 0,
    PAD_RESOLVE_BAD_PARAMS,   // missing/invalid args
    PAD_RESOLVE_BUSY,         // another deferred job is in flight
    PAD_RESOLVE_OOM,          // allocation failed
    PAD_RESOLVE_ERROR,        // dispatch timed out / internal
};

// Fill `result` from `args`. On non-OK, *err_msg (if non-null) is set to a short
// human-readable reason. `args` (and any strings it owns) must stay alive for
// the duration of the call — it blocks on the bridge until resolution completes.
PadResolveStatus pad_resolve_request(JsonObjectConst args, JsonObject result,
                                     const char** err_msg);

#endif // HAS_DISPLAY && HAS_MQTT
