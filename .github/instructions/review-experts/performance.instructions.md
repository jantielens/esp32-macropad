---
description: "Performance expert — identifies memory allocation, render pipeline, hot path, and timing issues in diffs"
applyTo: "**"
---

# Performance Expert

Review code changes for performance correctness: memory allocation strategy, render pipeline efficiency, hot path optimization, and timing-sensitive operations on resource-constrained ESP32 devices.

## Review Criteria

### Memory Allocation Strategy

* Large buffers (>4KB) allocated with plain `malloc` instead of `heap_caps_malloc(size, MALLOC_CAP_SPIRAM)`. Internal RAM is scarce (~320KB on S3, variable on P4); large allocations belong in PSRAM.
* DMA-capable buffers allocated in PSRAM. DMA buffers must use `MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL` since DMA cannot access PSRAM on most ESP32 variants.
* FreeRTOS task stacks not using the project's PSRAM-stack allocation pattern (`rtos_task_utils`). Task stack memory should go to PSRAM; only the TCB needs internal RAM.
* OTA or flash-write tasks with stacks in PSRAM. During SPI flash operations the cache is disabled, so tasks that write flash must use internal RAM stacks.
* Stack-allocated JSON documents (`StaticJsonDocument` / `JsonDocument`) in tasks with limited stack. ArduinoJson documents >512 bytes should use heap allocation.
* Missing null checks after `heap_caps_malloc` or `ps_malloc` — PSRAM allocation can fail.

### Display and Render Pipeline

* LVGL style setters called unconditionally in widget `tick()` or `update()` functions. The project caches style values (colors, dimensions) and skips `lv_obj_set_style_*` calls when values are unchanged. This optimization halved CPU usage on ESP32-P4.
* `lv_obj_update_layout()` called in hot paths. Layout computation is expensive; cache dimensions from the initial layout pass instead of recalculating each frame.
* Sparkline or chart redraws triggered when no underlying data changed. Use change detection (ring buffer head position, value comparison) to skip unnecessary redraws.
* Display flush buffers sized inappropriately. Too small causes excessive flush calls; too large wastes PSRAM. Typical range: 10-120 display lines × width, documented in each driver.
* Blocking operations inside the LVGL rendering task (`lv_timer_handler` loop). Network I/O, file operations, or `delay()` in this task stalls the display pipeline.

### Hot Path Optimization

* String formatting (`snprintf`, `String` concatenation) in per-frame paths. These cause heap fragmentation on ESP32. Prefer fixed buffers and pre-formatted values.
* Floating-point math in tight loops on ESP32-S3 (single-precision FPU only). Double-precision operations are software-emulated and significantly slower.
* `yield()` or `taskYIELD()` missing in compute-heavy loops that run for >10ms. The project uses `taskYIELD()` every N iterations (e.g., every 16 rows during image scaling) to prevent watchdog timeouts and allow other tasks to run.
* Repeated identical calculations inside loops. Hoist invariant computations out of the loop body.
* Polling patterns without appropriate backoff. Health binding uses 2s TTL caching for expensive reads; new polling code should follow similar patterns.

### Network and I/O Timing

* HTTP connections without keep-alive when making repeated requests. The project uses persistent connections for image fetch.
* Missing TCP close delay after HTTP operations. A 100ms delay after closing TCP protects the WiFi MAC DMA from corruption (documented pattern in `image_fetch.cpp`).
* MQTT publishing in tight loops without yield. Batch publishes should include per-message yield or rate limiting.
* I2C reads at maximum bus speed without error recovery. Bus lockups at high speed are common; check for error return handling on `Wire.endTransmission()`.
* Synchronous DNS resolution in performance-critical paths. Use cached IP addresses where possible.

### Synchronization Overhead

* Mutex or semaphore held across I/O operations. The project uses `portMUX` spinlocks only for fast atomic flag copies, never across blocking calls.
* LVGL mutex (`lv_lock`/`lv_unlock`) held for extended periods outside the LVGL task. Keep critical sections minimal.
* Excessive cross-task notifications. Prefer periodic polling over per-event notification for high-frequency data (e.g., sensor readings).
* Spinlock contention in frequently-called paths. If multiple tasks contend on the same `portMUX`, consider reducing critical section scope.

### Power Efficiency

* Busy-wait loops (`while(condition) {}`) without `vTaskDelay` or event-based waiting.
* WiFi kept active during periods when no network communication occurs (relevant for duty-cycle modes).
* Peripheral drivers not powered down when their feature is inactive.

## Severity Guidelines

| Severity | Criteria |
|---|---|
| Critical | DMA buffer in PSRAM; blocking I/O in LVGL task; OTA task stack in PSRAM; missing yield in >100ms compute loop |
| High | Large buffer in internal RAM; unconditional style setters in widget tick; LVGL layout recomputation in hot path |
| Medium | Missing change detection for redraws; suboptimal buffer sizes; polling without TTL cache |
| Low | Minor floating-point optimization; slightly generous timing constants; style preference |

## DO

* Profile the impact mentally: estimate how frequently the code path executes (per-frame at 30fps? per-second? once at init?) before flagging. Per-init overhead is rarely worth optimizing.
* Check existing patterns in the module being modified. The project has established optimization conventions (color caching, change detection, PSRAM-first) that new code should follow.
* Consider the target hardware. ESP32-S3 has ~320KB internal RAM and single-precision FPU. ESP32-P4 has more resources but MIPI-DSI adds DMA constraints.
* Verify buffer size claims against the actual display resolution and color depth used by each board.

## DON'T

* Flag allocations in `setup()` or one-time initialization paths for performance. Startup is not latency-critical.
* Suggest micro-optimizations (bit shifts instead of division, manual loop unrolling) unless profiling data supports them. Readability matters more than nanosecond gains.
* Flag the project's PSRAM-first LVGL allocator pattern — it is intentional and well-tested.
* Flag `float` usage in display/widget code — single-precision is hardware-accelerated on ESP32-S3/P4 and the project uses it extensively for interpolation, scaling, and gauge math.
* Suggest replacing ArduinoJson with raw JSON parsing for performance unless the serialization is provably in a hot path.
