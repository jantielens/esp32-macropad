# Logging Guidelines

## Goals
- Keep logs short, consistent, and low-overhead.
- Avoid RAM/CPU spikes and multi-task interleaving issues.
- Make logs easy to scan and filter by module/level.

## Format
Single-line only:

`[<ms>] <L> <MODULE>: <message>`

- `<ms>` is `millis()` since boot.
- `<L>` is one of: `E`, `W`, `I`, `D`.
- `<MODULE>` is a short stable tag (≤10 chars).

## API
Use the macros from [src/app/log_manager.h](src/app/log_manager.h):

- `LOGE(MOD, fmt, ...)`
- `LOGW(MOD, fmt, ...)`
- `LOGI(MOD, fmt, ...)`
- `LOGD(MOD, fmt, ...)`
- `LOG_DURATION(MOD, label, start_ms)`

Initialize once:
- `log_init(115200)`

## Module Tags
Recommended tags:
- `SYS`, `WIFI`, `MQTT`, `PORTAL`, `API`, `DISPLAY`, `TOUCH`, `MEM`, `OTA`, `IMG`, `SAVER`, `TELEM`
Additional tags used:
- `DIRIMG`, `STRIP`, `STRIPDEC`, `GFX`

## Rules
1. **One event = one line**
   - No nested blocks or multi-line logs.
2. **Explicit severity**
   - Use `LOGE` for errors, `LOGW` for recoverable issues, `LOGI` for normal events, `LOGD` for debug.
3. **Keep messages short**
   - Target ≤120 chars. Avoid large payload dumps.
4. **Avoid heap churn**
   - Use format strings; avoid building `String` objects in log paths.
5. **Rate-limit periodic logs**
   - Use a time gate (e.g., `log_every_ms()` pattern) for loop/timer logs.
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
