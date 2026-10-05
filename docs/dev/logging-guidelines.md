---
title: Logging Guidelines
description: Severity, targeted diagnostics, failure summaries, and serial export conventions.
---

## Goals
- Keep logs short, consistent, and low-overhead.
- Avoid RAM/CPU spikes and multi-task interleaving issues.
- Make logs easy to scan and filter by module/level.

## Format
Single-line only:

`[<ms>] <L> <MODULE>: <message>`

- `<ms>` is `millis()` since boot.
- `<L>` is one of: `E`, `W`, `I`, `D`.
- `<MODULE>` is a short stable tag, displayed up to 24 characters.
- Control characters in messages and module tags are replaced with spaces.
- Messages exceeding the 191-byte message buffer end in `... [truncated]`.

## API
Use the macros from [log_manager.h](../../src/app/log_manager.h):

- `LOGE(MOD, fmt, ...)`
- `LOGW(MOD, fmt, ...)`
- `LOGI(MOD, fmt, ...)`
- `LOGD(MOD, fmt, ...)`
- `LOGT(MOD, fmt, ...)` for explicitly selected subsystem diagnostics
- `LOG_DURATION(MOD, label, start_ms)`

Initialize once:
- `log_init(115200)`

## Module Tags
Recommended tags:
- `SYS`, `WIFI`, `MQTT`, `PORTAL`, `API`, `DISPLAY`, `TOUCH`, `MEM`, `OTA`, `IMG`, `SAVER`, `TELEM`
Additional tags used:
- `DIRIMG`, `STRIP`, `STRIPDEC`, `GFX`

## Selecting Diagnostics

`LOG_LEVEL` defaults to 4 (DEBUG). Levels are 0 (disabled), 1 (ERROR),
2 (WARN), 3 (INFO), and 4 (DEBUG). Filtering uses C++ constant comparisons,
not preprocessor comparisons against enum names. Rejected macros do not
evaluate formatting arguments; direct `log_write()` and duration calls are
also filtered by the backend.

Routine contact, HID report, widget geometry, image/cache, driver setup,
configuration detail, and action dispatch messages use `LOGT`.
`LOG_DIAGNOSTICS` defaults to `"*"`, so DEBUG enables every subsystem's
diagnostics without another setting. For quieter operational output, override
`LOG_LEVEL` to 3 (INFO), which does not emit `LOGT` messages.

To narrow DEBUG output, define `LOG_DIAGNOSTICS` build-wide as a
comma-separated list of exact, case-sensitive module tags, for example
`"GT911,Touch,GamepadHID,GamepadJoystick,GamepadButton"`. An explicit `""`
disables `LOGT` while retaining ordinary `LOGD` messages. Full diagnostics
can affect timing; select modules when investigating timing-sensitive behavior.
Keep the same definitions across compilation units, including the logger
implementation.

Normal INFO output reports operational outcomes and state transitions.
Secrets, notification text, key sequences, MQTT payloads, and source URLs
must not be dumped into routine logs. Use byte counts, slots, entity names,
HTTP status, error codes, and recovery counts instead.

NVS initialization stages, individual brightness/rotation values, GT911 reset
steps, and cached RSSI are targeted details under `Config`, `Main`, `GT911`,
and `Telemetry`. The loaded-configuration summary, completed touch
initialization, and failures remain visible at their normal levels.

## Memory Snapshots

`Mem` lines distinguish current free/minimum counters (`hf`, `hm`, `hi`,
`hin`, `pf`, `pm`) from cached allocator samples. On boards with cached
internal pool walks, `hl_sample` is the sampled largest block, `hf_sample`
is the matching sampled free-heap value, and `pool_age_ms` is the sample age.
`frag_sample` uses that pair, never the current `hf`. If the sample is older
than the configured pool-walk interval, absent, or inconsistent, fragmentation
is `na`, not an apparent 0% or 100%. Sample fields are historical, not current
largest-block guarantees.

Boards without cached internal walks retain `hl` and `frag`, with invalid or
unavailable values marked `na`. The PSRAM largest-block field `pl` is `na`
when its pool walk is disabled or its result is unavailable/inconsistent.
Reporting adds no allocator walks: cached sampling stays in the existing
background sampler. These labels apply to serial output; REST fields are
unchanged.

WiFi connection attempts label `WL_IDLE_STATUS` (0) as `Idle/connecting`.
It remains a warning after a failed/timed-out attempt, not an unknown status.

## Repeated Failures

The backend emits the first WARN/ERROR immediately. Identical rendered
messages from the same module and severity are suppressed for five seconds.
The next matching emission includes `[suppressed=N]`. Independent errors
retain their first occurrence. A fixed 32-entry fingerprint table bounds
memory; eviction or a failure that never recurs can leave counts unreported.
This is flood protection, not a durable error ledger.

Changing counters defeat exact-message suppression. Owners must pace their
attempts and aggregate these failures: camera capture and shutter ADC report
the first failure, changed error, and periodic totals; radar reports per scan.
Camera and MQTT health failures advance their retry deadlines. Selected owners
report recovery counts when operations resume. E-paper drivers emit periodic
refresh summaries rather than one INFO line per partial refresh. Audio reports
nonzero starvation and per-playback truncation counts, not successful zeroes.

## Serial Ownership

On ESP32, complete logger records are serialized with a static FreeRTOS mutex.
Logging remains synchronous and task-only; do not call it from ISRs, critical
sections, or while holding a framebuffer mutex. Emergency allocation/ISR
diagnostics keep their direct, minimal output path and can bypass serialization.
Avoid introducing a background logging task just to hide excessive logging.

Shutter `[MEAS]` CSV is a separate export channel. `LOG_SHUTTER_CSV` defaults
to 1 regardless of `LOG_LEVEL`, preserving the header and field order consumed
by the measurement monitor. Define it as 0 build-wide to disable CSV without
changing measurement behavior. Headers and rows use `log_serial_begin()` /
`log_serial_end()` to prevent ordinary task logs splitting a record. Preserve
the USB-CDC flush and brief delay before NVS writes.

Native extension messages include package ID and instance ID; worker messages
use instance 0. Extension lifecycle/event details use host-side targeted
diagnostics without changing ABI 17. Word-clock phrase logging is off by
default; its optional numeric configuration `"log_phrases":1` enables it for
the selected widget.

## Rules
1. **One event = one line**
   - No nested blocks or multi-line logs.
2. **Explicit severity**
   - Use `LOGE` for faults, `LOGW` for recoverable problems, `LOGI` for outcomes,
     `LOGD` for low-rate debug summaries, and `LOGT` for verbose opt-in detail.
3. **Keep messages short**
   - Target ≤120 chars. Avoid large payload dumps.
4. **Avoid heap churn**
   - Use format strings; avoid building `String` objects in log paths.
5. **Rate-limit periodic logs**
   - Use a time gate and counters for loop/timer logs; pace failed work as well.
6. **No logs in tight loops/ISRs**
   - Log only state changes or first occurrence.
7. **Errors include context**
   - Provide minimal `k=v` pairs or error codes.

## Examples
- `LOGI("WIFI", "Connected ssid=%s rssi=%d", ssid, rssi);`
- `LOGW("MQTT", "Reconnect failed state=%d", state);`
- `LOGE("IMG", "Decode failed err=%d", err);`

## Notes
- Flat logging is intentional to avoid cross-task nesting corruption.
- Duration tracking is explicit via `LOG_DURATION()`.
