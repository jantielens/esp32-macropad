# Pad Implementation Plan

> Implementation plan for the Pad feature as specified in [feature-specs.md](../feature-specs.md#4-pad).

---

## Overview

The Pad provides multi-page, customizable button layouts that adapt to any display shape, resolution, and pixel density. Two layout types are supported: **grid** (free-form cols×rows, auto-computed tile sizes) and **curated** (hand-optimized, resolution-specific, fixed button count). This plan breaks implementation into 7 phases, each producing a buildable, testable increment.

**Estimated file count:** 10 new files, 6 modified files.

---

## Phase 1: Board Configuration & Fonts

**Goal:** Add `DISPLAY_SHAPE`, `UI_SCALE_TIER`, and `CURATED_LAYOUTS` compile-time defines for all boards and enable required LVGL fonts.

### Files to modify

| File | Change |
|---|---|
| `src/app/board_config.h` | Add `DISPLAY_SHAPE_*` constants (0/1/2), `UI_SCALE_*` tier constants (0–3), defaults: `DISPLAY_SHAPE_RECT`, `UI_SCALE_MEDIUM`. Default `CURATED_LAYOUTS` empty (no curated layouts) |
| `src/app/lv_conf.h` | Enable `LV_FONT_MONTSERRAT_12`, `LV_FONT_MONTSERRAT_32`, `LV_FONT_MONTSERRAT_36` (set to 1) |
| `src/boards/cyd-v2/board_overrides.h` | Add `DISPLAY_SHAPE DISPLAY_SHAPE_RECT`, `UI_SCALE_TIER UI_SCALE_SMALL` |
| `src/boards/jc3248w535/board_overrides.h` | Add `DISPLAY_SHAPE DISPLAY_SHAPE_RECT`, `UI_SCALE_TIER UI_SCALE_SMALL` |
| `src/boards/jc3636w518/board_overrides.h` | Add `DISPLAY_SHAPE DISPLAY_SHAPE_ROUND`, `UI_SCALE_TIER UI_SCALE_MEDIUM`, `CURATED_LAYOUTS "round_7", "round_4"` |
| `src/boards/esp32-4848S040/board_overrides.h` | Add `DISPLAY_SHAPE DISPLAY_SHAPE_SQUARE`, `UI_SCALE_TIER UI_SCALE_MEDIUM`, `CURATED_LAYOUTS "square_hero_5", "square_dash_9"` |
| `src/boards/jc4880p433/board_overrides.h` | Add `DISPLAY_SHAPE DISPLAY_SHAPE_RECT`, `UI_SCALE_TIER UI_SCALE_LARGE` |
| `src/boards/esp32-p4-lcd4b/board_overrides.h` | Add `DISPLAY_SHAPE DISPLAY_SHAPE_SQUARE`, `UI_SCALE_TIER UI_SCALE_XLARGE`, `CURATED_LAYOUTS "square_hero_5_hd", "square_dash_9_hd"` |

### Verification

- `./build.sh` — all boards compile without errors
- Confirm no binary size regression beyond expected font additions (~80KB)

---

## Phase 2: Layout Engine

**Goal:** Pure-computation layout engine that takes display dimensions + layout config → tile rectangles. Supports both grid (auto-computed) and curated (hardcoded positions) layouts. No LVGL dependency — testable in isolation.

### Files to create

| File | Contents |
|---|---|
| `src/app/pad_layout.h` | `PadRect` struct (x, y, w, h, round flag), `PadGridConfig` struct (cols, rows), `PadLayout` struct (array of rects + count), `CuratedButtonRect` struct, `CuratedLayoutDef` struct (name, display_w/h, button_count, rects array), UI scale tier tables (font pointers, gap values, icon height %), `PIXEL_SHIFT_MARGIN` constant, `pad_compute_grid()` function, `pad_find_curated_layout()` lookup function, curated layout registry |

### Key implementation details

```cpp
// PadRect — output of layout computation (used by both grid and curated)
struct PadRect {
    int16_t x, y;
    uint16_t w, h;
    bool round;         // true → use LV_RADIUS_CIRCLE for corner_radius
};

// Grid config — input from page JSON (grid layout only)
struct PadGridConfig {
    uint8_t cols;       // 1–8
    uint8_t rows;       // 1–8
};

// Per-button placement (col/row + span → rect)
// Computed by: pad_compute_grid(config, display_w, display_h, buttons, count, out_rects)

// UI Scale tier tables
struct UIScaleInfo {
    const lv_font_t* font_small;
    const lv_font_t* font_medium;
    const lv_font_t* font_large;
    uint8_t gap;
    uint8_t icon_height_pct;  // % of tile_h
};

static const UIScaleInfo UI_SCALE_TABLE[] = {
    // SMALL:   12, 14, 18, gap=2, icon=40%
    // MEDIUM:  14, 18, 24, gap=3, icon=45%
    // LARGE:   14, 24, 32, gap=3, icon=45%
    // XLARGE:  18, 24, 36, gap=4, icon=50%
};

// Curated layout registry
struct CuratedButtonRect {
    int16_t x, y;
    uint16_t w, h;
    bool round;          // true → corner_radius = LV_RADIUS_CIRCLE
};

struct CuratedLayoutDef {
    const char* name;           // e.g., "round_7"
    uint16_t display_w;         // Target resolution width
    uint16_t display_h;         // Target resolution height
    uint8_t button_count;       // Fixed number of buttons
    bool supports_spanning;     // false for curated layouts
    const CuratedButtonRect* rects;
};

// Lookup function — returns nullptr if layout name not found or resolution mismatch
const CuratedLayoutDef* pad_find_curated_layout(
    const char* name, uint16_t display_w, uint16_t display_h);
```

**Curated layout discovery flow:**
1. Page JSON has `"layout": "round_7"` (not `"grid"`)
2. Call `pad_find_curated_layout("round_7", display_w, display_h)`
3. If found and resolution matches → use `rects[]` array directly (no grid computation)
4. If not found → fall back to grid 3×3

**Board allowlist:** Each board’s `board_overrides.h` declares `CURATED_LAYOUTS` (comma-separated string literals). The REST API validates that the requested layout name is in the board’s allowlist before saving.

### Core algorithm

```
margin = PIXEL_SHIFT_MARGIN (4)
gap = UI_SCALE_TABLE[UI_SCALE_TIER].gap

safe_w = display_w - 2 * margin
safe_h = display_h - 2 * margin

tile_w = (safe_w - (cols - 1) * gap) / cols
tile_h = (safe_h - (rows - 1) * gap) / rows

For each button:
  x = margin + col * (tile_w + gap)
  y = margin + row * (tile_h + gap)
  w = col_span * tile_w + (col_span - 1) * gap
  h = row_span * tile_h + (row_span - 1) * gap
```

### Verification

- `./build.sh` — compiles (header-only, no link issues)
- Manual review of computed values for each board's typical grid
- Manual review of curated layout rects (spot-check pixel positions for a few layouts)

---

## Phase 3: Button Config & FFat Persistence

**Goal:** `ScreenButtonConfig` struct, JSON serialization/deserialization, FFat load/save/delete.

### Files to create

| File | Contents |
|---|---|
| `src/app/pad_config.h` | `ScreenButtonConfig` struct, `PadPageConfig` struct (layout type string, cols, rows, buttons array, button count), `MAX_PAD_PAGES`, `MAX_PAD_BUTTONS`, `MAX_GRID_COLS`, `MAX_GRID_ROWS` constants, load/save/delete function declarations |
| `src/app/pad_config.cpp` | FFat JSON read/write at `/config/pad_N.json`, JSON→struct deserialization, struct→JSON serialization, color string parsing (`#RRGGBB`/`RRGGBB`/`0xRRGGBB`/uint32), default initialization. Layout type stored as string (e.g., `"grid"` or `"round_7"`) |

### Key constants

```cpp
#define MAX_PAD_PAGES      8
#define MAX_PAD_BUTTONS   64
#define MAX_GRID_COLS            8
#define MAX_GRID_ROWS            8
```

### JSON format

**Grid layout:**

```json
{
  "layout": "grid",
  "cols": 4,
  "rows": 4,
  "buttons": [
    {
      "col": 0, "row": 0, "col_span": 1, "row_span": 1,
      "label_top": "...", "label_center": "...", "label_bottom": "...",
      "icon_id": "...",
      "bg_color": "#333333", "fg_color": "#FFFFFF",
      ...
    }
  ]
}
```

**Curated layout** (Phase 2 — buttons used sequentially by index):

```json
{
  "layout": "round_7",
  "buttons": [
    { "label_center": "Home", "icon_id": "mi_home", "bg_color": "#1A5276", ... },
    { "label_center": "Lights", "icon_id": "mi_lightbulb", ... },
    ...
  ]
}
```

Curated layouts have no `cols`/`rows` fields. The layout name determines button count and positions. Extra buttons beyond the layout’s `button_count` are ignored. `col`/`row`/`col_span`/`row_span` fields are ignored.

### Verification

- `./build.sh` — compiles
- Test: save a page config via serial command or test harness, read it back, verify fields match

---

## Phase 4: REST API

**Goal:** `GET/POST/DELETE /api/pad?page=N` endpoint.

### Files to create

| File | Contents |
|---|---|
| `src/app/web_portal_pad.cpp` | REST API handlers: GET (load from FFat, serialize to JSON response), POST (parse JSON body, validate, save, preload icons, bump `ui_generation`), DELETE (remove file, reset to defaults). Input validation: page 0–7, layout type (`"grid"` or valid curated layout name from board’s `CURATED_LAYOUTS` allowlist), cols/rows 1–8 (grid only), button col/row within grid bounds (grid only) |

### Files to modify

| File | Change |
|---|---|
| `src/app/web_portal_config.cpp` (or equivalent web portal setup) | Register `/api/pad` route handlers |

### Validation rules (POST)

- `page` query param: 0 ≤ N < `MAX_PAD_PAGES`
- `layout`: must be `"grid"` or a curated layout name present in the board's `CURATED_LAYOUTS` allowlist. Reject unknown layout names with HTTP 400
- **Grid layout validation:**
  - `cols`: 1–`MAX_GRID_COLS`
  - `rows`: 1–`MAX_GRID_ROWS`
  - Each button: `col` < cols, `row` < rows, `col + col_span` ≤ cols, `row + row_span` ≤ rows
- **Curated layout validation:**
  - `cols`/`rows` fields ignored (not required)
  - Button count capped at layout's `button_count` (extra buttons silently ignored)
- Color fields: valid hex or uint32
- String fields: truncated to max length (not rejected)
- Body size: limited by `WEB_PORTAL_CONFIG_MAX_JSON_BYTES` (may need increase for 64-button pages)

### Verification

- `./build.sh` — compiles
- Manual test: `curl -X POST http://<device>/api/pad?page=1 -d '{"layout":"grid","cols":3,"rows":3,"buttons":[...]}'`
- `curl http://<device>/api/pad?page=1` returns saved config

---

## Phase 5: Pad Screen (LVGL)

**Goal:** LVGL screen that renders buttons (grid or curated layout) with icons, labels, colors, tap/long-press actions, and screen navigation.

### Files to create

| File | Contents |
|---|---|
| `src/app/screens/pad_screen.h` | `PadScreen` class extending `Screen`. Constructor takes page index. Members: page config, computed rects, LVGL object pointers per button, `ui_generation` cache, runtime label state |
| `src/app/screens/pad_screen.cpp` | `create()`: allocate LVGL screen + content container. `update()`: check `ui_generation`, rebuild tiles if changed. Tile creation: determine layout type — if `"grid"`, call `pad_compute_grid()` for rects; if curated, call `pad_find_curated_layout()` for hardcoded rects. For each button with content, create `lv_obj` at computed rect, set bg/border/radius styles (curated layouts may set `LV_RADIUS_CIRCLE` for round buttons), create up to 3 label objects (top/center/bottom) with tier-appropriate fonts, create icon image if `icon_id` set (via `icon_store_lookup()`), register click/long-press event handlers. `show()`/`hide()`: standard LVGL screen load. Pixel shift: offset content container by `screen_saver_manager_get_pixel_shift()` in `update()` |
| `src/app/screens/button_runtime.h` | Shared helpers: `button_tap_flash()` (30% brighter for 100ms timer), `button_apply_toggle_state()` (set fg color based on ON/OFF), `button_fire_ha_event()` (publish structured JSON event), `button_navigate()` (call `display_manager_show_screen()`) |

### Files to modify

| File | Change |
|---|---|
| `src/app/screens.cpp` | Add `#include "screens/pad_screen.cpp"` |
| `src/app/display_manager.h` | Add `PadScreen` instances (array of `MAX_PAD_PAGES`), register in `availableScreens[]` with IDs `"pad_0"` through `"pad_7"` |
| `src/app/display_manager.cpp` | Instantiate pad screens, register them, wire up `showScreen()` for pad IDs |

### Button rendering layout (within each tile)

```
┌────────────────────────┐
│  label_top  (Font S)   │  ← top-aligned, centered
│                        │
│     [icon]             │  ← centered, tier % of tile_h
│  or label_center (L)   │
│                        │
│  label_bottom (Font S) │  ← bottom-aligned, centered
└────────────────────────┘
```

### Event handling

- **Tap** (`LV_EVENT_SHORT_CLICKED`): publish MQTT → fire HA event → tap flash → navigate
- **Long press** (`LV_EVENT_LONG_PRESSED`): publish MQTT → fire HA event → tap flash → navigate (using `lp_action_*` fields)
- **MQTT publish**: gated by `HAS_MQTT` compile flag
- **Navigation**: `display_manager_show_screen(action_screen_id)` — works for any registered screen ID

### Verification

- `./build.sh` — compiles for all boards
- Flash to esp32-4848S040: POST a 3×3 grid config via REST API, verify tiles render correctly
- Test tap → MQTT publish (if MQTT configured) or just tap flash
- Test navigation between pad pages

---

## Phase 6: MQTT Live Labels & Binding-Based Colors

**Goal:** Subscribe to per-button MQTT topics for live label updates and binding-driven dynamic colors.

### Files to modify

| File | Change |
|---|---|
| `src/app/screens/pad_screen.cpp` | Add MQTT subscription management: on tile rebuild, subscribe to all configured `label_*_sub_topic` values and color binding topics. On message callback: extract value via `json_path`, format via `format` string, update label text. Color bindings: resolve binding template (e.g. `[expr:[mqtt:topic;path]>50?"#ff0000":"#00ff00"]`), parse hex result, apply to LVGL style. Coalescing: track `last_label_update_ms`, defer LVGL updates by `PAD_LABEL_COALESCE_MS` (200ms) |
| `src/app/mqtt_sub_store.cpp` | Scan color fields for binding topics during subscription collection |
| `src/app/pad_config.cpp/h` | Color fields stored as `char[192]` strings (hex or binding) with `uint32_t _default` fallback; shared `parse_hex_color()` utility |
| `src/app/web_portal_pad.cpp` | On POST: trigger MQTT resubscribe for the saved page |

### Data flow

```
MQTT broker → mqtt_manager → binding_template resolve()
  → color binding: resolve template → parse hex → apply lv_obj_set_style_bg/text/border_color
  → label binding: extract value (json_path) → format (printf) → set pending_label_update flag
  → update() loop: if pending && elapsed > 200ms → lv_label_set_text()
```

### Verification

- Configure a button with `label_center_sub: "home/temp"`, `label_center_fmt: "%.1f°C"`
- Publish `{"value": 21.5}` to `home/temp`
- Verify button center label updates to `"21.5°C"`
- Test color binding: configure `bg_color` as `[expr:[mqtt:topic;val]>50?"#ff0000":"#00ff00"]`, verify color changes with MQTT data

---

## Phase 7: Background Image Fetch

**Goal:** Buttons with `bg_image_url` fetch JPEG/PNG snapshots and display them as tile backgrounds. Reusable image decode module for future webcam screen.

### Architecture

**Two modules:**

| Module | Responsibility |
|---|---|
| `image_decoder.h/cpp` | Stateless decode+scale: JPEG/PNG → RGB565 cover-mode scaled pixel buffer. No FreeRTOS dependency. Reusable by future webcam screen |
| `image_fetch.h/cpp` | Slot-based fetch manager: single FreeRTOS task, HTTP download, round-robin, double-buffered PSRAM frames, pause/resume |

**Fetch pattern:** Single FreeRTOS task + round-robin through active slots. One HTTP connection at a time — avoids WiFi contention. Initial page load is progressive (images appear one-by-one).

**Decode pattern:** Download raw bytes to PSRAM temp buffer → detect format from magic bytes → decode to intermediate RGB888/RGB565 → cover-mode scale+crop to target tile size → write RGB565 to back buffer → atomic pointer swap.

### Files to create

| File | Contents |
|---|---|
| `src/app/image_decoder.h` | `image_decode_to_rgb565(data, len, target_w, target_h, &out_pixels, &out_size)` — returns PSRAM-allocated RGB565 buffer. Format auto-detected from magic bytes (`FF D8` = JPEG, `89 50 4E 47` = PNG) |
| `src/app/image_decoder.cpp` | JPEG decode via `tjpgd` (LVGL built-in), PNG decode via `lodepng` (LVGL built-in). Called as direct C library calls, not via LVGL's decoder pipeline. Cover-mode scale: `scale = max(tw/sw, th/sh)`, bilinear resample, center-crop to `target_w × target_h`. For JPEG, uses decoder's built-in downscale (1/2, 1/4, 1/8) to get close to target size first — saves decode RAM for large source images |
| `src/app/image_fetch.h` | Slot-based API: `image_fetch_init()`, `image_fetch_request(slot_id, url, user, pass, target_w, target_h, interval_ms)`, `image_fetch_cancel(slot_id)`, `image_fetch_pause()`, `image_fetch_resume()`, `image_fetch_has_new_frame(slot_id)`, `image_fetch_get_frame(slot_id)` → `lv_img_dsc_t*`. Struct: `ImageSlot` (url, auth, target size, interval, front/back pixel buffers, new_frame flag, last_fetch_ms) |
| `src/app/image_fetch.cpp` | Single FreeRTOS task created via `rtos_create_task_psram_stack_pinned()`. Round-robin: find next active slot where `millis() - last_fetch_ms >= interval_ms` (or `interval_ms == 0` and never fetched). HTTP GET via `HTTPClient` + `WiFiClientSecure` (for HTTPS). Basic auth via `http.setAuthorization()`. Download to PSRAM temp buffer → `image_decode_to_rgb565()` → copy to back buffer → atomic pointer swap → set `new_frame = true`. On error: log and skip to next slot. Pause: set flag, task `vTaskDelay`s in a loop. Resume: clear flag. |

### Files to modify

| File | Change |
|---|---|
| `src/app/pad_config.h` | Add 4 fields to `ScreenButtonConfig`: `bg_image_url[256]`, `bg_image_user[32]`, `bg_image_password[64]`, `bg_image_interval_ms`. Add max-length constants |
| `src/app/pad_config.cpp` | Parse/serialize 4 new JSON fields (`bg_image_url`, `bg_image_user`, `bg_image_password`, `bg_image_interval_ms`) |
| `src/app/screens/pad_screen.h` | Add `int8_t image_slot` to `ButtonTile` struct (−1 = no image). Add `lv_obj_t* bg_image` LVGL image widget pointer |
| `src/app/screens/pad_screen.cpp` | On tile create: if `bg_image_url[0]`, call `image_fetch_request()`. On `update()`: poll `image_fetch_has_new_frame()`, update LVGL image source. On tile destroy: `image_fetch_cancel()`. On `hide()`: `image_fetch_pause()`. On `show()`: `image_fetch_resume()` + re-request for all active slots. On config save: cancel old slots, request new |
| `src/app/board_config.h` | Add `HAS_IMAGE_FETCH` flag (default `true` for `HAS_DISPLAY` boards) |
| `src/app/lv_conf.h` | Enable `LV_USE_TJPGD 1` and `LV_USE_LODEPNG 1` |
| `src/app/screen_saver_manager.cpp` | On sleep: call `image_fetch_pause()`. On wake: call `image_fetch_resume()` if pad page is active |
| `src/app/web/home.html` | Add "Image Background" section to button editor dialog: URL, Auth User, Auth Password, Refresh Interval fields |
| `src/app/web/portal.js` | Add `bg_image_url`, `bg_image_user`, `bg_image_password`, `bg_image_interval_ms` to form builder, `buildConfigFromForm()`, and `loadConfig()` |
| `src/app/web_portal_pad.cpp` | Image fields auto-serialize via existing raw JSON preservation — no changes needed unless we add server-side validation |

### Image decode pipeline

```
HTTP response body (raw JPEG/PNG bytes, PSRAM temp buffer)
    │
    ├─ Magic byte detection: FF D8 → JPEG, 89 50 4E 47 → PNG
    │
    ├─ JPEG path:
    │   ├─ Get image info (width, height) via tjpgd JD_PREPARE
    │   ├─ Select best built-in downscale (1/1, 1/2, 1/4, 1/8) to get
    │   │   closest-to-target dimensions without going below target
    │   ├─ Decode to RGB888 intermediate buffer (PSRAM)
    │   └─ Cover-mode scale + RGB888→RGB565 in one pass
    │
    ├─ PNG path:
    │   ├─ lodepng_decode32() → RGBA8888 intermediate buffer (PSRAM)
    │   └─ Cover-mode scale + RGBA→RGB565 in one pass (alpha discarded)
    │
    └─ Output: target_w × target_h RGB565 buffer (PSRAM)
```

### Cover-mode scaling algorithm

```
// CSS object-fit: cover — fill target, center-crop excess
src_aspect = (float)src_w / src_h
dst_aspect = (float)target_w / target_h

if (src_aspect > dst_aspect) {
    // Source is wider → scale by height, crop sides
    scale = (float)target_h / src_h
    scaled_w = src_w * scale    // > target_w
    crop_x = (scaled_w - target_w) / 2
    crop_y = 0
} else {
    // Source is taller → scale by width, crop top/bottom
    scale = (float)target_w / src_w
    scaled_h = src_h * scale    // > target_h
    crop_x = 0
    crop_y = (scaled_h - target_h) / 2
}

// Bilinear resample from src → target with (crop_x, crop_y) offset
// Combined with colorspace conversion (RGB888→RGB565) in one pass
```

### Double-buffer frame handoff

```
ImageSlot:
    uint16_t* front_buf;     // Read by LVGL (atomic pointer)
    uint16_t* back_buf;      // Written by fetch task
    volatile bool new_frame;  // Set by fetch task, cleared by update()
    lv_img_dsc_t img_dsc;    // LVGL descriptor pointing to front_buf

Fetch task:
    decode to back_buf
    swap(front_buf, back_buf)    // Atomic pointer swap
    new_frame = true

LVGL update():
    if (new_frame) {
        img_dsc.data = (uint8_t*)front_buf
        lv_image_set_src(bg_image_obj, &img_dsc)
        new_frame = false
    }
```

### Lifecycle state machine

| Event | Action |
|-------|--------|
| **Pad page shown** | `image_fetch_resume()`, re-request all slots for visible page. Old frames display immediately |
| **Pad page hidden** | `image_fetch_pause()` — task stops cycling, HTTP connections close. PSRAM buffers kept |
| **Screen saver activates** | `image_fetch_pause()` — no background fetches during sleep |
| **Screen saver deactivates** | `image_fetch_resume()` if pad page is still the active screen |
| **Config save (POST /api/pad)** | Cancel old slots for page → rebuild tiles → request new slots for buttons with URLs |
| **Page delete** | Cancel all slots for that page, free PSRAM buffers |
| **Tile destruction** | `image_fetch_cancel(slot)` — frees both front and back buffers |

### Web UI fields

Add "Image Background" collapsible section to button editor dialog:

| Field | Input type | Placeholder | Helper text |
|-------|-----------|-------------|-------------|
| Image URL | text | `http://camera.local/snapshot.jpg` | JPEG or PNG snapshot URL |
| Auth User | text | _(empty)_ | Optional HTTP Basic Auth |
| Auth Password | password | _(empty)_ | Optional |
| Refresh Interval (ms) | number | `0` | 0 = fetch once, 5000 = every 5s |

### Gating

- Compile-time: `HAS_IMAGE_FETCH` flag (default `true` for `HAS_DISPLAY` boards)
- LVGL decoders: `LV_USE_TJPGD 1`, `LV_USE_LODEPNG 1` in lv_conf.h
- Runtime: images are optional per-button; buttons without `bg_image_url` are unaffected
- No external Arduino libraries needed — tjpgd and lodepng ship with LVGL 9.5

### PSRAM budget

| Board | Grid | Tile W×H | Per slot (2× RGB565) | 16 slots |
|-------|------|----------|---------------------|----------|
| cyd-v2 | 4×3 | 76×76 | 23 KB | 368 KB |
| jc3248w535 | 3×5 | 102×92 | 37 KB | 592 KB |
| esp32-4848S040 | 4×4 | 115×115 | 53 KB | 848 KB |
| jc4880p433 | 3×5 | 155×156 | 97 KB | 1.5 MB |
| esp32-p4-lcd4b | 4×4 | 175×175 | 122 KB | 2.0 MB |

Plus ~100–200 KB temporary decode buffer per fetch. All boards have ≥8 MB PSRAM — no concern.

### Verification

- `./build.sh` — compiles for all boards
- Test: configure button with `bg_image_url` pointing to a test JPEG (e.g., `http://<server>/test.jpg`)
- Verify image appears scaled to fill button (cover mode, no letterboxing)
- Test PNG URL → verify format auto-detection works
- Test refresh interval (set to 5000ms, verify periodic updates in serial log)
- Test page navigation: images survive hide/show, display old frame immediately on return
- Test screen saver: verify no HTTP activity in sleep (serial log), fetches resume on wake
- Test web UI: configure image URL via button editor, save, verify fetch starts

---

## Implementation Order & Dependencies

```
Phase 1: Board Config & Fonts
    │
    ▼
Phase 2: Layout Engine ◄── no LVGL dependency, pure computation
    │
    ▼
Phase 3: Button Config & FFat ◄── no LVGL dependency
    │
    ├──────────────────┐
    ▼                  ▼
Phase 4: REST API    Phase 5: LVGL Screen ◄── depends on 2 + 3
    │                  │
    └──────┬───────────┘
           ▼
    Phase 6: MQTT Live Labels ◄── depends on 4 + 5
           │
           ▼
    Phase 7: Background Images ◄── depends on 5
```

Phases 4 and 5 can be developed in parallel after Phase 3 is complete.

---

## Files Summary

### New files (10)

| File | Phase | Description |
|---|---|---|
| `src/app/pad_layout.h` | 2 | Grid layout engine, UI scale tables, tile rect computation |
| `src/app/pad_config.h` | 3 | Config structs, constants, load/save API |
| `src/app/pad_config.cpp` | 3 | FFat JSON I/O, color parsing |
| `src/app/web_portal_pad.cpp` | 4 | REST API handlers |
| `src/app/screens/pad_screen.h` | 5 | Screen class declaration |
| `src/app/screens/pad_screen.cpp` | 5 | LVGL rendering, events, runtime updates |
| `src/app/screens/button_runtime.h` | 5 | Shared button helpers (tap flash, HA events, navigation) |
| `src/app/image_decoder.h` | 7 | Stateless JPEG/PNG decode + cover-mode scale API |
| `src/app/image_decoder.cpp` | 7 | tjpgd + lodepng decode, bilinear scale, RGB565 output |
| `src/app/image_fetch.h` | 7 | Slot-based image fetch API (request, cancel, pause, resume, poll) |
| `src/app/image_fetch.cpp` | 7 | Single FreeRTOS task, round-robin, HTTP client, double-buffered PSRAM |

### Modified files (6+)

| File | Phase | Change |
|---|---|---|
| `src/app/board_config.h` | 1 | `DISPLAY_SHAPE_*`, `UI_SCALE_*` constants and defaults |
| `src/app/lv_conf.h` | 1 | Enable Montserrat 12, 32, 36 |
| `src/boards/*/board_overrides.h` | 1 | Per-board `DISPLAY_SHAPE` and `UI_SCALE_TIER` |
| `src/app/screens.cpp` | 5 | Include pad_screen.cpp |
| `src/app/display_manager.h` | 5 | PadScreen instances, screen registry |
| `src/app/display_manager.cpp` | 5 | Instantiation, registration |
| `src/app/web_portal_*.cpp` | 4 | Route registration |
| `src/app/mqtt_manager.cpp` | 6 | Pad MQTT message routing |

---

## Risk Areas

| Risk | Mitigation |
|---|---|
| **RAM usage with 64-button pages** | Buttons stored on FFat, only active page's LVGL objects in RAM. `ScreenButtonConfig` is large (~1.5KB per button) but only deserialized into a temporary buffer during JSON parse |
| **Flash size from 3 new fonts** | ~80KB for Montserrat 12+32+36. All boards have ≥16MB flash — acceptable |
| **JSON body size for 64-button POST** | May exceed current `WEB_PORTAL_CONFIG_MAX_JSON_BYTES` (4096). Increase or use chunked/streaming parse. A 64-button page with all fields is ~50KB; realistic configs (10-20 buttons, sparse fields) are 2-8KB |
| **PSRAM contention from image decode** | Single fetch task at low priority. Round-robin avoids parallel HTTP connections. Double-buffer with atomic pointer swap prevents LVGL from reading partially-decoded frames. Gated by `HAS_IMAGE_FETCH` |
| **Large source images** | JPEG decoder's built-in 1/2, 1/4, 1/8 downscale reduces intermediate buffer size. Temporary decode buffer freed immediately after scale+copy |
| **esp_jpeg not on ESP32-P4** | Using LVGL's built-in tjpgd (same underlying engine) which is available on all platforms |
| **Round display corner buttons** | Hardware clips naturally. No software work needed. May look odd with very large grids (8×8 on 360px round) — but that's a user choice |
