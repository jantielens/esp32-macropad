---
description: "Binding template engine conventions — scheme registration, token syntax, resolution rules, and thread safety requirements"
applyTo: "**/binding_template.*, **/*_binding.*, **/expr_eval.*, **/expr_binding.*, **/pad_binding.*, **/data_stream.*"
---

# Binding Template Engine Conventions

## Token Syntax

Binding tokens use `[scheme:params]` format inside label text:

- `[mqtt:topic;json_path;format]` — live MQTT data
- `[health:key;format]` — local device telemetry
- `[time:format;timezone]` — NTP-synced clock
- `[expr:expression;format]` — evaluated expression with inner bindings
- `[pad:name;format]` — pad-level named binding (define once, use everywhere)
- `[timer:N]` — on-device timer value

Mixed static + binding: `"Temperature: [mqtt:sensors/temp;$.value;%.1f]°C"`

## Pipe Fallback

`[scheme:params|fallback]` — custom placeholder when binding cannot resolve (default: `---`). Parsed at outermost bracket depth by `split_pipe_fallback()`.

## Scheme Registry

- Max 16 schemes registered via `binding_template_register(scheme, resolver, collector, spec)`
- Each scheme provides a `resolver` function and optionally a `collect_topics` function
- Core built-in schemes register only through `binding_builtin_schemes_init()` in `binding_builtin_schemes.cpp`. Add the scheme's header and guarded init call there; do not register core schemes directly from `app.ino`.
- Device-class schemes register through their owning device class's `on_setup_late` hook. Do not add them to the built-in aggregate.
- The MQTT scheme registers during `mqtt_sub_store_init()` because it owns its subscription store.

## Thread Safety

**Critical**: `binding_template_resolve()` is NOT thread-safe. It uses internal static buffers. Call only from the LVGL task. All `lv_*` calls and binding resolution must happen on the LVGL rendering task or be protected by `lv_lock`/`lv_unlock`.

## Resolution Flow

1. Token parser finds `[scheme:params]` tokens in label text
2. Scheme registry looks up resolver by scheme name
3. Resolver writes resolved value into output buffer
4. If resolver returns `false`, placeholder (`---` or pipe fallback) is used
5. Error conditions produce `ERR:xxx` placeholders

## Expression Binding (`[expr:]`)

- Inner bindings are resolved first, then the expression is evaluated
- Uses bracket-depth-aware semicolon splitting (not simple `strchr`)
- Expression evaluator (`expr_eval.cpp`) supports: arithmetic, comparisons, ternary, string operations, `threshold()` function
- `expr_eval.cpp` is pure C with no ESP32 dependencies — host-testable

## Pad Binding (`[pad:]`)

- Resolves against pad-level `bindings` array (name→value map)
- Supports per-usage format override: `[pad:name;%.2f]`
- `collect_topics()` recurses into the underlying binding's scheme
- Requires pad context pointer — only valid within pad screen rendering

## Adding a New Scheme

1. Implement `resolver(params, out, out_len)` → returns `true` if resolved
2. Optionally implement `collector(params, user_data)` for MQTT topic collection
3. Call `binding_template_register("scheme_name", resolver, collector, spec)` from the scheme's init function
4. Add that init function to `binding_builtin_schemes_init()` for a core scheme, or its owning `on_setup_late` hook for a device-class scheme
5. Gate with appropriate `#if HAS_*` flags
6. **Expose it to the MCP server** (so LLM pad-authoring clients can discover and use it): provide complete `BindingSchemeSpec` metadata. The shared registry serializes it for both MCP and the portal; `tests/test_mcp_scheme_parity.sh` enforces shared registry metadata for each registered scheme.

## Key Files

- `binding_template.cpp/h` — Token parser, scheme registry, resolve/collect API
- `health_binding.cpp/h` — Health scheme (cached telemetry: CPU, heap, PSRAM, RSSI, uptime, WiFi, device info)
- `time_binding.cpp/h` — Time scheme (NTP, Olson TZ, sub-second codes)
- `expr_eval.cpp/h` — Pure C expression evaluator (host-testable)
- `expr_binding.cpp/h` — Expression scheme glue with bracket-depth splitting
- `pad_binding.cpp/h` — Pad scheme with context pointer and topic recursion
- `timer_binding.cpp/h` — Timer scheme for on-device timers
- `data_stream.cpp/h` — Demand-driven ring buffer registry for history widgets
