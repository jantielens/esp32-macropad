# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---

## [Unreleased]

## [1.4.0] - 2026-03-03

### Added
- **Pad button icons** — emoji and Material Symbol icon support for pad buttons with PNG storage on LittleFS, PSRAM-cached ARGB8888 draw buffers, and browser-based icon picker with canvas-to-PNG upload
- **Sleep overlay** — opaque black layer rendered on `lv_layer_top()` while the screen saver is asleep, preventing stale content from showing through on displays without true backlight off
- **Pixel shift (burn-in prevention)** — each sleep/wake cycle applies a new sub-pixel offset (±4 px) to the active screen, cycling through 81 positions to distribute pixel wear evenly; pad layouts already reserve `PIXEL_SHIFT_MARGIN` insets
- **HA button press event entity** — every pad button tap/hold publishes an MQTT event to `~/event` with `event_type` (press/hold), page, col, row, and label; Home Assistant auto-discovers it as an Event entity for use in automations
- **Pad editor QoL features** — new web portal quality-of-life improvements for pad/button editing:
  - **Pad naming** — optional custom name per pad page (max 31 chars), shown in the pad dropdown and target screen dropdowns
  - **Button copy/paste** — copy a button's settings (position-independent) and paste into another cell; dialog footer now has Copy and Paste buttons
  - **Fill pad** — fill all cells with the copied button settings in one click
  - **Pad copy/paste** — copy an entire pad (grid size, name, all buttons) and paste into another page
  - **Pad export/import** — export a single pad page to JSON file; import from file loads into the editor (unsaved until Save Pad)
  - **Device config export/import** — export full device configuration (NVS settings + all 8 pad configs) to JSON; import overwrites settings, all pads, and reboots
  - **Used-color swatches** — each color picker in the button editor shows a row of clickable swatches drawn from colors already used on the current pad (and other visited pads), for quick reuse without memorising hex values
  - **"More ▾" dropdown menu** — decluttered pad actions into a compact dropdown: Fill/Copy/Paste Pad, Export/Import Pad, Export/Import Device Config, Clear Pad
  - **Dialog scroll-to-top** — button edit dialog now scrolls to top on open and locks background page scroll
  - **Custom pad names in `/api/info`** — firmware now includes user-defined pad names in the `available_screens` response, eliminating 8 extra HTTP fetches at page load

### Improved
- **Multi-step screen history** — "navigate back" now supports a full history stack (up to 8 deep) instead of only remembering the single previous screen; splash screen is excluded from history
- **ESP32-P4 WiFi startup** — replaced blind 5 s delay with active ESP-Hosted link polling (exits as soon as the C6 co-processor's MAC is readable) and reduced inter-retry disconnect delay from 2 s to 500 ms; cuts typical WiFi connect time from ~16 s to ~8 s

### Fixed
- **Image decoder heap safety** — four hardening fixes for the JPEG/PNG → RGB565 pipeline that address a sporadic `CORRUPT HEAP` crash (1-byte off-by-one overflow):
  - JPEG output callback now clips MCU blocks at the image boundary instead of dropping them entirely (tjpgd can deliver blocks extending past image dimensions due to 8/16-pixel MCU alignment)
  - tjpgd work buffer increased from 4 KB to 8 KB for additional margin on complex JPEGs
  - Letterbox scaling clamps `scaled_w`/`scaled_h` to the target dimensions (floating-point rounding could push them 1 px past the output buffer)
  - JPEG intermediate RGB888 buffer now freed with `heap_caps_free()` consistently (was using `free()` for a `heap_caps_malloc` allocation)
- **Phantom touch on screensaver wake** — tapping to wake the screen no longer triggers a click on the underlying screen; after touch suppression ends, a genuine release is now required before new presses are accepted (fixes stale GT911 touch state leaking through)
- **Rounded corners with image backgrounds** — pad button background images are now clipped to each tile's configured corner radius, so image-backed buttons visually match rounded button styling
- **Pad button border default** — new/unspecified button `border_width` now defaults to `0 px` (was `1 px`) in both firmware parsing and web editor defaults
- **Screen flicker on pad save with icons** — on MIPI-DSI boards (`DISPLAY_BLANK_ON_SAVE`), the browser now blanks the backlight for the entire save sequence (icon delete → icon uploads → config save → tile rebuild) and restores it after LVGL has rendered the new state, eliminating cyan/blue flashes caused by PSRAM bus contention between the DPI controller's continuous DMA scan and heavy LittleFS/lodepng I/O
- **Icon upload race condition** — a stale `errored` flag from a prior failed icon upload could silently block all subsequent icon uploads in the same pad save, causing icons to appear missing until reboot; the flag is now cleared at the start of each new upload
- **Pad config save requiring double-click** — the generation counter was incremented before the RAM cache was updated, so LVGL's polling task could rebuild tiles from stale data; cache is now written before the generation bump, ensuring a single save always renders the correct state
- **WiFi MAC crash during image fetch** — `WiFiClient` was stack-allocated on the PSRAM-backed fetch task stack; the WiFi MAC DMA engine could access stale/freed memory from the client's internal lwIP structures after `http.end()` returned but before the stack frame was reused, corrupting TX descriptors (Guru Meditation `LoadProhibited` in `ppTask`). Fix: heap-allocate WiFi clients in internal RAM and add a 100 ms drain delay after `http.end()` to let the MAC finish processing TCP close frames before client destruction

## [1.3.0] - 2026-03-01

### Added
- **Letterbox image scaling** — new `ImageScaleMode` option for button background images
  - `IMAGE_SCALE_LETTERBOX`: fit image inside target area with black bars (CSS object-fit: contain)
  - `IMAGE_SCALE_COVER` (default): existing fill + center-crop behavior preserved
  - Per-button `bg_image_letterbox` config field in pad JSON
  - Web portal: "Letterbox" checkbox in button image background editor
- **Smart idle sleep for image fetch** — fetch task now sleeps only until the soonest slot is due, instead of a fixed 500ms delay; improves responsiveness for short refresh intervals

## [1.2.0] - 2026-03-01

### Added
- **Background image fetch** — per-button HTTP(S) image backgrounds with configurable refresh interval
  - `image_fetch` module: FreeRTOS background task with round-robin slot scheduling (up to 64 slots)
  - `image_decoder` module: JPEG (tjpgd) and PNG (lodepng) decode with cover-mode bilinear scaling to RGB565
  - Double-buffered PSRAM pixel data with tile-owned copy for safe LVGL rendering
  - HTTP Basic Auth support for authenticated camera feeds
  - Per-slot pause/resume — only the active page fetches images; hidden pages retain their last frame in memory
  - Web portal: "Image Background" section in pad button editor (URL, auth, refresh interval)
- **LVGL image decoders enabled** — `LV_USE_TJPGD=1` and `LV_USE_LODEPNG=1` in lv_conf.h

### Fixed
- LVGL task stack increased from 8 KB to 16 KB to prevent overflow when rendering scaled images
- WiFiClientSecure heap-allocated only for HTTPS URLs to reduce fetch task stack pressure
- ESP Web Tools flash site: ESP32-P4 boards now correctly identified as `ESP32-P4` chip family with bootloader offset 0x2000 (previously fell through to classic `ESP32` with wrong offset 0x1000)

## [1.1.0] - 2026-02-28

### Added
- **Pad screen system** — multi-page configurable button grid with tap and long-press actions
  - Layout engine with grid computation (up to 8×8) and UI scale tiers per board
  - LittleFS-backed JSON config at `/config/pad_N.json` (8 pages, 64 buttons max)
  - REST API: `GET/POST/DELETE /api/pad?page=N` with full validation
  - LVGL rendering with per-tile colors, border, corner radius, 3 labels (top/center/bottom), spanning
  - Typed action system: navigate to screen, navigate back, publish MQTT
  - Screen saver pixel-shift integration
- **MQTT live labels** — per-label MQTT bindings with topic, JSON path extraction, and printf format strings
  - Thread-safe `mqtt_sub_store` (FreeRTOS mutex, PSRAM-allocated, 64 entries)
  - Subscribe-all-pages on connect for instant data availability
  - Automatic resubscribe on config save/delete
- **Toggle state** — per-button state binding (topic, JSON path, on-value) dims tiles when OFF
- **Web portal pad section** — full CRUD UI for pad configuration
  - Edit dialog with sticky header/footer, scrollable body, mobile-responsive layout
  - Per-button: colors, labels, actions (tap + long-press), MQTT label bindings, toggle state
  - Pad selector with 1-indexed "Pad 1–8" naming
- **Display/touch improvements** — `goBack()` navigation, board-specific UI scale tiers and display shapes
- **LittleFS migration** — filesystem health monitoring with `fs_health` module
- **Board config** — added `DISPLAY_SHAPE` and `UI_SCALE_TIER` for all boards

### Changed
- Partition tables updated for LittleFS support (6MB and 8MB variants)
- Compile-time flags documentation simplified

## [1.0.0] - 2026-02-27

### Added
- Initial release of ESP32 Macropad firmware (forked from esp32-template)
