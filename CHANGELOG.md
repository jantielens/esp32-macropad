# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---

## [Unreleased]

### Added
- **Wake Screen redirect** — per-pad `wake_screen` setting navigates to a target screen when the screensaver enters sleep (invisible under the sleep overlay); configurable in the web portal Pad Editor "Wake Screen" dropdown; default is to stay on the current screen
- **MQTT Active Screen control** — exposes active screen as an HA `select` entity (`~/screen/state` + `~/screen/set`); HA can navigate the device to any screen and wake the screensaver; navigating to the current screen while asleep wakes the device; inactivity timer resets on HA navigation (mimics a tap)
- **Pad background color** — per-pad `bg_color` setting with color picker and recently-used swatches in the web portal; default is pure black; applied as LVGL screen background on the device

### Fixed
- **ESP32-P4 health telemetry re-enabled** — background telemetry tasks (CPU monitor, health-window timer, sparklines) re-enabled on both P4 boards (`esp32-p4-lcd4b`, `jc4880p433`); originally disabled during flicker investigation, no longer needed

### Improved
- **Pad Editor aspect ratio** — grid preview now mirrors the device screen's aspect ratio using display dimensions from `/api/info`; portrait devices render as tall rectangles, square devices as squares; capped at 70vh with auto-centering to keep the editor compact
- **Pad Editor button rendering** — improved grid preview fidelity: background image buttons show a 🖼️ placeholder emoji (visible alongside labels/icons); top/bottom labels are pushed to cell edges matching device layout; binding tokens simplified to `[scheme:key]` in preview; spacer divs keep center content vertically centered when only one edge label is present

## [1.5.0] - 2026-03-04

### Added
- **Widget button type system** — extensible widget framework for specialized button visualizations beyond simple labels and icons
  - `WidgetType` interface with pluggable `parseConfig`, `createUI`, `update`, and `destroyUI` hooks
  - Registry-based widget lookup by name, stored as a 64-byte config blob per button
  - Web portal: "Button Type" dropdown in the pad button editor to select Normal or widget types
- **Bar Chart widget** — first widget type; renders a vertical bar that fills based on live data
  - Configurable min/max range, bar width %, background color, and up to 4 color thresholds
  - Supports both raw values and absolute-value mode for bidirectional metrics (e.g. grid import/export)
  - Adaptive layout: bar height adjusts dynamically based on presence of icon, top label, and bottom label
  - Uses binding template syntax for data source (e.g. `[mqtt:topic;path]`, `[health:cpu]`), supporting all registered schemes
  - Web portal: full "Bar Chart Settings" section with all config fields; grid preview shows gradient indicator
- **Icon scale percentage** — new `icon_scale_pct` field (0 = auto, 1–250 = explicit %) lets users control icon size per button; widget-aware auto-sizing renders icons at half height for bar chart buttons
- **Monitor logging** — `./monitor.sh --log` writes timestamped serial output to auto-named log files (`monitor_YYYYMMDD_HHMMSS.log`); `--log=file.log` for a custom filename
- **Inline label binding syntax** — labels now support `[mqtt:topic;path;format]` tokens directly in the label text, replacing the separate MQTT Label Binding fields (topic, path, format per label)
  - Extensible `[scheme:params]` registry pattern for future binding types (REST, sensor, time, etc.)
  - Supports static prefix/suffix text around tokens (e.g. `Power: [mqtt:grid;power;%d] W`)
  - Multiple binding tokens per label (e.g. `[mqtt:a;temp;%.0f]° / [mqtt:b;hum;%.0f]%`)
  - Graceful error handling: malformed tokens show `ERR:xxx`, missing data shows `---`
  - `CONFIG_LABEL_MAX_LEN` increased from 32 to 192 bytes; net RAM neutral (replaces 3 × 600-byte `LabelBinding` structs)
- **Binding syntax help dialog** — `?` button next to label fields opens a styled help popup with syntax reference, parameter descriptions, and progressive examples
- **Device health binding scheme** — `[health:key;format]` tokens resolve local device telemetry (CPU usage, heap, PSRAM, RSSI, uptime, IP address, hostname); expensive reads (CPU, RSSI, memory) cached for 2 s, lightweight keys (uptime, IP, hostname) resolve live
- **Date & time binding scheme** — `[time:format;timezone]` tokens display NTP-synced date/time using strftime format strings; supports ~40 Olson timezone names (e.g. `Europe/Amsterdam`) with automatic DST, raw POSIX TZ fallback, and UTC default

### Fixed
- **Square icon PNGs** — emoji/Material Symbol icons are now rendered on a square canvas (using the minimum of width and height) instead of the original rectangular glyph bounding box, eliminating transparent padding above and below icons
- **MQTT multi-label same-topic bug** — multiple label bindings subscribed to the same MQTT topic now all update correctly; previously the per-topic dirty flag was cleared by the first binding, causing subsequent bindings to miss the update
- **MQTT bindings empty on non-initial pad screens** — label and state bindings on pad pages other than the boot page (pad 0) now show retained MQTT data immediately; previously, pad 0 cleared the global dirty flags before other pages could read them, and navigating back to a page would not re-render updated values
- **OTA rollback protection** — `esp_ota_mark_app_valid_cancel_rollback()` is now called at end of `setup()`, preventing the bootloader from rolling back healthy firmware on the next reboot
- **Heap corruption detection** — periodic `heap_caps_check_integrity_all()` check added to the 60 s heartbeat loop for early detection of heap corruption

### Improved
- **Pixel shift wake logging** — screen saver wake log line now includes the current pixel shift offset (`dx`, `dy`) for easier burn-in prevention verification
- **MQTT payload buffer** — increased `MQTT_SUB_STORE_MAX_VALUE_LEN` from 256 to 2048 bytes to support larger JSON payloads (e.g. Home Assistant media player attributes); store is PSRAM-backed so no internal SRAM impact
- **MQTT JSON parser** — `DynamicJsonDocument` replaced with PSRAM-backed `BasicJsonDocument<PsramJsonAllocator>` (2048 bytes) for reliable parsing of larger payloads
- **MQTT payload overflow detection** — payloads exceeding the 2048-byte buffer are now detected and flagged; labels show `[TOO BIG]` instead of broken JSON, and a `LOGW` is emitted on serial for diagnostics
- **Boot-time MQTT discovery** — HA discovery messages are now published during the boot splash (blocking, 8 s timeout) instead of from the async main loop, reducing concurrent WiFi pressure during the critical early-boot window
- **Deferred image fetching** — `image_fetch_init()` now starts after the pad screen is visible (post-splash), avoiding concurrent HTTP downloads during boot
- **Cached WiFi RSSI** — RSSI is sampled once at boot (and on reconnect) and cached; health publishes no longer issue live `WiFi.RSSI()` RPC calls, eliminating repeated ESP-Hosted SDIO round-trips from the hot path
- **MQTT connect DRY refactor** — extracted shared `attemptConnectWithLWT()` and `onConnected()` helpers, removing ~40 lines of duplicated connect/post-connect logic between boot-time and runtime MQTT paths
- **HTTP keep-alive for image fetch** — camera image downloads now reuse persistent per-slot TCP connections (`setReuse(true)`) instead of creating and tearing down a new connection on every request; eliminates ~3 TCP SYN/ACK+FIN cycles per second that were stressing the WiFi MAC TX queue, addressing the `ppTask` crash in `lmacProcessTxComplete`; connections are automatically torn down on WiFi disconnect, protocol change, or slot cancellation
- **Simplified pad button editor** — removed the separate "MQTT Label Bindings" section (9 input fields) from the web portal; binding configuration is now done inline in the label text fields, reducing UI complexity

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
- **Screen navigation: go-back on all screens** — Info, Test, and FPS screens now navigate back to the previous screen on tap (instead of hardcoded Info↔Test cycling); Touch Test screen adds a centered "Back" button so users are no longer stuck on screens without web portal access
- **Multi-step screen history** — "navigate back" now supports a full history stack (up to 8 deep) instead of only remembering the single previous screen; splash screen is excluded from history
- **ESP32-P4 WiFi startup** — replaced blind 5 s delay with active ESP-Hosted link polling (exits as soon as the C6 co-processor's MAC is readable) and reduced inter-retry disconnect delay from 2 s to 500 ms; cuts typical WiFi connect time from ~16 s to ~8 s
- **Splash screen IP address** — boot splash now shows the device IP address (e.g. `Connected - 192.168.1.123` or `AP Mode - 192.168.4.1`) for 2.5 s before the "Ready!" message
- **Splash screen adaptive font** — splash status text and spinner scale with display resolution: three tiers (≤360 px, ≤480 px, ≥600 px) select progressively larger Montserrat fonts (14/18/24) and spinner sizes (40/50/60 px)

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
- **Splash spinner animation** — LVGL 9 spinner arc sweep changed from 60° to 270° (LVGL 9 requires 180–360° for smooth animation; 60° caused visible jumping)
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
