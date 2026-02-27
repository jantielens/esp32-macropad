# ESP32-P4 Display Performance Analysis

> **Purpose:** Reference document comparing our DSI display driver against
> Waveshare's official ESP-IDF sample projects, and documenting the LVGL v9
> upgrade performance work. Use this when investigating display instability,
> low FPS, visual tearing, or planning performance optimizations.
>
> **Last updated:** February 2026  
> **Board:** Waveshare ESP32-P4-WIFI6-Touch-LCD-4B  
> **Panel:** ST7703 MIPI-DSI 720×720 IPS, 2-lane 480 Mbps/lane  
> **LVGL:** 9.5.0 (upgraded from 8.4.0 — see §10)

## Reference Implementations

| Project | Framework | LVGL | Source |
|---|---|---|---|
| **Ours** | Direct ESP-IDF DSI + Arduino (arduino-cli) | 9.5.0 | `src/app/drivers/st7703_dsi_driver.cpp` |
| **NINA Display** | Waveshare BSP (ESP-IDF 5.4.0) | 9.2.0 | `sample/ESP32-P4-NINA-Display/` |
| **Smart-86-Box** | Waveshare BSP (ESP-IDF 5.4.0) | 8.4.x | `sample/ESP32-P4-Smart-86-Box-Smarthome-Panel/` |

Both sample projects use the `waveshare/esp32_p4_wifi6_touch_lcd_4b` BSP component,
which abstracts all DSI panel initialization, backlight, and touch behind a
`bsp_display_start_with_config()` API.

---

## 1. Framebuffer Architecture

All implementations share the same fundamental architecture:

```
LVGL partial render buffer (PSRAM or internal SRAM)
  → flush callback → esp_lcd_panel_draw_bitmap()
    → DMA2D hardware copies pixels to DPI framebuffer (async)
      → on_color_trans_done callback → lv_display_flush_ready()
        → DSI peripheral DMA reads from PSRAM framebuffer → panel
```

**Our driver** (`st7703_dsi_driver.cpp` — direct ESP-IDF, bypassing Arduino_GFX):
- Calls `esp_lcd_new_panel_dpi()` with `num_fbs = 1` (single DPI framebuffer)
- Sets `.flags.use_dma2d = true` and **`.flags.disable_lp = true`** (see §6)
- Framebuffer allocated in PSRAM by ESP-IDF internally (~1 MB for 720×720 RGB565)

**Pixel copy path** (current — DMA2D, since LVGL v9 upgrade):
- `pushColors()` calls `esp_lcd_panel_draw_bitmap(panel_handle, x1, y1, x2, y2, data)`
- With `use_dma2d=true`, this triggers an **asynchronous** hardware 2D-DMA copy
- Cache management (writeback) is handled internally by `esp_lcd_panel_draw_bitmap()`
- DMA completion fires `on_color_trans_done` ISR callback → `lv_display_flush_ready()`
- CPU is free to perform LVGL rendering while DMA copies pixels in the background

**Previous pixel copy path** (v8, CPU memcpy — replaced):
- Row-by-row `memcpy()` from LVGL buffer → PSRAM framebuffer
- Manual `esp_cache_msync()` with `ESP_CACHE_MSYNC_FLAG_DIR_C2M` after copy
- **Synchronous** — CPU blocked during the entire copy (~14.8 ms/frame, 47% of budget)

**Why we bypassed Arduino_GFX for DSI:**
Arduino_GFX v1.6.5's `Arduino_ESP32DSIPanel` does not expose the `disable_lp` flag.
With `disable_lp=false` (default), the DSI host enters Low-Power mode during every
blanking interval, causing cyan/idle-color flashes on the ST7703 panel (see §6).
The direct ESP-IDF path also enables the DMA2D hardware copy path.

---

## 2. Configuration Differences

### LVGL Draw Buffer

| Setting | Ours | NINA | Smart-86-Box |
|---|---|---|---|
| **Buffer lines** | 80 | BSP default (KConfig) | BSP default (KConfig) |
| **Buffer size** | 720 × 80 = 57,600 px (115 KB) | `BSP_LCD_DRAW_BUFF_SIZE` | `BSP_LCD_DRAW_BUFF_SIZE` |
| **Memory** | **PSRAM** | PSRAM (`buff_spiram=true`) | **Internal RAM** (`buff_spiram=false`) |
| **Double buffer** | No (single) | `BSP_LCD_DRAW_BUFF_DOUBLE` | `BSP_LCD_DRAW_BUFF_DOUBLE` |

**Key finding (v8):** Smart-86-Box deliberately uses `buff_spiram=false` for internal
RAM LVGL draw buffers. Internal SRAM access is ~3–5× faster than PSRAM for CPU writes,
which mattered with the old synchronous CPU memcpy flush path.

**Key finding (v9 + DMA2D):** With the DMA2D hardware flush, the draw buffer location
no longer affects FPS. PSRAM buffers perform identically to internal SRAM buffers
(39–42 FPS in both cases) because the CPU is no longer involved in the copy. Using
PSRAM frees ~115 KB of internal SRAM for the heap.

Current config: `LVGL_BUFFER_PREFER_INTERNAL false` + `LVGL_BUFFER_SIZE (720 * 80)`.

Our code supports double buffering via `LVGL_DRAW_BUF_COUNT == 2` in
`board_overrides.h`, but it is **not enabled** for the P4 board. Empirical testing
with v8 showed that double buffering **hurts** FPS when the flush is synchronous CPU
memcpy (see §7). With DMA2D, double buffering may now be viable but has not been
re-tested.

### LVGL Refresh Period

| Setting | Ours | NINA | Smart-86-Box |
|---|---|---|---|
| **Refresh period** | 15 ms (override) | 15 ms | 15 ms |
| **Target FPS** | ~66 fps | ~66 fps | ~66 fps |

LVGL default `LV_DEF_REFR_PERIOD` is 33 ms (v9) / 30 ms (v8). Both sample repos
explicitly set 15 ms. We override this at runtime via `LVGL_REFR_PERIOD_MS 15` in
`board_overrides.h` (applied in `display_manager.cpp` after display driver registration
using `lv_timer_set_period()`). This had **zero FPS cost** in testing.

Relevant config paths:
- **Ours:** `LVGL_REFR_PERIOD_MS 15` in `board_overrides.h` → runtime override
- **NINA:** `sdkconfig.defaults` → `CONFIG_LV_DEF_REFR_PERIOD=15`
- **Smart-86-Box:** `sdkconfig.defaults` → `CONFIG_LV_DISP_DEF_REFR_PERIOD=15`

### LVGL 9.x Features (Now Available)

With the LVGL v9.5.0 upgrade, these features are now active:

| Feature | Setting | Impact | Status |
|---|---|---|---|
| **PPA (Pixel Processing Accelerator)** | `LV_USE_PPA 1` in `lv_conf.h` | Hardware-accelerated blend/fill/rotate | **ENABLED** |
| **DMA2D flush** | `esp_lcd_panel_draw_bitmap()` with `use_dma2d=true` | Async framebuffer copy, CPU freed during flush | **ENABLED** |
| **IRAM fast-mem** | `LV_ATTRIBUTE_FAST_MEM IRAM_ATTR` in `lv_conf.h` | Hot LVGL functions in IRAM | **ENABLED** |
| **Multi-threaded SW rendering** | `LV_DRAW_SW_DRAW_UNIT_CNT=2` | Uses both RISC-V cores for parallel rendering | **TESTED, REJECTED** — PSRAM contention |
| **LVGL OS threading** | `LV_USE_OS LV_OS_FREERTOS` | Internal render thread management | **TESTED, REJECTED** — no benefit with 1 draw unit |

### Build System / sdkconfig

Both IDF sample repos explicitly configure the ESP32-P4 memory subsystem:

```ini
# Memory subsystem (both NINA and Smart-86-Box)
CONFIG_SPIRAM_SPEED_200M=y           # PSRAM at 200 MHz (max)
CONFIG_SPIRAM_XIP_FROM_PSRAM=y       # Execute code from PSRAM (frees flash bandwidth)
CONFIG_CACHE_L2_CACHE_256KB=y        # Maximum L2 cache (256 KB)
CONFIG_CACHE_L2_CACHE_LINE_128B=y    # 128-byte cache lines (optimal for DMA reads)

# Compiler and runtime
CONFIG_COMPILER_OPTIMIZATION_PERF=y  # GCC -O2 (vs Arduino's -Os)
CONFIG_FREERTOS_HZ=1000              # 1 kHz tick (vs Arduino default 100 Hz on some cores)

# LVGL performance (both repos)
CONFIG_LV_ATTRIBUTE_FAST_MEM_USE_IRAM=y  # Hot LVGL functions in IRAM (single-cycle)
```

**Arduino build:** These are controlled by the ESP32 Arduino core's built-in
`sdkconfig` defaults for the P4 target. We do not have direct control without a
custom board JSON or forked Arduino core. The P4 Arduino support is relatively new
and defaults may not be display-optimized.

### Smart-86-Box Additional Tuning

```ini
CONFIG_LV_LAYER_SIMPLE_BUF_SIZE=102400   # 100 KB layer buffer
CONFIG_LV_IMG_CACHE_DEF_SIZE=5           # Image cache slots
CONFIG_LV_GRAD_CACHE_DEF_SIZE=10240      # 10 KB gradient cache
CONFIG_LV_USE_PERF_MONITOR=y             # Runtime perf overlay
```

---

## 3. Impact Summary (Ranked)

| # | Gap | Impact | Status |
|---|---|---|---|
| 1 | ~~No PPA acceleration~~ | ~~HIGH~~ | **APPLIED** — LVGL v9.5.0 with `LV_USE_PPA 1` |
| 2 | ~~CPU memcpy flush bottleneck~~ | ~~HIGH~~ | **APPLIED** — DMA2D async flush via `esp_lcd_panel_draw_bitmap()` (+10 FPS) |
| 3 | ~~Single-threaded rendering~~ | ~~HIGH~~ | **TESTED, REJECTED** — multi-unit regressed to 27 FPS (PSRAM contention) |
| 4 | **Arduino sdkconfig defaults** (PSRAM speed, L2 cache, `-Os`) | HIGH | Custom board JSON or Arduino core fork |
| 5 | ~~Double buffering~~ | ~~MEDIUM-HIGH~~ | **REJECTED** (v8) — caused 30→20 fps regression (cache thrashing) |
| 6 | **15 ms refresh period** | FREE | **APPLIED** — `LVGL_REFR_PERIOD_MS 15` |
| 7 | ~~LVGL draw buffer in internal SRAM~~ | ~~MEDIUM~~ | **REVERTED** — with DMA2D, PSRAM buffer = same FPS, frees 115 KB SRAM |
| 8 | ~~No IRAM fast-mem for LVGL~~ | ~~LOW-MEDIUM~~ | **APPLIED** — `LV_ATTRIBUTE_FAST_MEM IRAM_ATTR` |
| 9 | **`-Os` vs `-O2` optimization** | LOW-MEDIUM | Arduino platform limitation |
| 10 | ~~Pixel copy: for-loop vs memcpy~~ | ~~LOW~~ | **SUPERSEDED** — DMA2D replaces CPU copy entirely |

---

## 4. Quick Wins Applied

Changes in `src/boards/esp32-p4-lcd4b/board_overrides.h` and `src/app/lv_conf.h`:

### ✅ Lower refresh period (APPLIED)
```cpp
#define LVGL_REFR_PERIOD_MS 15  // Runtime override, zero FPS cost
```
Applied via `lv_timer_set_period()` in `display_manager.cpp` after display driver
registration.

### ✅ DMA2D async flush (APPLIED — LVGL v9)
```cpp
// In st7703_dsi_driver.cpp pushColors():
esp_lcd_panel_draw_bitmap(panel_handle, x1, y1, x2, y2, data);
// on_color_trans_done ISR callback → lv_display_flush_ready(disp)
```
Replaced synchronous CPU memcpy with hardware DMA2D. The biggest single optimization:
+10 FPS (30→40). See §10 for details.

### ✅ PPA hardware accelerator (APPLIED — LVGL v9)
```cpp
#define LV_USE_PPA 1  // in lv_conf.h
```
ESP32-P4's Pixel Processing Accelerator for hardware-accelerated fills, blits, and
rotations. Requires `LV_DRAW_BUF_STRIDE_ALIGN 1` and `LV_DRAW_BUF_ALIGN 64`.

### ✅ IRAM fast-mem (APPLIED — LVGL v9)
```cpp
#define LV_ATTRIBUTE_FAST_MEM IRAM_ATTR  // in lv_conf.h
```
Places hot LVGL rendering functions in IRAM for single-cycle access.

### ✅ PSRAM draw buffer (APPLIED — LVGL v9 + DMA2D)
```cpp
#define LVGL_BUFFER_PREFER_INTERNAL false
#define LVGL_BUFFER_SIZE (DISPLAY_WIDTH * 80)   // 115 KB in PSRAM
```
With DMA2D flush, PSRAM buffer matches SRAM performance (39–42 FPS). Frees ~115 KB
of internal SRAM for the heap vs. the previous `LVGL_BUFFER_PREFER_INTERNAL true`.

### ❌ Enable double buffering (REJECTED — v8 testing)
```cpp
#define LVGL_DRAW_BUF_COUNT 2  // Caused 30→20 fps regression with CPU memcpy
```
Double buffering helps when the flush is asynchronous. With the old synchronous CPU
memcpy, the second buffer doubled PSRAM cache pressure without pipelining benefit.
May be worth re-testing now that DMA2D provides true async flush.

### ❌ Increase draw buffer size (REJECTED — v8 testing)
```cpp
#define LVGL_BUFFER_SIZE (DISPLAY_WIDTH * 120)  // Increased cache pressure with CPU memcpy
```
Tested alongside double buffering with v8. Combined with the DPI framebuffer, larger
PSRAM buffers exceeded the L2 cache working set. May be worth re-testing with DMA2D.

---

## 5. DSI Timing Reference

```
DPI pixel clock:  38 MHz
DSI lane rate:    480 Mbps × 2 lanes
HSYNC:            pw=20, bp=50, fp=50
VSYNC:            pw=4,  bp=20, fp=20
```

Theoretical frame rate at 38 MHz:
```
H_total = 720 + 20 + 50 + 50 = 840 pixels
V_total = 720 + 4 + 20 + 20 = 764 lines
FPS = 38,000,000 / (840 × 764) ≈ 59.2 fps
```

The DSI panel hardware can display ~59 fps. With `LVGL_REFR_PERIOD_MS 15` applied,
the LVGL timer ceiling (~66 fps) exceeds the panel rate. The bottleneck is LVGL
rendering throughput (measured at ~40 fps on the FPS test screen with DMA2D).

---

## 6. LP (Low Power) Transitions During Blanking — FIXED

Arduino_GFX v1.6.5 does **not** set `disable_lp = true` in `esp_lcd_dpi_panel_config_t.flags`.
With `disable_lp=false` (default), the DSI host enters Low-Power mode during every
horizontal and vertical blanking interval (HBP, HFP, VBP, VFP, VSA). Each LP↔HS
transition has D-PHY ramp latency. On the ST7703 panel this manifested as visible
cyan/idle-color flashes — hundreds of transitions per frame.

**Root cause confirmed:** Reading `Arduino_ESP32DSIPanel.cpp` source showed
`dpi_config.flags` only sets `.use_dma2d = true`, with no API to control `disable_lp`.

**Fix applied (February 2026):** Bypassed Arduino_GFX entirely for DSI init.
The driver (`st7703_dsi_driver.cpp`) now calls ESP-IDF APIs directly:
- `esp_lcd_new_dsi_bus()` → DSI bus
- `esp_lcd_new_panel_io_dbi()` → command channel
- `esp_lcd_new_panel_dpi()` → DPI panel with **`.flags.disable_lp = true`**
- `esp_lcd_panel_io_tx_param()` → vendor init commands
- `esp_lcd_panel_init()` → start DMA refresh
- `esp_lcd_dpi_panel_get_frame_buffer()` → framebuffer pointer for LVGL flush

The Arduino_GFX library is no longer used for the DSI display path. It is still
used by other boards (jc3248w535, jc3636w518) which use QSPI interfaces and are
not affected by DSI LP transitions.

**Result:** Cyan flashes eliminated. Combined with the switch from per-pixel copy
to `memcpy()` (v8) and later hardware DMA2D (v9), this driver change contributed
to the FPS improvements documented in §7.

---

## 7. Empirical Test Results

All tests measured on the FPS test screen (full-screen redraws every frame).

### Phase 1: LVGL v8.4.0 Buffer & Timing Tests (February 2026)

A/B testing to isolate the impact of each v8 configuration change:

| Test | Buffer | Lines | Double | Refresh | FPS | Notes |
|---|---|---|---|---|---|---|
| Baseline | PSRAM | 40 | No | 30 ms | **30** | Original configuration |
| All changes | PSRAM | 120 | Yes | 15 ms | **20** | All "quick wins" applied — 33% regression |
| Isolate A | PSRAM | 40 | Yes | 15 ms | **21** | Buffer size not the cause |
| Isolate B | PSRAM | 40 | No | 15 ms | **30** | Double buffering is the culprit |

**Conclusions:**
- **15 ms refresh period:** Free — no FPS cost. Kept.
- **Double buffering:** −33% FPS. The flush is a synchronous CPU memcpy between two
  PSRAM regions (draw buffer → DPI framebuffer). Both buffers + the 1 MB DPI
  framebuffer compete for L2 cache lines. Reverted.
- **120-line buffer:** Not independently tested. Likely increases cache pressure.

### Phase 1b: Internal RAM draw buffer (v8)

| Test | Buffer Mem | FPS | Notes |
|---|---|---|---|
| PSRAM buffer | PSRAM | **30** | Baseline (with 15ms refresh) |
| Internal RAM | Internal SRAM | **34** | +4 fps — less cache contention |

### Phase 1c: Direct ESP-IDF DSI driver (v8, cumulative)

| Test | DSI Driver | disable_lp | FPS | Notes |
|---|---|---|---|---|
| Arduino_GFX + internal buf | Arduino_GFX | false | **34** | LP flashes still present |
| Direct ESP-IDF + internal buf | ESP-IDF direct | true | **~40** | +6 fps, flashes eliminated |

### Phase 2: LVGL v9.5.0 Upgrade — Optimization Experiments (February 2026)

Systematic testing of v9 features and configurations. All tests use the direct
ESP-IDF DSI driver with `disable_lp=true`, 15ms refresh period.

| # | Config | FPS | Internal Heap | Verdict |
|---|---|---|---|---|
| 1 | v9 baseline + single PSRAM buf (80 lines) | **23** | ~350 KB | v9 slower than v8 with PSRAM |
| 2 | v9 + single SRAM buf (80 lines) | **30** | ~238 KB | SRAM helps; matched v8 |
| 3 | v9 + double SRAM buf (80 lines) | **30** | ~118 KB | No gain, heap waste |
| 4 | v9 + LV_OS_FREERTOS + 2 draw units | **27** | ~238 KB | **REJECTED** — PSRAM contention |
| 5 | v9 + IRAM_ATTR for LV_ATTRIBUTE_FAST_MEM | **30** | ~238 KB | Neutral, kept for correctness |
| 6 | v9 + LVGL task priority 1→4 | **30** | ~238 KB | Neutral, kept (matches NINA) |
| 7 | **v9 + DMA2D flush** (SRAM buf) | **40** | ~238 KB | **+10 FPS!** Key breakthrough |
| 8 | **v9 + DMA2D flush + PSRAM buf** | **39–42** | ~350 KB | **Same FPS, +115 KB heap** |

### Phase 2 Profiling Data (CPU memcpy bottleneck discovery)

Before implementing DMA2D, profiling instrumentation was added to the flush callback.
Results on the FPS screen (full-screen redraws at 30 FPS with SRAM buffer):

```
fps=30  lv_timer=31700us  flush=14800us (270 calls)  render=16900us
```

- **270 flushes/sec ÷ 30 fps = 9 flushes/frame** (80 rows × 9 = 720 = full screen)
- **~14.8 ms flush per frame** = 47% of the ~31.7 ms frame budget
- CPU was spending nearly half its time on `memcpy` + `esp_cache_msync`

After DMA2D (`esp_lcd_panel_draw_bitmap`), the flush still shows ~14 ms in wall-clock
time, but this is **overlap** — the DMA engine runs concurrently with LVGL rendering.
The CPU is only blocked briefly to set up each DMA transfer.

### Summary: Performance Evolution

| Milestone | FPS | Key Change |
|---|---|---|
| v8 original (PSRAM buf, Arduino_GFX) | 30 | Baseline |
| v8 + internal SRAM buffer | 34 | +4 fps (cache contention) |
| v8 + direct ESP-IDF DSI + disable_lp | ~40 | +6 fps (HS mode, memcpy per row) |
| v9 initial port (SRAM buf, CPU memcpy) | 30 | v9 API overhead offset by PPA |
| **v9 + DMA2D flush (PSRAM buf)** | **40–42** | **+10 fps (async hardware copy)** |

---

## 8. Diagnostic Commands

### Check actual PSRAM speed at runtime
```cpp
Serial.printf("PSRAM size: %d bytes\n", ESP.getPsramSize());
Serial.printf("Free PSRAM: %d bytes\n", ESP.getFreePsram());
```

### Measure flush performance
The display manager's perf tracking (`g_perf.fps`, `g_perf.lv_timer_us`) reports:
- **fps:** Actual frames rendered per second
- **lv_timer_us:** Time spent in `lv_timer_handler()` per call (rendering + flush)

If `lv_timer_us` consistently exceeds 15 ms, LVGL can't sustain 66 fps even
with the refresh period lowered.

### LVGL built-in perf monitor (Smart-86-Box approach)
Requires `LV_USE_PERF_MONITOR=1` in `lv_conf.h` — shows FPS and CPU% overlay.
Not available without a custom `lv_conf.h` in Arduino builds.

---

## 9. File References

| File | Role |
|---|---|
| `src/app/drivers/st7703_dsi_driver.cpp` | DSI driver (direct ESP-IDF, DMA2D flush, disable_lp=true) |
| `src/app/drivers/st7703_dsi_driver.h` | Driver header (ESP-IDF handles, DMA2D callback) |
| `src/app/display_driver.h` | Display HAL interface (`asyncFlush()`, `configureLVGL()`) |
| `src/app/display_manager.cpp` | LVGL init, buffer alloc, flush callback, perf stats |
| `src/app/lv_conf.h` | LVGL v9.5.0 config (PPA, IRAM, stride/buf alignment) |
| `src/boards/esp32-p4-lcd4b/board_overrides.h` | P4 board config (buffer sizes, pins, DSI timing) |
| `sample/ESP32-P4-NINA-Display/sdkconfig.defaults` | NINA display sdkconfig |
| `sample/ESP32-P4-NINA-Display/main/main.c` | NINA BSP display init |
| `sample/ESP32-P4-Smart-86-Box-Smarthome-Panel/ESP-IDF/Smarthome-Panel/sdkconfig.defaults` | Smart-86-Box sdkconfig |
| `sample/ESP32-P4-Smart-86-Box-Smarthome-Panel/ESP-IDF/Smarthome-Panel/main/main.c` | Smart-86-Box BSP display init |
| Arduino_GFX `src/databus/Arduino_ESP32DSIPanel.cpp` | Reference: DSI panel bus (disable_lp missing here) |
| Arduino_GFX `src/display/Arduino_DSI_Display.cpp` | Reference: Framebuffer drawing + cache sync |

---

## 10. LVGL v9 Upgrade — DMA2D Flush Architecture

### Background

The LVGL v8.4.0 → v9.5.0 upgrade (branch `poc/lvgl-v9-upgrade`) initially showed
a regression from ~40 FPS to 30 FPS on the P4 board. Profiling revealed that the
CPU memcpy flush path consumed 47% of frame time. The root cause: `pushColors()`
performed a row-by-row `memcpy()` from the LVGL draw buffer to the PSRAM DPI
framebuffer, followed by `esp_cache_msync()`. This was synchronous — the CPU was
blocked for ~14.8 ms per frame.

### Solution: Hardware DMA2D

ESP-IDF's `esp_lcd_panel_draw_bitmap()` was already available. With the DPI panel
config flag `use_dma2d = true` (already set), this function triggers the ESP32-P4's
hardware 2D-DMA engine to copy pixels asynchronously. The implementation:

1. **`pushColors()`** calls `esp_lcd_panel_draw_bitmap(panel_handle, x1, y1, x2, y2, data)`
   instead of manual memcpy. Coordinates use LVGL's inclusive-start + exclusive-end
   convention (x2+1, y2+1 from LVGL's inclusive area).

2. **`configureLVGL()`** (new override) registers an `on_color_trans_done` callback
   via `esp_lcd_dpi_panel_register_event_callbacks()`. The callback runs from ISR
   context and calls `lv_display_flush_ready(disp)` to signal LVGL that the draw
   buffer can be recycled.

3. **`asyncFlush()`** (new `DisplayDriver` virtual method) returns `true` for the
   ST7703 driver. `DisplayManager::flushCallback()` checks this and skips calling
   `lv_display_flush_ready()` synchronously — the DMA callback handles it.

### How `esp_lcd_panel_draw_bitmap()` Works Internally

With `use_dma2d=true`, the function resolves to `dpi_panel_draw_bitmap_2d()` which
has three code paths:

1. **Direct FB write** (sync): If `color_data` points inside the panel's own frame
   buffer, just flush cache and call `on_color_trans_done` immediately.

2. **DMA2D hook** (async): Takes a non-blocking semaphore, sets up
   `esp_async_fbcpy_trans_desc_t` with 2D source/destination descriptors, calls
   `esp_async_fbcpy()`. Returns immediately. DMA2D completion ISR gives back the
   semaphore and fires `on_color_trans_done`.

3. **CPU memcpy fallback** (sync): If DMA2D is not enabled, performs the same
   row-by-row memcpy + cache writeback that our old code did.

All cache management is internal — callers never need `esp_cache_msync()`.

### Key Implementation Details

- **Stride handling**: `esp_lcd_panel_draw_bitmap()` handles stride differences
  between the source buffer (LVGL draw buffer width) and destination (full framebuffer
  width) via 2D DMA descriptors. No manual `flushSrcStride` needed for the copy.

- **Sync guarantee**: LVGL's flush-ready mechanism ensures the next flush won't
  start until the previous DMA transfer completes. The DMA2D hook uses a non-blocking
  `xSemaphoreTake(draw_sem, 0)` — if the previous transfer isn't done, it returns
  `ESP_ERR_INVALID_STATE`.

- **Buffer location**: With DMA2D, the LVGL draw buffer can be in PSRAM or internal
  SRAM with no FPS difference (39–42 FPS either way). We use PSRAM to save ~115 KB
  of internal SRAM.

### Rejected Approaches

| Approach | Result | Why |
|---|---|---|
| **LV_OS_FREERTOS + 2 draw units** | 27 FPS (−3) | PSRAM bandwidth contention — both draw threads + DMA compete for L2 cache |
| **Double SRAM buffers** | 30 FPS, 118 KB heap | No FPS gain (only one buffer can be filled at a time with LV_OS_NONE) |
| **Single PSRAM buffer (no DMA2D)** | 23 FPS | CPU memcpy from PSRAM is slower than from SRAM |
| **LVGL task priority 1→4** | 30 FPS | Not the bottleneck; kept at 4 for correctness |

### Current Configuration (lv_conf.h)

```cpp
#define LV_USE_OS               LV_OS_NONE      // Manual mutex, no LVGL threads
#define LV_DRAW_SW_DRAW_UNIT_CNT  1             // Single draw unit (multi regressed)
#define LV_DRAW_BUF_STRIDE_ALIGN  1             // Required for PPA compatibility
#define LV_DRAW_BUF_ALIGN         64            // P4 L2 cache line alignment
#define LV_USE_PPA                1             // Hardware pixel acceleration
#define LV_ATTRIBUTE_FAST_MEM     IRAM_ATTR     // Hot functions in IRAM
```

### Current Configuration (board_overrides.h)

```cpp
#define LVGL_BUFFER_PREFER_INTERNAL false        // PSRAM — DMA2D makes location irrelevant
#define LVGL_BUFFER_SIZE (DISPLAY_WIDTH * 80)    // 115 KB, 80 lines
#define LVGL_REFR_PERIOD_MS 15                   // ~66 fps target
#define LVGL_TASK_CORE 1                         // Core 1 (Core 0 = WiFi SDIO)
```
