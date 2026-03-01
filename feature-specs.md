# Feature Specifications

> **Purpose:** Agent-optimized specifications for reproducing features in other ESP32/LVGL codebases.
> Behavior rules are **normative** (must match). Everything else is **informative** (guidance — adapt as needed).
>
> **Reference implementation:** This repository. Agents should read the referenced source files for implementation details, struct layouts, and patterns beyond what's specified here.

---

## Table of Contents

- [1. Screen Saver & Burn-in Prevention](#1-screen-saver--burn-in-prevention)
- [2. Icon Store](#2-icon-store)
- [3. Energy Monitor](#3-energy-monitor)
- [4. Pad](#4-pad)

---

## 1. Screen Saver & Burn-in Prevention

### Intent

Protect LCD panels from burn-in and reduce power draw by fading the backlight to zero after a period of inactivity. A pixel-shift mechanism ensures that even content displayed for long durations doesn't leave permanent ghosts on the panel. The system must be safe across multi-task environments (LVGL task, AsyncTCP task, main Arduino loop).

### Prerequisites

| Requirement | Details |
|---|---|
| Feature flag | `HAS_DISPLAY` (compile-time) |
| Optional flag | `HAS_TOUCH` — enables wake-on-touch |
| Optional flag | `HAS_IMAGE_FETCH` — pauses background image fetching during sleep |
| Display HAL | Must expose `setBacklightBrightness(uint8_t 0-100)` or at minimum `setBacklight(bool)` |
| LVGL | `lv_layer_top()` for sleep overlay, `lv_timer_create` for scheduled callbacks |
| FreeRTOS | `portMUX_TYPE` spinlocks for cross-task signaling |

### Key Concepts

| Term | Meaning |
|---|---|
| **ScreenSaverState** | Enum: `Awake`, `FadingOut`, `Asleep`, `FadingIn` |
| **Sleep overlay** | A full-screen black LVGL object on `lv_layer_top()` that forces all LCD pixels to black while the backlight is off (reduces LC stress on RGB panels) |
| **Pixel shift** | ±4 px content offset through a coprime-stride pattern covering 81 unique positions in a 9×9 grid. Advanced each time the display enters `Asleep` |
| **Coprime stride** | Step size of 34 through 81 positions (34 is coprime to 81), guaranteeing all positions are visited before repeating |
| **Activity** | Any user interaction or API call that resets the idle timer |

**Configuration defaults:**

| Setting | Type | Default | Range |
|---|---|---|---|
| `screen_saver_enabled` | bool | `true` | — |
| `screen_saver_timeout_seconds` | uint16 | 300 | 0 = disabled |
| `screen_saver_fade_out_ms` | uint16 | 800 | 0 = instant |
| `screen_saver_fade_in_ms` | uint16 | 400 | 0 = instant |
| `screen_saver_wake_on_touch` | bool | `true` | — |
| `backlight_brightness` | uint8 | 100 | 0–100 |

### User-Facing Behavior

1. **Auto-sleep**: When enabled and the idle timer exceeds `timeout_seconds`, the backlight fades from current brightness to 0 over `fade_out_ms`. Once fully off, the state transitions to `Asleep`.
2. **Sleep overlay**: When entering `Asleep`, a full-screen black overlay is created on `lv_layer_top()` so the RGB panel physically drives black pixels. This is removed *before* the backlight rises during wake, giving LVGL time to redraw real content while the backlight is still at 0.
3. **Wake**: On wake trigger, the overlay is removed, then the backlight fades from 0 to `backlight_brightness` over `fade_in_ms`. State transitions: `Asleep` → `FadingIn` → `Awake`.
4. **Touch wake**: If `wake_on_touch` is enabled, raw touch state is polled only while `Asleep` or `FadingOut`. A press-edge triggers wake. Touch input is suppressed in LVGL for `fade_in_ms + 250ms` to prevent wake gestures from clicking through to UI elements.
5. **Touch suppression while not awake**: While in any state other than `Awake`, LVGL touch input is force-released. This prevents phantom clicks during transitions.
6. **Pixel shift**: Each time the display enters `Asleep`, the pixel shift counter advances by the coprime stride (34). All screens read the current offset `(dx, dy)` and shift their content container accordingly. The shift range is ±4 pixels on both axes.
7. **Explicit controls**: `sleep_now()` and `wake()` can be called from any task. Requests are queued via spinlock-protected flags and processed in the main loop. Wake takes priority if both sleep and wake are requested simultaneously.
8. **Brightness sync**: On init, the manager syncs with the display driver's current brightness to avoid a visual jump.
9. **Feature disable at runtime**: If the user disables the screen saver while the display is sleeping/fading, the display immediately wakes.
10. **0ms fade**: If fade duration is 0, brightness change is applied instantly with no intermediate states.

### Configuration

Exposed via `GET/POST /api/config`:

| JSON field | Maps to | Notes |
|---|---|---|
| `screen_saver_enabled` | `.screen_saver_enabled` | |
| `screen_saver_timeout_seconds` | `.screen_saver_timeout_seconds` | |
| `screen_saver_fade_out_ms` | `.screen_saver_fade_out_ms` | |
| `screen_saver_fade_in_ms` | `.screen_saver_fade_in_ms` | |
| `screen_saver_wake_on_touch` | `.screen_saver_wake_on_touch` | |
| `backlight_brightness` | `.backlight_brightness` | Applied immediately on POST; also resets idle timer |

### Integration Points

| Interface | Details |
|---|---|
| **Screen content** | All screens should read `screen_saver_manager_get_pixel_shift(dx, dy)` and offset their content container by `(dx, dy)` pixels |
| **Activity notification** | Any module that handles user interaction should call `notify_activity(bool wake)` — `wake=true` from sleep, `wake=false` to just reset idle timer |
| **REST API** | `GET /api/health` reports screen saver status (enabled, state, brightness, seconds_until_sleep) |
| **MQTT wake** | Optional `mqtt_wake_topic`/`mqtt_wake_payload` — on matching message, calls wake |
| **Image fetch** | When entering sleep, pause background image fetching; resume on wake |

### Reference Implementation

| File | Role |
|---|---|
| [src/app/screen_saver_manager.h](src/app/screen_saver_manager.h) | Public API + no-op stubs when `!HAS_DISPLAY` |
| [src/app/screen_saver_manager.cpp](src/app/screen_saver_manager.cpp) | Full implementation — state machine, fade interpolation, pixel shift, cross-task signaling |

### Design Notes

- **Cross-task safety**: API calls (`wake`, `sleep`, `notify_activity`) come from the AsyncTCP task but LVGL operations must run on the main loop. We use a spinlock + pending-flag pattern: the API sets a flag, the main loop drains it. This is more robust than FreeRTOS queues for this use case (flags are idempotent; duplicate events collapse naturally).
- **Linear fade interpolation**: We use simple linear brightness fade. Gamma correction is left to the display driver if needed.
- **Coprime stride for pixel shift**: The stride of 34 over 81 positions guarantees every position is visited exactly once per full cycle. This is simpler and more uniform than random offsets.
- **Sleep overlay on RGB panels**: Without it, the last-rendered frame stays visible as faint ghosting even with the backlight off. Driving black pixels eliminates LC stress. This may be unnecessary on SPI-based displays.

---

## 2. Icon Store

### Intent

Provide a runtime icon management system that lets users install emoji and Material Icons onto the device via a web API, store them persistently on a FAT filesystem, and render them in LVGL UI elements. The system must handle the constraint that LVGL render tasks often run on PSRAM-backed stacks which cannot safely perform flash I/O.

### Prerequisites

| Requirement | Details |
|---|---|
| Feature flag | `HAS_DISPLAY` (compile-time) |
| Filesystem | FFat partition (FAT on flash) with enough space for icons |
| Memory | PSRAM strongly recommended for cache (each 48×48 icon ≈ 7 KB) |
| LVGL | `lv_img_dsc_t` with `LV_IMG_CF_TRUE_COLOR_ALPHA` format |

### Key Concepts

| Term | Meaning |
|---|---|
| **ICN1** | Custom binary icon format. Header: 4-byte magic `"ICN1"`, 2-byte LE width, 2-byte LE height, 1-byte format (1 = RGB565+Alpha, 3 bytes/pixel), 3 reserved bytes, 4-byte LE data length. Followed by raw pixel data |
| **IconKind** | `Color` (emoji — true-color + alpha, no recolor) or `Mono` (Material Icons — monochrome + alpha, LVGL `img_recolor` applied for tinting) |
| **Icon ID** | Lowercase alphanumeric + underscore string. Prefix `emoji_` for color, `mi_` for mono. Stored at `/icons/<id>.bin` |
| **Non-evicting cache** | In-memory cache that never frees entries because LVGL image objects hold raw pointers to cache data. Freeing would corrupt rendering |
| **Negative cache** | Small ring of IDs known to be unavailable, suppressing repeated log warnings from the LVGL task |

**Constants:**

| Constant | Value | Notes |
|---|---|---|
| `ICON_STORE_CACHE_SLOTS` | 80 (PSRAM) / 4 (no PSRAM) | Non-evicting |
| `CONFIG_ICON_ID_MAX_LEN` | 32 | |
| Max icon dimensions | 256×256 | |
| Max blob size | 256 KB | |
| Safe icon ID chars | `[a-z0-9_]+` | |

### User-Facing Behavior

1. **Icon installation**: Client converts a PNG to ICN1 format (browser-side), uploads via `POST /api/icons/install?id=<icon_id>`. The blob is validated, written to FFat at `/icons/<id>.bin`, and immediately cached in memory (no reboot needed).
2. **Icon lookup**: When a UI element references an `icon_id`, the system checks the in-memory cache first, then attempts to load from FFat. If both fail, a fallback logo icon is displayed.
3. **PSRAM-stack safety**: FFat reads disable the SPI flash cache, which crashes if the calling task's stack is in PSRAM. The store refuses FFat reads from PSRAM-stacked tasks (like the LVGL render task) and adds the icon to the negative cache. Icons must be pre-warmed from an SRAM-stacked task (e.g., Arduino `setup()`).
4. **Preload at boot**: On startup, all `icon_id` values referenced in the device configuration and all pad page JSON files are collected, deduplicated, and loaded into cache from FFat.
5. **Preload on save**: When pad configs are saved via the REST API, icons for the saved page are immediately cached so they're available to the LVGL task without a reboot.
6. **Garbage collection**: `POST /api/icons/gc` scans all referenced icon IDs across config and pad files, then deletes any `/icons/*.bin` files with `emoji_` or `mi_` prefix that are not referenced. Reports count deleted and bytes freed.
7. **Icon listing**: `GET /api/icons/installed` returns a JSON array of installed icon IDs from FFat.
8. **Icon kind detection**: IDs prefixed with `mi_` are treated as `Mono` (LVGL recolor tint is applied). All others are treated as `Color` (rendered as-is).
9. **Concurrency**: Only one install or GC operation can run at a time (spinlock guard). Concurrent requests receive a "busy" error.

### Configuration

Icons are not stored in NVS. They are referenced by `icon_id` strings in:
- `EnergyConsumerConfig.icon_id` — per-consumer indicator icon
- `ScreenButtonConfig.icon_id` — per-button icon (pad and energy screen)

### Integration Points

| Interface | Details |
|---|---|
| **REST API** | `POST /api/icons/install?id=<id>` — install ICN1 blob |
| | `GET /api/icons/installed` — list installed icons |
| | `POST /api/icons/gc` — garbage collect unused icons |
| **LVGL rendering** | `icon_store_lookup(id, &ref)` returns `lv_img_dsc_t*` + `IconKind`; caller sets `lv_img_set_src()` and conditionally applies `img_recolor` based on kind |
| **Boot sequence** | Call `icon_store_preload(config)` from `setup()` after FFat is mounted |
| **Pad save** | Call `icon_store_preload_buttons(buttons, count)` after saving page config |

### Reference Implementation

| File | Role |
|---|---|
| [src/app/icon_store.h](src/app/icon_store.h) | Public API, ICN1 format documentation, `IconKind`/`IconRef` types |
| [src/app/icon_store.cpp](src/app/icon_store.cpp) | Cache, FFat I/O, install, GC, preload, PSRAM-stack guard |

### Design Notes

- **Non-evicting cache is a hard constraint**, not laziness. LVGL `lv_img` objects hold raw `const uint8_t*` pointers and don't copy data. Any eviction would need to also update every LVGL object referencing that icon — which LVGL doesn't support.
- **Client-side conversion** keeps the firmware simple and saves flash wear. The browser has access to Canvas APIs and can efficiently convert PNGs to the fixed RGB565+Alpha format.
- **PSRAM stack detection** (`esp_ptr_external_ram`) is the safety net. The preload pattern is the expected code path; the runtime check only catches programming errors.
- **Negative cache** prevents log spam. The LVGL task retries icon lookups every frame (~30 Hz), so without it, a missing icon would generate hundreds of warnings per second.

---

## 3. Energy Monitor

### Intent

Visualize real-time household energy flow across three categories (Solar generation, Home consumption, Grid import/export) with color-coded status indicators, bar graphs, and an animated alarm system when power exceeds configured thresholds. Values arrive via MQTT and are displayed on an LVGL screen with up to 5 auxiliary consumer indicators.

### Prerequisites

| Requirement | Details |
|---|---|
| Feature flag | `HAS_DISPLAY` (compile-time) |
| Optional flag | `HAS_MQTT` — for receiving energy values |
| LVGL | Image objects for category icons, labels, manual bar graphs |
| Screen saver | Pixel-shift integration (reads offset each frame) |
| Icon Store | For consumer indicator icons |
| Pad | Energy screen embeds a pad (page 0, layout E) at the bottom |

### Key Concepts

| Term | Meaning |
|---|---|
| **Category** | Solar, Home, or Grid. Solar and Grid values come from MQTT. Home is computed: `home = solar + grid` |
| **kW / milli-kW (mkw)** | Display values are in kW (float). Thresholds are stored in milli-kW (int32) for integer comparison precision |
| **Color tiers** | Each category has 4 colors: Good, OK, Attention, Warning — mapped by 3 ascending milli-kW thresholds |
| **T2 alarm** | When any category's value reaches or exceeds its third threshold (`threshold_mkw[2]`), the screen enters alarm mode |
| **Alarm pulse** | Background animates between black and the warning color of the triggering category. Foreground text is contrast-remapped to remain readable |
| **Consumer indicator** | Up to 5 auxiliary loads (e.g., EV charger, heat pump) shown as icons. Visible when their MQTT subscription value exceeds a threshold |
| **Coalescing window** | MQTT values that arrive within 1000ms of each other are batched into a single LVGL redraw to reduce PSRAM bus contention |

**Configuration defaults:**

| Setting | Type | Default | Notes |
|---|---|---|---|
| `energy_solar_bar_max_kw` | float | 3.0 | Full-scale bar height |
| `energy_home_bar_max_kw` | float | 3.0 | |
| `energy_grid_bar_max_kw` | float | 3.0 | |
| `energy_alarm_pulse_cycle_ms` | uint16 | 2000 | Full cycle (200–10000) |
| `energy_alarm_pulse_peak_pct` | uint8 | 100 | Peak intensity (0–100%) |
| `energy_alarm_clear_delay_ms` | uint16 | 800 | Hold time before clearing |
| `energy_alarm_clear_hysteresis_mkw` | int32 | 100 | Must drop by this much below T2 to clear |
| `energy_consumer_icon_color_rgb` | uint32 | 0xFFFFFF | Global tint for consumer icons |

**Per-category color config** (`EnergyCategoryColorConfig`):

| Field | Type | Description |
|---|---|---|
| `color_good_rgb` | uint32 | Color when below threshold 0 |
| `color_ok_rgb` | uint32 | Color between threshold 0 and 1 |
| `color_attention_rgb` | uint32 | Color between threshold 1 and 2 |
| `color_warning_rgb` | uint32 | Color at/above threshold 2 (also used as alarm peak color) |
| `threshold_mkw[3]` | int32[3] | Three ascending milli-kW breakpoints |

**Per-consumer config** (`EnergyConsumerConfig`):

| Field | Type | Max len | Description |
|---|---|---|---|
| `topic` | char[] | 128 | MQTT topic for consumer value |
| `threshold` | float | — | Show icon when value exceeds this |
| `icon_id` | char[] | 32 | Icon Store reference |

### User-Facing Behavior

1. **Three-column layout**: Solar, Home, Grid displayed left-to-right. Each column has: icon (top), value label in kW (middle), unit label "kW", and vertical bar graph.
2. **Flow arrows**: Directional arrows between columns indicate energy flow. Solar→Home arrow visible when solar ≥ 0.01 kW. Grid↔Home arrow direction flips based on grid polarity (positive = export to grid, negative = import from grid).
3. **Value formatting**: Values displayed as `%.2f` kW. When no data has been received, shows `"--"`.
4. **Bar graphs**: Manual (not `lv_bar`) — a background rectangle with a fill rectangle growing from bottom. Height proportional to `|value| / bar_max_kw`, capped at 100%. Values > 0 but rounding to 0 height get minimum 1px fill.
5. **Home value derivation**: `home_kw = solar_kw + grid_kw`. Only computed when both solar and grid have valid (non-NaN) values.
6. **Color mapping**: Each category independently selects its color from 4 tiers based on its milli-kW value compared to 3 ascending thresholds. Solar and Home use absolute values for comparison; Grid uses signed values (positive = export is "worse").
7. **T2 alarm activation**: When any category's value reaches `threshold_mkw[2]`, alarm enters `Active` state. The background pulses between black and the warning color. The pulse is a binary flip (not smooth fade) at half-cycle intervals.
8. **Contrast remapping during alarm**: When the background is near peak color, foreground elements of the alarming category are blended toward white to maintain readability. Non-alarming categories keep their normal colors.
9. **T2 alarm clearing**: Alarm clears only when all categories drop below `threshold_mkw[2] - hysteresis_mkw` and remain there for `clear_delay_ms`. This prevents rapid on/off flickering near the threshold.
10. **Consumer indicators**: Up to 5 icons displayed when their MQTT value exceeds the configured threshold. Icons loaded from Icon Store; use global tint color. Consumers with no topic configured are hidden.
11. **Embedded pad**: The energy screen embeds pad page 0 (layout E: 1×3 = 3 buttons) at the bottom. These buttons share the same configuration and runtime behavior as standalone pad buttons.
12. **Anti-flicker optimizations**: Style changes are cached; LVGL `set_style_*` calls are skipped when the value is unchanged (each call triggers `lv_obj_invalidate()` even for identical values).
13. **Pixel shift**: The entire background container is offset by the screen saver's pixel shift `(dx, dy)` each frame.
14. **Thread safety**: Energy values are written from the MQTT callback task and read from the LVGL task via a FreeRTOS spinlock.

### Configuration

Exposed via `GET/POST /api/config`:

| JSON field | Maps to | Notes |
|---|---|---|
| `mqtt_topic_solar` | `.mqtt_topic_solar` | MQTT subscription for solar kW |
| `mqtt_topic_grid` | `.mqtt_topic_grid` | MQTT subscription for grid kW |
| `mqtt_solar_value_path` | `.mqtt_solar_value_path` | JSON path to extract float value from MQTT payload |
| `mqtt_grid_value_path` | `.mqtt_grid_value_path` | JSON path to extract float value |
| `energy_solar_bar_max_kw` | `.energy_solar_bar_max_kw` | |
| `energy_home_bar_max_kw` | `.energy_home_bar_max_kw` | |
| `energy_grid_bar_max_kw` | `.energy_grid_bar_max_kw` | |
| `energy_alarm_pulse_cycle_ms` | `.energy_alarm_pulse_cycle_ms` | |
| `energy_alarm_pulse_peak_pct` | `.energy_alarm_pulse_peak_pct` | |
| `energy_alarm_clear_delay_ms` | `.energy_alarm_clear_delay_ms` | |
| `energy_alarm_clear_hysteresis_mkw` | `.energy_alarm_clear_hysteresis_mkw` | |
| `energy_[solar\|home\|grid]_color_[good\|ok\|attention\|warning]` | Per-category `EnergyCategoryColorConfig` | Hex `#RRGGBB` |
| `energy_[solar\|home\|grid]_threshold_[0\|1\|2]_kw` | `.threshold_mkw[]` | Float in kW; converted to mkw internally |
| `energy_consumer_[1-5]_topic` | `.energy_consumers[].topic` | |
| `energy_consumer_[1-5]_threshold` | `.energy_consumers[].threshold` | |
| `energy_consumer_[1-5]_icon_id` | `.energy_consumers[].icon_id` | |
| `energy_consumer_icon_color_rgb` | `.energy_consumer_icon_color_rgb` | Hex `#RRGGBB` |

### Integration Points

| Interface | Details |
|---|---|
| **MQTT → State** | MQTT callback extracts float values and calls `energy_monitor_set_solar/grid/consumer_value()`. Thread-safe via spinlock |
| **State → Screen** | LVGL update loop calls `energy_monitor_get_state(true)` to consume pending updates |
| **Warning query** | `energy_monitor_has_warning(config)` — used by other modules to check if alarm is active (e.g., for LED indicator) |
| **Pad integration** | Energy screen takes a reference to PadScreen page 0 for button configs and runtime state |
| **Screen saver** | Reads pixel shift offset for burn-in prevention |

### Reference Implementation

| File | Role |
|---|---|
| [src/app/energy_monitor.h](src/app/energy_monitor.h) | Thread-safe state API |
| [src/app/energy_monitor.cpp](src/app/energy_monitor.cpp) | State storage with spinlock, warning detection |
| [src/app/screens/energy_monitor_screen.h](src/app/screens/energy_monitor_screen.h) | Screen class with alarm state machine |
| [src/app/screens/energy_monitor_screen.cpp](src/app/screens/energy_monitor_screen.cpp) | Full LVGL layout, color mapping, alarm animation, consumer icons, embedded pad |
| [src/app/config_manager.h](src/app/config_manager.h) | `EnergyCategoryColorConfig`, `EnergyConsumerConfig`, related config fields |

### Design Notes

- **Why milli-kW for thresholds?** Floating-point comparison near boundary values causes flickering. Integer milli-kW with explicit hysteresis gives deterministic behavior.
- **Binary alarm pulse (not smooth)**: The alarm phase flips between 0 and 255 at half-cycle intervals. This was changed from smooth sine-wave fading because the binary flip is more attention-grabbing and uses less CPU. The `applyAlarmStyles()` function maps phase 255 to peak color via `lv_color_mix()`.
- **Coalescing window (1000ms)**: Solar and grid values often arrive from the same Home Assistant update within milliseconds. Without coalescing, each triggers a separate LVGL redraw, causing visible tearing on RGB panels where PSRAM bandwidth is shared between DMA and CPU.
- **Home is derived, not subscribed**: We intentionally don't subscribe to a separate home topic. Home = Solar + Grid is the physical truth from a meter perspective and avoids synchronization issues between three independent MQTT topics.
- **Consumer icons are lazily created**: At `create()` time during boot, PSRAM timing may not be stable. Icons are created on the first `update()` call instead.

---

## 4. Pad

### Intent

Provide a multi-page, fully customizable button grid that adapts to any display shape (square, rectangle, round), resolution (320px–720px+), and pixel density. Users configure grids via a REST API — choosing columns, rows, button content, and actions — without firmware changes. The layout engine automatically computes tile sizes to fill the available screen area. Future phases add curated layouts — hand-optimized, resolution-specific button arrangements (e.g., circular arrangements for round displays) that go beyond what a regular grid can achieve.

### Prerequisites

| Requirement | Details |
|---|---|
| Feature flag | `HAS_DISPLAY` (compile-time) |
| Optional flag | `HAS_MQTT` — for button actions, live labels, toggle state, HA event entities |
| Optional flag | `HAS_IMAGE_FETCH` — for background image fetching (JPEG/PNG from URL) |
| Filesystem | FFat partition for per-page JSON config and icon storage |
| LVGL | Container objects, image objects, label objects |
| Icon Store | For button icons |
| Screen saver | Pixel-shift integration (4px margin reservation) |

### Key Concepts

| Term | Meaning |
|---|---|
| **Page** | One of `MAX_PAD_PAGES` (8) pad screens (0–7). Each page has independent grid dimensions and button set |
| **Grid layout** | User-defined `cols × rows` (1–8 each). Tile sizes computed automatically from display geometry |
| **Curated layout** | (Phase 2) A hand-optimized, fixed-button-count layout tied to a specific resolution. Each board declares which curated layouts it supports. Multiple boards sharing the same resolution share the same curated layouts |
| **Safe area** | Usable drawing region after subtracting pixel-shift margin (4px all sides). Grid is anchored within this area |
| **Spanning** | A button can span multiple grid cells (`col_span`, `row_span`). Covered cells are hidden. Effective size includes gaps between spanned cells |
| **UI scale tier** | Compile-time setting (`UI_SCALE_TIER`) that selects font sizes and spacing for the board's PPI class |
| **Display shape** | Compile-time setting (`DISPLAY_SHAPE`) — Rectangle, Square, or Round. Informs layout strategy |
| **Runtime label** | MQTT-subscribed label text that overrides the static label at runtime (e.g., live sensor values) |
| **Toggle state** | MQTT-subscribed state that dims the button when "OFF" (foreground color switches to `disabled_fg_color_rgb`) |
| **Icon kind** | Color (emoji — rendered as-is) or Mono (Material Icons — tinted with `fg_color_rgb` via LVGL `img_recolor`) |

### Display Shape & UI Scale

**Display shape** (compile-time, per board):

```cpp
#define DISPLAY_SHAPE_RECT   0  // Rectangular (landscape or portrait)
#define DISPLAY_SHAPE_SQUARE 1  // Square
#define DISPLAY_SHAPE_ROUND  2  // Circular panel
```

| Board | Resolution | Shape | Curated Layouts | Notes |
|---|---|---|---|---|
| cyd-v2 | 320×240 | `RECT` | _(none)_ | Smallest, landscape |
| jc3248w535 | 320×480 | `RECT` | _(none)_ | Portrait |
| jc3636w518 | 360×360 | `ROUND` | `round_7`, `round_4` | Circular panel, corners clipped by hardware |
| esp32-4848S040 | 480×480 | `SQUARE` | `square_hero_5`, `square_dash_9` | Reference board |
| jc4880p433 | 480×800 | `RECT` | _(none)_ | Tallest, portrait |
| esp32-p4-lcd4b | 720×720 | `SQUARE` | `square_hero_5_hd`, `square_dash_9_hd` | Highest resolution |

**Round display handling (grid mode)**: The grid uses the full `width × height` dimensions. Corner tiles extend beyond the visible circle — the panel hardware clips them naturally. No software masking is applied. This maximizes usable screen real estate.

**UI scale tiers** (compile-time, per board):

```cpp
#define UI_SCALE_SMALL   0  // 320px-class displays, low PPI
#define UI_SCALE_MEDIUM  1  // 360–480px, moderate PPI
#define UI_SCALE_LARGE   2  // 480–720px, higher PPI
#define UI_SCALE_XLARGE  3  // 720px+, high PPI
```

Each tier selects a font triplet (Small / Medium / Large) from LVGL's compiled-in Montserrat fonts:

| Tier | Font S | Font M | Font L | Gap | Icon height % | Boards |
|---|---|---|---|---|---|---|
| `SMALL` | 12 | 14 | 18 | 2px | 40% tile_h | cyd-v2, jc3248w535 |
| `MEDIUM` | 14 | 18 | 24 | 3px | 45% tile_h | jc3636w518, esp32-4848S040 |
| `LARGE` | 14 | 24 | 32 | 3px | 45% tile_h | jc4880p433 |
| `XLARGE` | 18 | 24 | 36 | 4px | 50% tile_h | esp32-p4-lcd4b |

**Required LVGL fonts**: Montserrat 12, 14, 18, 24, 32, 36 (currently only 14/18/24 enabled — must add 12, 32, 36 in `lv_conf.h`).

**Font assignment per button:**
- `label_top` → Font S
- `label_center` → Font L (hero/value text; hidden when icon is present)
- `label_bottom` → Font S
- Icon: scaled to tier-defined percentage of tile height

### Grid Layout Engine

The layout engine computes tile positions and sizes from the display dimensions and the page's `cols × rows`:

```
PIXEL_SHIFT_MARGIN = 4           // Reserved on all sides for screen saver pixel shift
gap = tier_gap[UI_SCALE_TIER]    // From UI scale tier table above

safe_w = DISPLAY_WIDTH  - 2 * PIXEL_SHIFT_MARGIN
safe_h = DISPLAY_HEIGHT - 2 * PIXEL_SHIFT_MARGIN

tile_w = (safe_w - (cols - 1) * gap) / cols
tile_h = (safe_h - (rows - 1) * gap) / rows

// Button position (0-based col, row):
x = PIXEL_SHIFT_MARGIN + col * (tile_w + gap)
y = PIXEL_SHIFT_MARGIN + row * (tile_h + gap)
```

Tiles are **not forced square** — they stretch to fill available space in both dimensions. This optimizes screen usage on all aspect ratios (e.g., 480×800 portrait produces tall tiles with a 3×5 grid).

**Spanning**: A button with `col_span=2, row_span=1` gets width `2 * tile_w + gap` (includes the gap between spanned cells). Height follows the same pattern for `row_span`.

**Concrete examples** (4px margin, tier-appropriate gap):

| Board | Display | Grid | Tile W×H | Buttons |
|---|---|---|---|---|
| cyd-v2 | 320×240 | 4×3 | 76×76 | 12 |
| jc3248w535 | 320×480 | 3×5 | 102×92 | 15 |
| jc3636w518 | 360×360 | 3×3 | 115×115 | 9 |
| esp32-4848S040 | 480×480 | 4×4 | 115×115 | 16 |
| jc4880p433 | 480×800 | 3×5 | 155×156 | 15 |
| esp32-p4-lcd4b | 720×720 | 4×4 | 175×175 | 16 |

### Curated Layouts (Phase 2 — Not Implemented)

Curated layouts are hand-optimized, resolution-specific button arrangements that go beyond what a regular grid can achieve. Unlike grid layouts where the engine computes tile positions from `cols × rows`, curated layouts have hardcoded pixel positions, sizes, and optionally round button shapes.

**Key properties:**

- **Resolution-scoped, not board-scoped**: A curated layout targets a specific pixel resolution (e.g., `360×360`). Any board with that resolution can use it.
- **Fixed button count**: Each curated layout defines exactly how many buttons it supports. The config's `buttons` array is used sequentially — e.g., a `round_7` layout uses buttons 0–6.
- **Per-board allowlist**: Each board declares which curated layouts it supports via `CURATED_LAYOUTS` in its `board_overrides.h`. A board may support zero, one, or multiple curated layouts.
- **Property subset**: Curated layouts use the same `ScreenButtonConfig` fields as grid layouts, but may not support all properties (e.g., spanning is meaningless for fixed-position layouts).
- **Round buttons**: Curated layouts can specify circular button shapes (via `corner_radius = LV_RADIUS_CIRCLE`), which is not natural in a rectangular grid.

**Layout registry:**

Each curated layout is defined as a `CuratedLayoutDef` in firmware:

```cpp
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
    const CuratedButtonRect* rects;  // Hardcoded button positions
};
```

**Example curated layouts:**

| Name | Resolution | Buttons | Description | Candidate boards |
|---|---|---|---|---|
| `round_7` | 360×360 | 7 | 1 center + 6 ring (hexagonal) | jc3636w518 |
| `round_4` | 360×360 | 4 | 1 center + 3 ring (trefoil) | jc3636w518 |
| `square_hero_5` | 480×480 | 5 | 1 large hero + 4 small | esp32-4848S040 |
| `square_dash_9` | 480×480 | 9 | Mixed sizes dashboard | esp32-4848S040 |
| `square_hero_5_hd` | 720×720 | 5 | HD version of hero_5 | esp32-p4-lcd4b |
| `square_dash_9_hd` | 720×720 | 9 | HD version of dash_9 | esp32-p4-lcd4b |

**Board override example:**

```cpp
// src/boards/jc3636w518/board_overrides.h
#define CURATED_LAYOUTS  "round_7", "round_4"
```

**JSON format** — a curated layout page uses the layout name instead of `"grid"`:

```json
{
  "layout": "round_7",
  "buttons": [
    { "label_center": "Home", "icon_id": "mi_home", ... },
    { "label_center": "Lights", "icon_id": "mi_lightbulb", ... },
    ...
  ]
}
```

No `cols`/`rows` fields — the layout name fully determines button count and positions. Extra buttons beyond the layout's `button_count` are ignored. `col`/`row`/`col_span`/`row_span` fields are ignored for curated layouts.

### Constants

| Constant | Value | Notes |
|---|---|---|
| `MAX_PAD_PAGES` | 8 | Pages 0–7; easy to adjust |
| `MAX_PAD_BUTTONS` | 64 | 8×8 ceiling; per-page array size |
| `MAX_GRID_COLS` | 8 | |
| `MAX_GRID_ROWS` | 8 | |
| `PIXEL_SHIFT_MARGIN` | 4 | Matches screen saver ±4px shift range |
| `PAD_LABEL_COALESCE_MS` | 200 | MQTT label update batching window |

### Per-Button Configuration (`ScreenButtonConfig`)

| Field | Type | Max len | Description |
|---|---|---|---|
| `col` | uint8 | — | Grid column (0-based) |
| `row` | uint8 | — | Grid row (0-based) |
| `col_span` | uint8 | — | Columns spanned (default 1; ignored for curated layouts) |
| `row_span` | uint8 | — | Rows spanned (default 1; ignored for curated layouts) |
| `label_top` | char[] | 32 | Top label text (Font S) |
| `label_center` | char[] | 32 | Center label text (Font L; hidden when icon set) |
| `label_bottom` | char[] | 32 | Bottom label text (Font S) |
| `icon_id` | char[] | 32 | Icon Store reference; replaces center label when set |
| `bg_color_rgb` | uint32 | — | Background color |
| `fg_color_rgb` | uint32 | — | Foreground/text/icon color |
| `border_color_rgb` | uint32 | — | Border color (0x000000 = no border) |
| `corner_radius` | uint8 | — | Rounded corners (px, tier-scaled) |
| `action_screen_id` | char[] | 16 | Navigate to screen on tap |
| `action_mqtt_topic` | char[] | 128 | Publish on tap |
| `action_mqtt_payload` | char[] | 128 | Payload for tap |
| `lp_action_screen_id` | char[] | 16 | Navigate on long press |
| `lp_action_mqtt_topic` | char[] | 128 | Publish on long press |
| `lp_action_mqtt_payload` | char[] | 128 | Payload for long press |
| `label_top_sub_topic` | char[] | 128 | MQTT topic → live top label |
| `label_top_json_path` | char[] | 64 | JSON path to extract value |
| `label_top_format` | char[] | 32 | printf format string |
| `label_center_sub_topic` | char[] | 128 | MQTT topic → live center label |
| `label_center_json_path` | char[] | 64 | |
| `label_center_format` | char[] | 32 | |
| `label_bottom_sub_topic` | char[] | 128 | MQTT topic → live bottom label |
| `label_bottom_json_path` | char[] | 64 | |
| `label_bottom_format` | char[] | 32 | |
| `state_topic` | char[] | 128 | ON/OFF toggle state MQTT topic |
| `state_json_path` | char[] | 64 | JSON path for state value |
| `state_on_value` | char[] | 32 | Value that means "ON" |
| `disabled_fg_color_rgb` | uint32 | — | Dimmed foreground when OFF |
| `bg_image_url` | char[] | 256 | JPEG/PNG snapshot URL |
| `bg_image_user` | char[] | 32 | HTTP Basic Auth user |
| `bg_image_password` | char[] | 64 | HTTP Basic Auth password |
| `bg_image_interval_ms` | uint32 | — | Auto-refresh (0 = fetch once) |

**Default colors:**

| Default | Hex | Usage |
|---|---|---|
| `SCREEN_BUTTON_DEFAULT_BG_COLOR` | `#333333` | Button background |
| `SCREEN_BUTTON_DEFAULT_FG_COLOR` | `#FFFFFF` | Button text/icon |
| `SCREEN_BUTTON_DEFAULT_DISABLED_FG_COLOR` | `#666666` | Dimmed text when OFF |
| `SCREEN_BUTTON_DEFAULT_BORDER_COLOR` | `#000000` | No border (0 = hidden) |

### User-Facing Behavior

**Grid rendering:**

1. **Auto-computed grid**: Tile sizes are computed from `cols × rows` and the display's safe area. No fixed layout templates — any combination of 1–8 cols/rows works.
2. **Tiles fill the space**: Tiles are not forced square. A 3×5 grid on a 480×800 portrait display produces 155×156 tiles, using nearly every pixel.
3. **Three label slots per button**: `label_top` (top, Font S), `label_center` (center, Font L), `label_bottom` (bottom, Font S). Icon replaces center label when set.
4. **Empty slot skipping**: Buttons with no icon, no labels, no subscribe topics, and no actions are not rendered. The grid cell is left empty (transparent background).
5. **Spanning**: A button with `col_span=2, row_span=2` occupies 4 grid cells. Covered cells are hidden. Effective size includes gaps between spanned cells.
6. **Round display clipping**: On `DISPLAY_SHAPE_ROUND`, corner tiles extend past the visible circle and are clipped by the panel hardware. No software masking.

**Curated layout rendering (Phase 2):**

7. **Fixed positions**: Button positions and sizes are taken directly from the curated layout's hardcoded `rects[]` array. No grid computation.
8. **Sequential button assignment**: Buttons from the config's `buttons[]` array are assigned to layout positions by index (0, 1, 2, ...). Extra buttons beyond the layout's `button_count` are ignored.
9. **Round buttons**: Curated layouts can specify round buttons — rendered with `LV_RADIUS_CIRCLE` corner radius. Natural for circular arrangements on round displays.
10. **Same button content model**: Curated layout buttons support the same labels, icons, colors, actions, and MQTT subscriptions as grid buttons. `col`/`row`/`col_span`/`row_span` fields are simply ignored.

**Button actions:**

11. **Tap**: Publishes `action_mqtt_payload` to `action_mqtt_topic` (if set), fires a Home Assistant button event entity (always, if `HAS_MQTT`), plays a tap-flash visual feedback (30% brighter for 100ms), then navigates to `action_screen_id` (if set).
12. **Long-press**: Same flow as tap but uses `lp_action_*` fields and HA "hold" event type.
13. **Tap flash feedback**: Button background briefly flashes 30% lighter for 100ms, then restores.

**Live MQTT updates:**

14. **Label subscriptions**: Each of the 3 label slots can subscribe to an MQTT topic. When a message arrives, the value is extracted via `json_path` (if set, for JSON payloads) or used directly, then formatted with `format` (printf-style, e.g. `"%.1f°C"`), and displayed on the button.
15. **Toggle state**: When `state_topic` is configured, the device subscribes. Incoming values are compared to `state_on_value` (after optional `state_json_path` extraction). Match → "ON" (normal `fg_color_rgb`). Mismatch → "OFF" (dimmed `disabled_fg_color_rgb`).
16. **Runtime label coalescing**: Rapid MQTT label updates are batched for `PAD_LABEL_COALESCE_MS` (200ms) before applying to LVGL, reducing redraw churn.

**Background images:**

17. **Image fetch**: Buttons with `bg_image_url` set fetch JPEG/PNG snapshots from the URL. A single dedicated FreeRTOS fetch task round-robins through all active image slots, downloading one at a time. Images are decoded to RGB565 pixel buffers (decode once, draw cheap) and displayed behind button text/icons.
18. **Cover-mode scaling**: Fetched images are scaled to fill the entire button area using CSS `object-fit: cover` semantics — `scale = max(target_w/src_w, target_h/src_h)`, bilinear resample, center-crop. No letterboxing, no stretching. For JPEG, the decoder's built-in downscale (1/2, 1/4, 1/8) is used to get close to target size first, then fine-scale — saves decode RAM for large source images.
19. **Refresh interval**: `bg_image_interval_ms` controls auto-refresh (0 = fetch once). Useful for webcam snapshots.
20. **Cached across hide/show**: Decoded pixel buffers survive `hide()`/`show()` cycles. When navigating back to a page with image buttons, the previously-decoded images display immediately while the fetch task begins a new refresh cycle in the background.
21. **Authentication**: Optional HTTP Basic Auth per button (`bg_image_user`/`bg_image_password`).
22. **Visibility-gated fetching**: Only the currently visible pad page's image slots are actively fetched. When navigating away, the fetch task pauses (HTTP connections close, no new requests). When navigating back, the task resumes. Buffers are retained across page switches.
23. **Screen saver pause**: When the screen saver activates, all image fetching stops. Fetching resumes when the screen saver deactivates (if the pad page is still the active screen).
24. **Double-buffered frames**: Each image slot has two PSRAM pixel buffers (front/back). The fetch task writes to the back buffer; on completion, an atomic pointer swap makes the new frame visible. LVGL only reads the front buffer, so no mutex is needed for frame handoff.
25. **Lazy slot allocation**: Pixel buffers are only allocated for buttons that have `bg_image_url` set. No upfront allocation for the full page.

**Persistence:**

26. **LittleFS JSON storage**: Each page's config is stored at `/config/pad_N.json`.
27. **Live update on save**: After a POST to `/api/pad`, icons are pre-cached, the in-memory config is updated, and `ui_generation` is bumped. The LVGL task detects the generation change and rebuilds tiles without a reboot.
28. **MQTT resubscribe**: After saving a page with new subscribe topics, the MQTT manager is notified to update subscriptions.
29. **Image re-fetch on save**: After saving a page, old image slots are cancelled and new slots are started for any buttons with `bg_image_url`.

**Pixel shift:**

30. **Burn-in prevention**: The content wrapper container is offset by the screen saver's pixel shift `(dx, dy)` each frame. The 4px margin on all sides provides space for this shift without pushing content off-screen.

### Configuration

**Per-page config** — `GET/POST/DELETE /api/pad?page=N`:

| Method | Behavior |
|---|---|
| GET | Load from FFat, return JSON config |
| POST | Save to FFat, update in-memory state, preload icons, bump `ui_generation`, trigger MQTT resubscribe |
| DELETE | Delete config file, reset page to defaults |

**Page JSON format:**

```json
{
  "layout": "grid",
  "cols": 4,
  "rows": 4,
  "buttons": [
    {
      "col": 0, "row": 0, "col_span": 2, "row_span": 1,
      "label_top": "Living Room",
      "icon_id": "mi_thermostat",
      "label_bottom": "Heating",
      "fg_color": "#FFFFFF", "bg_color": "#1A5276",
      "action_screen": "pad_2",
      "label_center_sub": "home/temp",
      "label_center_path": "value",
      "label_center_fmt": "%.1f°C",
      "state_topic": "home/hvac", "state_on": "heating"
    }
  ]
}
```

**Button JSON field mapping:**

| JSON field | Struct field | Notes |
|---|---|---|
| `col` | `col` | Grid column (0-based) |
| `row` | `row` | Grid row (0-based) |
| `col_span` | `col_span` | Grid span (default 1) |
| `row_span` | `row_span` | Grid span (default 1) |
| `label_top` | `label_top` | Top text |
| `label_center` | `label_center` | Center text (hidden when icon set) |
| `label_bottom` | `label_bottom` | Bottom text |
| `icon_id` | `icon_id` | Icon Store reference |
| `bg_color` | `bg_color_rgb` | `#RRGGBB` hex |
| `fg_color` | `fg_color_rgb` | `#RRGGBB` hex |
| `border_color` | `border_color_rgb` | `#RRGGBB` hex |
| `corner_radius` | `corner_radius` | Pixels |
| `action_screen` | `action_screen_id` | Tap navigation target |
| `action_mqtt_topic` | `action_mqtt_topic` | Tap MQTT publish target |
| `action_mqtt_payload` | `action_mqtt_payload` | Tap payload |
| `lp_action_screen` | `lp_action_screen_id` | Long-press navigation |
| `lp_action_mqtt_topic` | `lp_action_mqtt_topic` | Long-press MQTT target |
| `lp_action_mqtt_payload` | `lp_action_mqtt_payload` | Long-press payload |
| `label_top_sub` | `label_top_sub_topic` | MQTT subscribe for top label |
| `label_top_path` | `label_top_json_path` | JSON extraction path |
| `label_top_fmt` | `label_top_format` | printf format string |
| `label_center_sub` | `label_center_sub_topic` | MQTT subscribe for center label |
| `label_center_path` | `label_center_json_path` | |
| `label_center_fmt` | `label_center_format` | |
| `label_bottom_sub` | `label_bottom_sub_topic` | MQTT subscribe for bottom label |
| `label_bottom_path` | `label_bottom_json_path` | |
| `label_bottom_fmt` | `label_bottom_format` | |
| `state_topic` | `state_topic` | Toggle state MQTT topic |
| `state_path` | `state_json_path` | JSON path for state value |
| `state_on` | `state_on_value` | Value = "ON" |
| `disabled_fg` | `disabled_fg_color_rgb` | `#RRGGBB` when OFF |
| `bg_image_url` | `bg_image_url` | Background image URL |
| `bg_image_user` | `bg_image_user` | HTTP Basic Auth user |
| `bg_image_password` | `bg_image_password` | HTTP Basic Auth password |
| `bg_image_interval_ms` | `bg_image_interval_ms` | Refresh interval (0 = once) |

### Integration Points

| Interface | Details |
|---|---|
| **REST API** | `GET/POST/DELETE /api/pad?page=N` |
| **MQTT subscribe** | Per-button label and state topics subscribed dynamically. MQTT manager routes messages to the correct screen/button |
| **MQTT publish** | Button taps/holds publish to configured topics |
| **HA event entity** | Every tap/hold fires a structured JSON event to `<base>/event` with `{event_type, screen, row, col, label_top, label_center, label_bottom, payload}` |
| **Screen navigation** | `display_manager_show_screen(screen_id)` for tap/long-press navigation. Enables multi-page workflows (page 1 button navigates to page 3) |
| **Icon Store** | `icon_store_lookup()` for button icons; `icon_store_preload_buttons()` after save |
| **Screen saver** | Pixel shift via `screen_saver_manager_get_pixel_shift()`. Margin of `PIXEL_SHIFT_MARGIN` (4px) reserved on all sides |
| **Image fetch** | `image_fetch_request/cancel()` for background images; `image_fetch_pause/resume()` for screen saver and screen visibility |

### Reference Implementation

| File | Role |
|---|---|
| [src/app/pad_layout.h](src/app/pad_layout.h) | Grid + curated layout engine, `PIXEL_SHIFT_MARGIN`, `CuratedLayoutDef` registry, UI scale tier font/gap tables, `PadLayout` struct, tile rect computation |
| [src/app/pad_config.h](src/app/pad_config.h) | FFat JSON load/save/delete API, `ScreenButtonConfig` struct |
| [src/app/pad_config.cpp](src/app/pad_config.cpp) | JSON serialization/deserialization, color parsing, default initialization |
| [src/app/screens/pad_screen.h](src/app/screens/pad_screen.h) | Screen class with runtime state, MQTT label/state integration |
| [src/app/screens/pad_screen.cpp](src/app/screens/pad_screen.cpp) | LVGL tile creation, click/long-press handlers, runtime updates, image fetch lifecycle |
| [src/app/screens/button_runtime.h](src/app/screens/button_runtime.h) | Shared helpers: tap flash, runtime label/color application, HA event publishing |
| [src/app/image_decoder.h](src/app/image_decoder.h) | Stateless decode+scale API (JPEG via tjpgd, PNG via lodepng) — reusable by pad and future webcam screen |
| [src/app/image_decoder.cpp](src/app/image_decoder.cpp) | Format detection, decode to RGB565, cover-mode scale with center-crop |
| [src/app/image_fetch.h](src/app/image_fetch.h) | Slot-based image fetch API: request, cancel, pause, resume, poll for new frames |
| [src/app/image_fetch.cpp](src/app/image_fetch.cpp) | Single FreeRTOS task, round-robin fetch, HTTP client, double-buffered PSRAM slots |
| [src/app/web_portal_pad.cpp](src/app/web_portal_pad.cpp) | REST API handlers for GET/POST/DELETE |
| [src/app/board_config.h](src/app/board_config.h) | Default `DISPLAY_SHAPE`, `UI_SCALE_TIER`, `CURATED_LAYOUTS`, font enable flags |

### Design Notes

- **No fixed layout templates**: Unlike the reference implementation's 5 predefined templates, this design uses free-form `cols × rows` for grids. This is more flexible and avoids the problem of templates not fitting non-square displays. For cases where grids aren't optimal (e.g., circular arrangements on round displays), curated layouts provide hand-optimized fixed-position alternatives — tied to a resolution, not a board.
- **Curated layouts are resolution-scoped**: A curated layout hardcodes pixel positions for a specific resolution. Any board sharing that resolution can use it. Each board declares its allowlist via `CURATED_LAYOUTS`. This avoids per-board duplication while keeping layouts precisely tuned.
- **MAX_PAD_BUTTONS = 64**: Sized to 8×8 maximum grid. This is generous; most pages will use 9–16 buttons. The `ScreenButtonConfig` structs are stored on FFat (not in RAM simultaneously), so the ceiling is cheap.
- **4px margin = exact fit for pixel shift**: The screen saver shifts content ±4px. A 4px margin on all sides means tiles at maximum shift touch the screen edge but never overflow. If the pixel shift range changes, `PIXEL_SHIFT_MARGIN` must be updated to match.
- **Tiles are not square by design**: On a 480×800 display with a 3×4 grid, tiles are 155×195 — taller than wide. This is intentional: it maximizes screen usage. Users who want square tiles can choose a grid where `cols/rows ≈ width/height`.
- **Round display: clipping not masking**: Software masking (alpha blending at circle edges) would require per-frame compositing on PSRAM-constrained devices. Hardware clipping is free and looks fine — corner buttons are simply truncated.
- **UI scale tiers vs continuous scaling**: Continuous scaling would require arbitrary font sizes, which LVGL doesn't support without FreeType/TinyTTF (disabled, adds ~100KB flash + runtime overhead). Discrete tiers with 6 pre-compiled font sizes are a pragmatic compromise.
- **Lazy tile creation**: Tiles are created in `update()`, not `create()`, because config is loaded from FFat after the screen object is constructed.
- **ui_generation counter**: A single `uint8_t` counter bumped on every save. The LVGL task compares its cached value with the live one. On mismatch, tiles are destroyed and recreated. This avoids complex diff logic.
- **MQTT label coalescing (200ms)**: Home Assistant often publishes multiple entity state changes in rapid succession. Coalescing prevents per-label LVGL redraws and reduces PSRAM bus contention on RGB panels.
- **Background image pixel ownership**: The image_fetch module owns decoded pixel buffers (PSRAM `uint16_t*`) in double-buffered slots. The fetch task writes to the back buffer and atomically swaps pointers on completion. LVGL reads the front buffer without locking. Buffers survive screen hide/show cycles.
- **Shared image decode module**: `image_decoder.h/cpp` is split from `image_fetch.h/cpp` so decode+scale can be reused by a future full-screen webcam viewer without pulling in the fetch task's slot management.
- **Single fetch task + round-robin**: One FreeRTOS task cycles through active slots, fetching the oldest slot first. This avoids WiFi contention from parallel HTTP connections and keeps the pattern simple. Initial page load is progressive — images appear one-by-one.
- **Cover-mode scaling**: Images are scaled to fill the tile using `max(tw/sw, th/sh)`, bilinear interpolation, center-crop. For JPEG, the decoder's built-in 1/2, 1/4, 1/8 downscale is used as a first pass to reduce decode buffer size before fine-scaling.
- **tjpgd + lodepng (LVGL built-in)**: Both decoders ship with the LVGL 9.5 library. Called directly as C libraries (not via LVGL's decoder pipeline) because decoding happens in the fetch task, outside of LVGL's drawing context. esp_jpeg is not available on ESP32-P4, which rules it out as a cross-platform option.
- **Color parsing flexibility**: The REST API accepts `#RRGGBB`, `RRGGBB`, `0xRRGGBB`, or plain uint32. Output is always `#RRGGBB`.
- **Curated layout forward-compatibility**: Phase 1 only implements `"layout": "grid"`. Unknown layout type strings (e.g., `"round_7"`) fall back to grid 3×3. When Phase 2 adds the curated layout registry, these layout names will resolve to their hardcoded button positions. No config migration needed.
