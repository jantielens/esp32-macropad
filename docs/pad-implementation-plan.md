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

## Phase 6: MQTT Live Labels & Toggle State

**Goal:** Subscribe to per-button MQTT topics for live label updates and toggle state.

### Files to modify

| File | Change |
|---|---|
| `src/app/screens/pad_screen.cpp` | Add MQTT subscription management: on tile rebuild, subscribe to all configured `label_*_sub_topic` and `state_topic` values. On message callback: extract value via `json_path`, format via `format` string, update label text. Toggle state: compare to `state_on_value`, set fg color accordingly. Coalescing: track `last_label_update_ms`, defer LVGL updates by `PAD_LABEL_COALESCE_MS` (200ms) |
| `src/app/mqtt_manager.cpp` (or equivalent) | Add routing: pad label/state topics → `pad_screen_on_mqtt_message(page, button_index, topic, payload)` |
| `src/app/web_portal_pad.cpp` | On POST: trigger MQTT resubscribe for the saved page |

### Data flow

```
MQTT broker → mqtt_manager → pad_screen_on_mqtt_message()
  → extract value (json_path)
  → format (printf)
  → set pending_label_update flag + timestamp
  → update() loop: if pending && elapsed > 200ms → lv_label_set_text()
```

### Verification

- Configure a button with `label_center_sub: "home/temp"`, `label_center_fmt: "%.1f°C"`
- Publish `{"value": 21.5}` to `home/temp`
- Verify button center label updates to `"21.5°C"`
- Test toggle: configure `state_topic`, verify button dims when value != `state_on_value`

---

## Phase 7: Background Image Fetch

**Goal:** Buttons with `bg_image_url` fetch JPEG/PNG snapshots and display them as tile backgrounds.

### Files to create

| File | Contents |
|---|---|
| `src/app/screens/button_image_fetch.h` | API: `button_image_fetch_start(url, user, password, interval_ms, tile_w, tile_h, slot_index)`, `button_image_fetch_stop(slot_index)`, `button_image_fetch_get_frame(slot_index, &pixels, &w, &h)`. Frame double-buffer management |
| `src/app/screens/button_image_fetch.cpp` | FreeRTOS task: HTTP GET → decode JPEG/PNG → scale to tile size → write to back buffer → swap. Uses PSRAM for pixel buffers. HTTP Basic Auth support. Interval-based re-fetch. Pauses during screen saver sleep |

### Files to modify

| File | Change |
|---|---|
| `src/app/screens/pad_screen.cpp` | On tile create: if `bg_image_url` set, start fetch for that slot. On `update()`: check for new frame, set as LVGL image source. On tile destroy: stop fetch. On `hide()`: pause all fetches. On `show()`: resume |

### Gating

- Compile-time: `HAS_IMAGE_API` flag (not all boards have enough RAM for image decode)
- Runtime: images are optional per-button; buttons without `bg_image_url` are unaffected

### Verification

- Configure button with `bg_image_url` pointing to a test JPEG
- Verify image appears as button background with text/icon overlaid
- Test refresh interval (set to 5000ms, verify periodic updates)
- Test screen saver: images pause during sleep, resume on wake

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
| `src/app/screens/button_image_fetch.h` | 7 | Image fetch API |
| `src/app/screens/button_image_fetch.cpp` | 7 | HTTP fetch, decode, double buffer |

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
| **PSRAM contention from image decode** | Image fetch runs on a dedicated FreeRTOS task at low priority. Double-buffer prevents LVGL from reading partially-decoded frames. Gated by `HAS_IMAGE_API` |
| **Round display corner buttons** | Hardware clips naturally. No software work needed. May look odd with very large grids (8×8 on 360px round) — but that's a user choice |
