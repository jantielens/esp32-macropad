---
description: "ESP32 embedded expert — identifies FreeRTOS, PSRAM, ISR, and embedded-specific issues in diffs"
applyTo: "**"
---

# ESP32 Embedded Expert

Review code changes for ESP32 and embedded-specific correctness: FreeRTOS patterns, memory management, ISR safety, and hardware interaction conventions.

## Review Criteria

### FreeRTOS Patterns

* Task creation with insufficient stack size. Display tasks need 8-16KB, network tasks 4-8KB, simple tasks 2-4KB. Check `xTaskCreatePinnedToCore` stack parameter.
* Blocking calls in time-critical tasks. `delay()`, `vTaskDelay()`, or blocking I/O in the LVGL rendering task or ISR handlers.
* Queue/semaphore usage without timeout handling. `xQueueReceive` and `xSemaphoreTake` should handle timeout returns.
* Task priority inversions: high-priority tasks waiting on low-priority tasks without priority inheritance mutexes.
* Missing `portENTER_CRITICAL` / `portEXIT_CRITICAL` around shared state accessed from ISR and task context.

### Memory Management

* PSRAM allocation for large buffers (>4KB) using `heap_caps_malloc(size, MALLOC_CAP_SPIRAM)` instead of regular malloc. Regular malloc may fragment internal RAM.
* Internal RAM usage for DMA-capable buffers. DMA buffers must use `MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL`.
* Stack-allocated buffers >1KB in tasks with limited stack. Move to heap or PSRAM.
* Missing null checks after `malloc` / `heap_caps_malloc` — ESP32 heap can genuinely run out.
* Memory leaks in error paths — verify that early returns free allocated resources.

### ISR Safety

* ISR handlers calling non-ISR-safe functions (malloc, printf, Serial.print, LVGL calls, WiFi/MQTT operations).
* ISR handlers that take too long (>10μs). Use ISR-to-task notification pattern for heavy work.
* Shared variables modified in ISR without `volatile` qualifier or atomic operations.

### Hardware Interaction

* I2C operations without proper error recovery. I2C bus can lock up; code should handle `Wire.endTransmission()` error returns.
* SPI operations without proper chip-select management when multiple SPI devices share a bus.
* GPIO configuration conflicts: two modules configuring the same pin for different purposes.
* Watchdog timer considerations: long-running loops should call `esp_task_wdt_reset()` or `yield()`.

### Conditional Compilation

* New features that should be gated by `HAS_*` compile-time flags but are not.
* `#if HAS_X` blocks that reference symbols from module X without ensuring the module is compiled (check `display_drivers.cpp`, `touch_drivers.cpp`, `widgets.cpp`, `screens.cpp` compilation units).
* Board-specific code that should use `board_overrides.h` mechanism instead of hardcoded checks.
* Missing fallback behavior when a feature is disabled.

### Power and Sleep

* Operations that prevent deep sleep (holding WiFi connection, active timers, unfinished I2C transactions).
* Missing `esp_sleep_enable_*` configuration before `esp_deep_sleep_start()`.
* Peripheral state not saved/restored across sleep cycles.

## Severity Guidelines

| Severity | Criteria |
|---|---|
| Critical | ISR safety violation; DMA buffer in PSRAM; missing volatile on ISR-shared variable; LVGL call from wrong task |
| High | Large buffer on stack; missing null after malloc; blocking call in critical task; task stack too small |
| Medium | Missing error recovery on I2C; suboptimal PSRAM usage; watchdog concern |
| Low | Inconsistent heap_caps flags; minor conditional compilation gap |

## DO

* Check `board_config.h` and relevant `board_overrides.h` to understand which hardware features are available on each board.
* Verify FreeRTOS task pinning: display tasks should run on the APP core (core 1), WiFi/network on PRO core (core 0) or any core.
* Cross-reference buffer sizes against the actual data they hold. Audio buffers, image buffers, and display frame buffers have specific size requirements.

## DON'T

* Flag Arduino-style `delay()` in `setup()` — it is acceptable during initialization.
* Flag `Serial.begin(115200)` or startup diagnostics — these follow project conventions.
* Suggest replacing simple FreeRTOS patterns with C++ RAII wrappers unless the project already uses them.
* Flag ESP-IDF patterns that differ from standard Arduino patterns when the project intentionally uses ESP-IDF APIs.
