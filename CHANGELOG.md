# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

- **Web portal UI redesign** — rebuilt the web portal as a single-page application with a responsive two-level navigation shell. The outer sidebar organizes settings into 8 categories (Device, Display, Pads, Actions, Connectivity, Audio, Sensors, Firmware) with emoji icons; clicking a category item loads its content via AJAX fragment injection without a full page reload. Styled with Bootstrap 5.3 (CSS only, no JS bundle) plus a custom `portal-custom.css` overlay for portal-specific theming. Dark/light mode follows the OS preference. The sidebar collapses to a hamburger menu on mobile. Each feature module registers itself through a C++ component registry (`REGISTER_COMPONENT` macro with static constructors) — components declare their nav category, display name, sort order, config CRUD handlers, and custom REST actions. The framework dispatches routes generically: `GET/POST/DELETE /api/component/{id}/config` and `GET/POST/PUT /api/component/{id}/{action}`. Authentication is enforced centrally in the dispatchers (`portal_auth_gate`). Fragment HTML files use a `{{PLACEHOLDER}}` template system for shared partials (binding help, reboot overlay). Old multi-page URLs (`/home.html`, `/network.html`, `/firmware.html`) redirect to the shell with the correct hash fragment for backward compatibility. The portal stress test and documentation are updated to match the new asset paths.
- **Brightness and volume health binding keys** — new `[health:brightness]` (0–100, backlight level) and `[health:volume]` (0–100, audio volume, requires `HAS_AUDIO`) binding keys for displaying current device settings on pad buttons. Both keys also appear as conditional rows in the `[health:table]` / `[health:extended_table]` widget output — brightness shows on all display boards, volume only on boards with audio hardware.
- **Icon + center label co-display** — buttons can now show both an icon and a center label simultaneously. A new **Icon Position** setting controls layout: *Above label* (default, vertical stack), *Left of label* (horizontal row), or *Centered* (icon centered, label underneath). Configurable per-button and as a device-wide button default. The pad editor grid preview reflects all three layout modes. When a gauge, bar chart, or sparkline widget is active, the widget controls icon/label placement and the position dropdown is hidden. All six widget `createUI` signatures now receive the center label explicitly, replacing the gauge's previous child-scan heuristic.
- **Numeric rocker widget** — a new widget type that splits a button into 4 tap zones for fine and coarse numeric adjustment (`«  ‹  center  ›  »`). Uses one action template with a `{step}` placeholder — the widget substitutes the correct signed step value at tap time. Supports both horizontal (default) and vertical axis. Zone widths are pixel-clamped (12%/15% of span, min 40 px, max 80 px) so zones stay usable on any button size. The center zone works as a normal button with full tap and long-press action support; outer/inner zones use a dedicated adjustment action. Step values support decimals (e.g. 0.1, 0.5) and setting a step to 0 disables that zone pair, with the remaining zone expanding to fill the freed space. Visual indicators (`<<`, `<`, `>`, `>>` or `▲▲`, `▲`, `▼`, `▼▼`) mark each zone with configurable color and opacity.
- **Minimum brightness floor** — display brightness is now clamped to a minimum of 5% (`MIN_USER_BRIGHTNESS`) for button actions and NVS config load. Prevents the display from being set to zero brightness (effectively off) without using the screensaver. The transient REST API endpoint (`PUT /api/display/brightness`) allows brightness 0 so the browser can blank the backlight during save sequences on MIPI-DSI boards. The floor is board-overridable via `#ifndef` guard.
- **Bindable action values** — all action value fields now support `[scheme:key]` binding templates, resolved at dispatch time. MQTT topic and payload, BLE key sequence, beep pattern, volume/brightness values, timer values, and all notify fields can use live data from any binding scheme. For example, an MQTT publish action can use `[health:cpu]` as its payload, or a beep pattern field can reference `[mqtt:home/alerts/pattern]`. The pad editor shows an `fx` badge on supported fields. Previously only notify text and color fields supported bindings.
- **Screensaver sleep optimization** — when the screensaver is fully asleep, the LVGL render loop throttles from ~50 fps to 5 Hz (`SCREENSAVER_SLEEP_TICK_MS`, default 200 ms), `screen->update()` calls are gated, and the display panel is put into hardware sleep mode via `displaySleep()`/`displayWake()` on the `DisplayDriver` HAL. CPU usage drops from ~30% to ~2% during sleep. Panel sleep is implemented for all active drivers: MIPI-DSI (DCS sleep-in/out), TFT_eSPI (command 0x10/0x11), Arduino_GFX (bus command), and ST77916 QSPI. FPS correctly reports 0 during sleep. The tick interval is board-overridable via `#ifndef` guard.

### Changed

- **GitHub Actions Node.js 24 compatibility** — updated third-party workflow actions to Node.js 24-compatible versions: `peter-evans/find-comment` v3→v4, `peter-evans/create-or-update-comment` v4→v5, `softprops/action-gh-release` v1→v2 (was Node 12), `actions/upload-pages-artifact` v3→v4 (with `include-hidden-files: true` for `.nojekyll`). Moved `GITHUB_TOKEN` from env var to `token` input in the release workflow. Official `actions/*` repos remain at current versions (Node 24-compatible when forced). (Closes #23)
- **Timer binding numeric default** — `[timer:N]` (no format parameter) now resolves to raw seconds with decisecond precision (e.g. `"45.3"`, `"0.0"`, `"-5.3"`) instead of `mm:ss` formatted text. This makes timer bindings usable as data sources for gauge, bar chart, and sparkline widgets, which parse values with `strtof()`. All named formats (`mm:ss`, `hh:mm:ss`, `ss`, `mm:ss.d`) continue to work when explicitly requested. The portal validator now allows `[timer:N]` and `[timer:N;ss]` in widget data bindings while still rejecting non-numeric display formats.
- **Per-board intermediate build cache** — `build.sh` now passes `--build-path build/<board>/intermediate/` to `arduino-cli compile`, giving each board its own persistent object cache. Switching between boards no longer invalidates the entire library cache, making incremental rebuilds seconds instead of minutes. Disk usage grows to ~170 MB per board (~1 GB for 6 boards). `clean.sh` already removes the full `build/` tree including intermediate caches. CI builds are unaffected. (Closes #25)
- **Unified volume, brightness, and timer action patterns** — volume and brightness actions now use a consistent set/adjust model. "Set" assigns an absolute value; "Adjust" adds a signed delta. Both appear as separate sub-options under System Command in the pad editor. Timer actions gain a new "Set Countdown" command alongside the existing "Adjust Countdown". All three action types store values as strings, enabling the numeric rocker `{step}` placeholder for hardware-driven adjustment. The old `up`/`down` volume and brightness modes and the packed `id:command:value` timer format are replaced (breaking change for existing pad configs using these actions).
- **Volume and brightness moved to System Command** — the "Set Volume" and "Set Brightness" action types are now sub-options of System Command in the pad editor, alongside Reboot Device, Reconnect WiFi, and Enable Screensaver. This consolidates all device-level operations under a single dropdown. On the wire, volume and brightness actions still use their native `type:"volume"` / `type:"brightness"` JSON format for backward compatibility with existing dispatch logic.

### Fixed

- **Brightness lost after screensaver wake** — runtime brightness changes (button actions, REST API) were written directly to the display driver, bypassing the screensaver's internal tracking. When the screensaver activated and then woke, the display reverted to the original NVS-configured brightness instead of the user-chosen level. The fade-out also jumped to the stale tracked value before animating. All brightness changes now route through `screen_saver_manager_set_brightness()` which updates the in-RAM config, internal tracking, and driver atomically. If the display is asleep, setting brightness triggers a wake to the new level.
- **Vertical numeric rocker sign convention** — tapping the bottom zone of a vertical numeric rocker now correctly decreases the value (was increasing). Top zones increase, bottom zones decrease, matching the natural expectation of "up = more".
- **Pad editor long-press action slots** — opening the button editor showed all 3 long-press action slots expanded instead of only slot 1 (with the remaining slots addable via the "+ Add" link). The `padWidgetTypeChanged()` function unconditionally force-showed all LP wraps, overriding the progressive disclosure set during dialog initialization. Now only slot 0 is ensured visible; slots 1–2 respect the dialog's data-driven visibility. Also removed a dead no-op loop over tap action wraps and made the tap add-action link use the same `padUpdateAddLink()` pattern for consistency.

## [1.14.0] - 2026-04-19

### Added
- **System command button action** — new "System Command" action type lets you assign device-level operations to any button tap, long-press, or swipe gesture. Three commands available: **Reboot Device** (with 200 ms flush delay), **Reconnect WiFi** (triggers a clean disconnect + reconnect through the tiered escalation state machine), and **Enable Screensaver** (immediately activates the screensaver). Unknown commands are logged and ignored for forward compatibility. Configurable in the web portal action editor with a command sub-selector dropdown.

- **Pad building blocks** — pre-configured groups of buttons that you can insert into any pad from the **More ▾** menu. Select a block, click an empty cell to place it, and the block's buttons, spans, bindings, and actions are inserted automatically. A live ghost overlay shows where the block will land (green for valid, red for overlap or out-of-bounds). Press Escape or Cancel to exit placement mode. Blocks check grid dimensions, free cell count, and the 64-button limit before offering placement. Ships with a built-in **Countdown Timer** block (3 rocker buttons + 2-column timer display with `font_family:segment`) and a **System Info** block (network bar, triple CPU/RAM/PSRAM gauge, chip info, and uptime — all powered by health bindings). The block catalog uses a registration API (`pad_block_register()`) so feature branches can add their own blocks without merge conflicts.
- **Notification bubble action** — a new "Show Notification" button action that displays a floating message bubble on the device screen. Supports customizable text, duration, text/background/border colors, opacity, font size, and location (top, center, bottom). All text and color fields support binding templates for dynamic content. Duration `0` creates a persistent notification that stays until tapped. Font size defaults to the same scale-tier font used by button center labels (scales with display resolution). Bubble sizing is responsive — minimum 40% screen width with dynamic padding scaled to resolution. Also exposed as a Home Assistant text entity (`~/notify/set`) that accepts plain text (default styling) or JSON for full control. Empty message dismisses the active bubble.
- **Rocker widget** — a new widget type that splits a button into two tap zones. Tap the top half to trigger one set of actions, tap the bottom half to trigger another — ideal for brightness up/down, volume +/−, or any value you want to adjust from a single button. Reuses existing tap and long-press action slots for the two zones (no schema changes). Supports both vertical (up/down, default) and horizontal (left/right) axis. Visual cue: small chevron indicators (▲▼ or ◄►) at the edges. Tap flash covers only the tapped half. Zone B (down/right) uses the device long-press beep pattern for a distinct audio cue. Configure axis, indicator color, and opacity in the button editor under Rocker Settings.
- **Gauge needle cutoff** — new `Needle Cutoff` parameter (0–99%) removes the inner portion of the gauge needle starting from the center point, preventing overlap with the center label or icon. Set in the pad editor under the gauge widget section. Default 0 preserves the existing full-length needle.
- **Per-board grid limits** — `MAX_PAD_BUTTONS`, `MAX_GRID_COLS`, and `MAX_GRID_ROWS` are now overridable per board via `board_overrides.h`. The pad editor dynamically populates grid size dropdowns from the device's `/api/info` endpoint. The jc3636w518 (360×360 round display) is set to 5×5 max grid and 3 LRU cached pads, reducing peak PSRAM usage from ~5.3 MB to ~0.8 MB.
- **Brightness button action** — new `brightness` action type sets, increases, or decreases display backlight brightness (configurable step size or absolute set, default ±10%). Values clamped to 0–100%. Session-only — not persisted to NVS.
- **Expert panel code review system** — new pre-commit review infrastructure with 9 specialized expert reviewers (architecture, binding-system, dead-code, docs, DRY, ESP32, KISS, naming, performance). Run via the `sanitycheck` prompt or `Code Review` agent. Findings are presented in a single numbered table with batch fix/skip triage. Each expert is defined by a standalone `.instructions.md` file in `.github/instructions/review-experts/`, making the system extensible by adding new files.
- **Scoped instruction files** — extracted domain-specific conventions from `copilot-instructions.md` into 8 standalone `.instructions.md` files with `applyTo` frontmatter for automatic context-scoped loading. Covers web portal, config settings, display/touch HAL, terminology, agent guidelines, diagramming, compile-time flags, and binding system. Reduces base agent context from 52 KB to 12 KB; domain knowledge loads only when editing relevant files.
- **Drag-to-resize buttons** — in the pad editor, hover over any button to reveal thin drag handles on all four edges. Drag an edge outward to grow the button into adjacent empty cells, or inward to shrink it. The resize snaps to cell boundaries with an early threshold (~20% into the next cell) for a responsive feel. Occupancy validation prevents overlapping other buttons. Works with both mouse and touch input.
- **Drag-and-drop button move** — in the pad editor, drag any configured button to an empty cell to move it. The button keeps its full configuration (labels, actions, colors, icon, widget). Multi-cell spans are preserved when the target has room, otherwise the button is placed as 1×1. A green dashed outline highlights valid drop targets while dragging. Drag-and-drop is automatically disabled during block placement mode and does not apply to template ghost buttons.
- **Event-driven WiFi reconnection with tiered escalation** — replaced the blocking WiFi reconnect watchdog with a non-blocking state machine driven by `WiFi.onEvent()` callbacks. Display, touch, and screensaver remain fully responsive during any WiFi outage. Three recovery tiers: Tier 1 (0–60 s) trusts SDK auto-reconnect; Tier 2 (60 s–5 min) issues non-blocking reconnects with exponential backoff (10 s → 60 s cap); Tier 3 (5–10 min) performs a hard WiFi stack reset (`WIFI_OFF` cycle on S3, safe disconnect+begin on P4). Device reboots after 10 min total outage. On reconnection: mDNS restarted, RSSI cache refreshed, recovery logged with tier and duration. Gateway ping liveness detection triggers the state machine. All tier thresholds are board-overridable via `#ifndef` guards (`WIFI_TIER1_DURATION_MS`, `WIFI_TIER2_DURATION_MS`, `WIFI_REBOOT_AFTER_MS`, `WIFI_TIER2_BACKOFF_BASE_MS`, `WIFI_TIER2_BACKOFF_MAX_MS`). P4 boards skip `WIFI_OFF` toggle for SDIO stability. Thread safety uses `std::atomic<bool>` flags. Backoff and tier logic covered by 23 host-native unit tests.

### Changed

- **Volume button action no longer persists to NVS** — volume changes from the `volume` button action are now session-only, matching the new brightness action behavior. Volume resets to the NVS-saved value on reboot.

### Fixed

- **Widget animation stuck on rapid bindings** — bar chart and gauge widgets appeared frozen when their data binding resolved faster than the configured animation duration (e.g. `[time:%ms]` every ~10–20 ms vs 300 ms ease-out). Each `lv_anim_start()` replaced the running animation and reset the ease-out curve before any visible progress. Widgets now track the last update timestamp and fall back to direct-set when updates arrive faster than `anim_ms`. Slow-updating bindings (MQTT sensors) still animate smoothly.
- **Compile-time flags report for inheriting boards** — `compile_flags_report.py` now resolves relative `#include` directives in board override files, so boards that inherit from a parent (e.g. `jc4880p433-hx711` including `../jc4880p433/board_overrides.h`) correctly report inherited flags like `HAS_DISPLAY` and driver selectors. Previously these boards showed `(none)` for active features and `—` for driver selectors in build logs and the compile-time flags doc.
- **P4 letterbox scaling black bars** — letterbox mode on ESP32-P4 boards produced unnecessary black bars on both axes because the PPA hardware scaler quantises to m/16 steps, undershooting the ideal scale on most aspect ratios. Replaced PPA with a CPU bilinear RGB565 scaler for letterbox mode while keeping the fast HW JPEG decoder. Cover mode continues to use PPA where quantisation is harmless.
- **PadConfig OOM log flood** — when `buildTiles()` failed to allocate PadConfig or binding arrays, it returned without setting `tilesBuilt`, causing the LVGL task to retry every frame (~30 Hz) and flood the serial log. Now marks `tilesBuilt = true` on OOM so the error logs once per config generation change.
- **Template-inherited icons showing wrong image** — pads using a template pad could display stale or wrong icons for inherited buttons after editing. The icon cache retained old page-local entries even after icon files were deleted, and the preload path skipped refresh for keys already in cache. Fixed by invalidating page-prefixed cache entries when page icons are deleted and always reconciling preloaded icons from the current on-disk or template source.
- **Health table builder missing board config include** — `health_table_builder.cpp` was missing `#include "board_config.h"`, which defines the `HAS_DISPLAY` guard used at the top of the file. Could cause compilation failures depending on include order.

## [1.13.0] - 2026-04-11

### Added
- **CI test job** — host-native unit and integration tests now run automatically on every PR in the GitHub Actions build workflow. Tests execute in parallel with firmware builds, and test failures block the PR summary comment. ArduinoJson headers are installed on-the-fly for tests that need them.
- **Hardware JPEG decode + PPA scaling on ESP32-P4** — image fetch slots on ESP32-P4 boards now use the hardware JPEG decoder (`driver/jpeg_decode.h`) and PPA SRM scaler (`driver/ppa.h`) instead of software tjpgd + CPU bilinear interpolation. Decode time drops from ~50 ms to ~10 ms per frame. The PPA handles both cover and letterbox scale modes with 1/16-step precision. Output buffers are 64-byte cache-line aligned for PPA DMA. The hardware handles are lazy-initialised once and reused across frames. PNG images and all non-P4 boards continue to use the unmodified software path. Falls back transparently to software on any hardware failure. Gated by `CONFIG_IDF_TARGET_ESP32P4`.
- **MJPEG streaming for camera buttons** — when a button's image URL serves a `multipart/x-mixed-replace` MJPEG stream, the device opens a persistent TCP connection and reads frames continuously instead of making repeated per-frame HTTP requests. Eliminates per-frame TCP setup and camera-side capture latency, improving camera button frame rates from ~2–3 fps (snapshot mode) to ~8–15 fps (streaming mode). Mode is auto-detected from the HTTP `Content-Type` response header — no config change needed, existing snapshot URLs continue to work unchanged. Set **Refresh Interval to `0`** for streaming. Compatible with go2rtc, Frigate, and any `multipart/x-mixed-replace` source. Also reduces image fetch internal RAM usage: max slot count reduced 64→16, slot and connection arrays moved from static `.bss` to PSRAM allocation, and MJPEG stream close bypasses the HTTPClient data drain to speed up socket cleanup.
- **Boot actions** — configure up to 3 sequential actions to run when the device boots (e.g., play a startup sound, publish an MQTT message, navigate to a specific pad). Uses the same action system as buttons and swipe gestures. Configure in the new Boot Actions section on the Home page, or via the REST API (`GET/POST /api/boot-actions`). Stored on LittleFS at `/config/boot_actions.json`. Dispatched after the first screen is shown during boot.
- **Sound player** — play uploaded MP3 files through the speaker. Upload `.mp3` files (≤512 KB) via the new Sound Files section on the Home page, or the REST API. Assign a "Play Sound" action to any button with optional volume override. Also available as an MQTT text entity (`~/audio/sound/set`) for Home Assistant integration. Uses the minimp3 decoder (CC0 license) with linear interpolation resampling to 16 kHz. MP3 header validation rejects non-MP3 uploads. Compile-time gated by `HAS_SOUND_PLAYER` (defaults to `HAS_AUDIO`).
- **Custom display fonts** — three additional font families for button labels, ideal for hero values and specialized displays. Use `font_family:dseg7` (7-segment LCD), `font_family:bebas` (bold condensed), or `font_family:doto` (dot-matrix / pixel) in label style overrides. Aliases: `segment` for dseg7, `pixel` for doto. All families share the same 7 sizes as the built-in Montserrat font (12, 14, 18, 24, 32, 36, 48). Gated by `HAS_CUSTOM_FONTS` (defaults to `HAS_DISPLAY`). Font C sources are generated by `tools/generate-fonts.sh` from TTF files via `lv_font_conv`.
- **Deferred flash I/O in LVGL task** — action dispatch now defers NVS writes and LittleFS access to the main loop, preventing crashes on ESP32-P4 boards where the LVGL task stack resides in PSRAM.
- **Device-level timer configuration** — timer mode (count-up / countdown), countdown duration, and expire actions are now configured once at the device level on the Home page, instead of being scattered across individual button actions. Expire actions support the full action system (play sound, MQTT publish, screen navigation, beep pattern, or any combination of up to 3 actions). Stored on LittleFS at `/config/timers.json` with a dedicated REST API (`GET/POST /api/timers`). Timer button actions are simplified to runtime control only (toggle, start, stop, pause, reset, adjust).
- **Table widget** — a new button widget for rendering structured multi-column table data, with configurable table font style and optional scrolling.
- **Health table bindings** — added `health:table` and `health:extended_table` binding keys for structured table payloads, including table-widget validation and binding-reference documentation in the web portal.

### Changed
- **Label style `font` renamed to `font_size`** — the label style DSL property for overriding font size is now `font_size` (e.g., `font_size:24;align:left`). The old `font` key still works for backward compatibility but is no longer documented.
- **Shared action parse/serialize** — extracted `action_parse()` and `action_to_json()` into a shared `action_parse.cpp/h` module, eliminating duplicated code between pad_config, swipe_config, and the new boot_actions module. Also fixes a drift bug where swipe actions were missing `sound_file`, `sound_volume`, and `timer_*` field handling.
- **Timer action simplified** — removed `Set Countdown`, `Set Mode`, `Default Countdown`, `Expire Beep Pattern`, and `Expire Beep Volume` from the button timer action editor. These are now part of the device-level timer configuration. The `timer_countdown`, `timer_expire_beep`, and `timer_expire_volume` fields have been removed from `ButtonAction`.

### Removed
- **Per-button timer configuration fields** — `timer_countdown`, `timer_expire_beep`, and `timer_expire_volume` removed from `ButtonAction` struct. Use the device-level timer configuration on the Home page instead.

## [1.12.0] - 2026-03-27

### Added
- **Widget animations** — bar chart and gauge widgets now smoothly animate to new values instead of jumping. Arcs sweep, needles rotate, and bars grow/shrink with configurable ease-out transitions. Set **Animation (ms)** per widget in the pad editor (0–5000 ms, default 300, 0 = instant). First data arrival always snaps immediately to avoid animate-from-zero on screen load. Zero-centered gauge arcs animate correctly in both directions.
- **Image fetch frame drop telemetry** — the image fetch subsystem now tracks per-slot frame drops (when LVGL hasn't consumed a frame before the next one arrives). Drop counts are exposed in `/api/health` under `image_fetch.slot_N_drops` and logged at debug level. Adds `image_fetch_get_drops()` public API. Closes #18.
- **Template pads** — set any pad as a "template" for another pad. Buttons from the template pad automatically appear in empty grid positions, shown as ghost overlays in the editor. Template buttons and bindings are merged at load time (target pad always wins on conflict). No chaining — a template pad's own template reference is ignored. Configure via the new **Template Pad** dropdown on the Pads page.
- **Device-level button defaults** — set default background, text, and border colors, border width, corner radius, and label styles once for the entire device. All buttons on all pads inherit these defaults automatically, saving you from repeating the same appearance settings on every button. Per-button overrides still take precedence. Configure in the collapsible **Button Defaults** section at the bottom of the Pads page. Stored as a separate JSON file on LittleFS (`/config/button_defaults.json`) with a dedicated REST API (`GET/POST /api/button-defaults`).
- **Reset to default** — when a button has a custom color or border override, a small ↩ link appears next to the field label in the button editor. Click it to revert to the pad default.
- **Audio support for JC4880P433** — enabled ES8311 codec + power amplifier on the GUITION JC4880P433 board. Pin mapping sourced from the [community BSP](https://github.com/csvke/esp32_p4_jc4880p433c_bsp): MCLK=GPIO13, BCLK=GPIO12, LRCK=GPIO10, DOUT=GPIO9, PA enable=GPIO11. All existing audio features (beep patterns, tap/long-press cues, MQTT siren/volume, timer expiry beep) now work on this board.
- **Guition JC1060P470C board support** — new ESP32-P4 board with 7.0" 1024×600 IPS display (JD9165 MIPI-DSI driver), GT911 capacitive touch, and ES8311 + NS4150B audio. Adds `jd9165_dsi_driver` display backend. Configured in portrait orientation (600×1024) by default.
- **MIPI-DSI portrait rotation via PPA** — ESP32-P4 boards with MIPI-DSI displays can now run in portrait orientation using the ESP32-P4's PPA (Pixel Processing Accelerator) hardware for async block rotation. LVGL renders in portrait coordinates; the PPA SRM module rotates each flush block to landscape for the physical panel, then DMA2D transfers to the framebuffer — all asynchronous with no CPU copy. Achieves ~25 FPS in portrait (vs ~30 FPS landscape). Enabled per-board by setting `DISPLAY_ROTATION` to 1 or 3 in `board_overrides.h`.
- **ES8311 codec init fixes** — aligned codec initialization with the official Espressif ADF driver: corrected REG37 DAC source select (0x48→0x08), added REG44 writes for I2C noise immunity and internal reference, fixed COEFF_DAC_OSR (0x10→0x20), and added 50 ms MCLK stabilization delay. Improves reliability on all ESP32-P4 audio boards.
- **Multi-action buttons** — each button now supports up to 3 sequential actions per tap and per long-press (previously limited to 1). Actions execute in order: e.g., MQTT publish → play beep → navigate to screen. Useful for combined workflows like tare-then-start-brew, or publish-then-navigate. The pad editor shows only the first action by default; use the "+ Add tap/long-press action" link to reveal additional slots.
- **Hero label typography controls** — label style overrides now support a larger built-in font size `font:48` for big, high-emphasis button labels (for measurements, status headlines, and other hero text use cases).
- **Label style `font_upscale`** — new optional runtime scale modifier for label text (`font_upscale:1.0` to `font_upscale:2.0`). Works with both automatic scale-tier fonts and explicit `font:<size>` overrides, enabling combinations like `font:36;font_upscale:1.4`.
- **Gauge target zone & markers** — gauges now support a bindable target value with visual markers across all active rings. Configure a target value (e.g., setpoint from MQTT), and the gauge draws a tick mark at that position plus an optional colored zone centered on it. The target zone angle specifies the total zone width in degrees (divided by 2 for ± rendering). Both the marker color and zone color support binding expressions for dynamic theming. Boundary ticks at the zone edges help delineate the target range. Useful for thermostat setpoints, power budget targets, or any "desired value" overlay.
- **Binding syntax validator** — binding fields across the pad editor and Home page now validate syntax in real time as you type. Catches bracket mismatches, unknown scheme names (with fuzzy "did you mean?" suggestions), incorrect parameter counts, invalid health/timer keys, malformed printf format strings (e.g., `%.0f`, `%d`), and expression syntax errors (e.g., `[expr:1 +]`). Nested bindings inside `[expr:]` are recursively validated. Errors appear as inline red messages below the field, and saving is blocked until errors are resolved. Validation runs on a 400 ms debounce while typing and immediately on blur.

### Changed
- **Faster boot sequence** — reduced time-to-first-screen by ~6 seconds on ESP32-P4 boards. WiFi hardware initialization (SDIO link to C6 co-processor) now starts before display and config init, overlapping the ~2–5 s link bring-up with other work. Reduced post-Serial settle delay from 1000 ms to 100 ms. Splash screen IP/status display shortened from 2500 ms to 500 ms and the separate "Ready!" delay removed entirely. MQTT discovery now runs during the splash phase rather than adding to it.
- **Simplified audio defaults** — new firmware defaults for a better out-of-box experience: volume 50% (was 70%), tap beep `500:40` (was silent), long-press beep `500:40 60 1000:40` (was silent). Beep patterns are now a clean 2-level system: device default (Home page) → action override. Per-button beep overrides (the "Audio Feedback" section in the button editor) have been removed entirely. To customize audio for a specific button, add a Play Beep or Play Sound action — this automatically suppresses the device-level feedback beep. Sound actions (`ACTION_TYPE_SOUND`) now also suppress the feedback beep, fixing the previous bug where the beep and MP3 file would overlap.
- **JSON format**: Button actions are now stored as arrays (`"actions": [...]` / `"lp_actions": [...]`). The firmware transparently reads the old single-object format (`"action": {...}` / `"lp_action": {...}`) for backward compatibility with existing configs on the device.

## [1.11.0] - 2026-03-18

### Added
- **Timer engine** — 3 independent timers with count-up (stopwatch) and countdown modes. Assign timer actions to buttons to toggle, start, stop, pause, resume, reset, or lap timers directly from the touch screen. Countdown timers display negative values (e.g., "-0:05") when running past zero.
- **Timer binding scheme** — display timer values on any button label using `[timer:N]` bindings. Supports multiple formats (`mm:ss`, `hh:mm:ss`, `ss`, `mm:ss.d`) and state queries (`[timer:N_state]`, `[timer:N_expired]`, `[timer:N_mode]`).
- **Timer countdown presets** — configure a default countdown duration per button. When navigating to a pad, the first button referencing each timer automatically sets its countdown preset (only when the timer is fresh/stopped at zero).
- **Timer expiry beep** — configure a beep pattern and optional volume override that plays when a countdown timer reaches zero. Edge-triggered to fire exactly once per countdown cycle.
- **Timer adjust countdown** — new `Adjust Countdown Time` action adds or subtracts seconds from a running countdown preset. Useful for "+15s" / "-10s" quick-adjust buttons.
- **Configurable swipe actions** — swipe left, right, up, or down on any screen to trigger actions with full button parity (screen navigation, back, MQTT publish, BLE key sequence, BLE pair). Default: swipe right = back. Configure via the new Swipe Actions section on the Home page or the REST API (`GET/POST /api/swipe-actions`). Stored on LittleFS with 300ms debounce to prevent multi-fire.
- **Shared action dispatch** — extracted button and swipe action execution into a shared `action_dispatch` module, eliminating code duplication between pad buttons and swipe gestures.
- **Shared action editor UI** — extracted the action type/screen/MQTT/key editor into a reusable `portal_action_editor.js` component used by both the button editor dialog and the swipe actions form.
- **WiFi gateway ping liveness check** — the WiFi watchdog now pings the default gateway every 30 seconds to detect "ghost connected" states where `WiFi.status()` reports connected but the radio link is actually dead (common on ESP32-P4 with ESP-Hosted). After 3 consecutive ping failures (~90s), forces a WiFi disconnect and reconnect. Runs as a low-priority async task with zero impact on display rendering. Silent in the happy path — only logs on failure or recovery.
- **Audio subsystem** (ESP32-P4 boards) — ES8311 codec + NS4150B amplifier over I2S with async FreeRTOS playback. New beep pattern DSL (`freq:dur` tones, bare `dur` silence gaps, space-delimited). Device-level volume control (0–100%, NVS-persisted) with slider on the Home page.
- **Beep button action** — new `beep` action type plays a configurable beep pattern with optional per-action volume override.
- **Volume button action** — new `volume` action type sets, increases, or decreases device audio volume (±10% steps or absolute set). Volume changes persist to NVS.
- **Configurable audio cues** — device-level tap and long-press beep patterns that play automatically when a button with an action is pressed. Configured in the Home page Audio section using the beep pattern DSL (e.g. `800:80` for a quick tap click, `600:40 40 600:40` for a double-chirp on long-press). Buttons with no action configured are completely inert — no visual tap flash and no audio cue. Per-button overrides in the button editor's Audio Feedback section let you customize or suppress (enter `none`) the sound for individual buttons. Swipe gestures also use the device-level tap beep. Beep actions are auto-suppressed to avoid double-beep.
- **Home Assistant audio integration** — the device speaker is controllable from Home Assistant via auto-discovered MQTT entities (no YAML needed):
  - **Siren** entity — looping tone with preset selection (default, alert, doorbell, warning, custom), volume override, and optional auto-stop duration
  - **Volume** number entity — set device volume (0–100%), persisted across reboots
  - **Custom Tone** text entity — define a beep pattern DSL string, then play it via the siren with `tone: "custom"`
  - **Beep / Beep Double / Beep Triple** button entities — one-shot beep triggers
  - See [Home Assistant Integration Guide](docs/ha-integration-guide.md) for usage examples and automation recipes

### Fixed
- **BLE auto-re-pair on stale bonds** — when Windows (or another host) drops its BLE bond after a firmware update (due to GATT database hash change), the ESP32 now detects that the reconnecting peer matches the previously bonded owner and automatically clears the stale bond and enters a 60-second re-pair window. Previously, the device would reject the now-unbonded host indefinitely, requiring manual pairing via the web portal. The owner's identity address is persisted in NVS for cross-reboot detection.
- **Missing auth on icon install route** — restored `portal_auth_gate()` on `POST /api/icons/install`.
- **Sparkline min/max labels overlapping current-value labels** — clamped min/max marker labels to the chart plot area so they no longer intrude into the right-side margin reserved for current-value line labels.
- **Sparkline current dot disconnected from line** — changed the X-axis mapping to use `chart_w - 1` instead of `chart_w`, so the rightmost data point stays on the last pixel inside the clipped chart area. Fixes the visual gap between the line endpoint and the current-value dot, and prevents LVGL's rounded line cap from being clipped at the chart edge.

## [1.10.0] - 2026-03-16

### Added
- **WiFi health binding keys** — new `[health:wifi_connected]` (`ON`/`OFF`) and `[health:wifi_ssid]` (connected network name) binding keys for building WiFi status indicators directly on pad buttons
- **BLE HID keyboard** — the macropad can now act as a Bluetooth keyboard. Assign a `key` action to any button to send keystrokes (single keys, modifier combos, media keys, or multi-step sequences) to a paired host. Assign a `ble_pair` action to clear bonds and open a fresh 60-second pairing window. Features:
  - Key sequence DSL with text literals, modifier keys, consumer/media keys, and delays
  - Single-owner pairing policy: one bonded host at a time, unbonded peers rejected outside the pairing window
  - Runtime enable/disable toggle on the Home page (disabled by default, saves ~70 KB RAM when off; requires reboot)
  - BLE state exposed in `/api/health` (`ble_status`, `ble_state`, `ble_name`, `ble_pairing`, `ble_bonded`, `ble_encrypted`, `ble_peer_addr`, `ble_peer_id_addr`)
  - Health binding support: `[health:ble_status]`, `[health:ble_state]`, `[health:ble_name]`, `[health:ble_pairing]`, etc.
  - New BLE Keyboard section on the Home page with connection status, peer details, and "Pair New Device" button
  - New `POST /api/ble/pairing/start` endpoint for triggering pairing from the portal
  - **ESP32-P4 only** — disabled on ESP32-S3 boards (`HAS_BLE_HID false`) due to insufficient internal RAM for NimBLE + WiFi + display concurrently
  - Improved button editor wording: "Send BLE Keys", "Start BLE Pairing", "Keys to Send" with contextual hint when BLE actions are selected
- **Sparkline current-value labels** — per-line labels that display resolved binding text in a right-side margin, tracking the Y position of the most recent data point. Supports up to 3 labels (one per line), full binding expressions, configurable label width (0 = 50px default), and greedy collision avoidance when labels overlap. Label color inherits the line color. Label-only redraws are throttled to 1 Hz to minimize CPU usage
- **16-pad support with LRU memory management** — expanded from 8 to 16 configurable pads. Heavy per-pad arrays (bindings, tiles, color/number/state bindings) are now lazily allocated in PSRAM on first visit and freed via an LRU-8 eviction cache, keeping peak memory usage comparable to the previous 8-pad implementation. Key changes:
  - `MAX_PADS` raised to 16 (overridable per board in `board_overrides.h`)
  - `MAX_SCREENS` derived from `MAX_PADS + MAX_NON_PAD_SCREENS` (default 26)
  - `SCREEN_HISTORY_MAX` controls both back-navigation depth and LRU cache size (default 8)
  - `DATA_STREAM_MAX_STREAMS` raised to 64 for sparkline widget headroom
  - Pad screen IDs and names generated dynamically (`pad_0`..`pad_15`, `Pad 1`..`Pad 16`)
  - Web portal pad dropdown populated dynamically from `/api/info` `max_pads` field
  - Device export/import loops use `max_pads` instead of hardcoded 8

### Changed
- **Zero-allocation health binding resolve** — WiFi SSID, IP address, and connection status are now cached in static buffers during the 2-second telemetry refresh instead of allocating Arduino `String` objects on every resolve call, eliminating heap fragmentation in the LVGL render loop
- **Expanded health binding keys for system pad use** — added 14 new `[health:]` binding keys for building system-status pads with bar charts and labels. New keys fall into three categories:
  - **Memory totals** (for bar chart `widget_bar_max`): `heap_total`, `heap_internal_total`, `psram_total` — read from `heap_caps_get_total_size()` cached once at init, zero runtime cost
  - **Memory used** (for bar chart `data_binding`): `heap_internal_used`, `psram_used` — computed from `total − free` each refresh cycle
  - **Static device info** (cached once at init): `chip`, `chip_rev`, `cores`, `cpu_freq`, `flash_size`, `firmware`, `board`, `mac`, `reset_reason` — zero ongoing overhead
  - All new keys documented in the Binding Reference tooltip (System Info and Memory Totals sections) and user guides
- **Bindable min/max values for all widgets** — bar chart, gauge, and sparkline min/max scale fields now accept binding expressions (e.g. `[health:psram_total]`) in addition to static numbers. This enables dynamic scaling for system-status pads where the maximum value depends on device hardware. Fields are resolved at render time using the existing `resolve_number()` infrastructure with negligible overhead. Sparkline auto-scale (empty = NAN) is preserved.
- **Terminology consolidation** — standardized user-facing terminology across the entire project to a consistent hierarchy: **Screen** → **Pad** → **Button** → **Widget**. Retired "page" (as synonym for pad), "tile" (as synonym for button), and "cell" (in user-facing text). Key changes:
  - Renamed `PadPageConfig` → `PadConfig` and `MAX_PAD_PAGES` → `MAX_PADS` throughout source code
  - Renamed REST endpoint `/api/pad/tile_sizes` → `/api/pad/button_sizes` with JSON fields `button_w` / `button_h` (breaking API change)
  - Updated web UI: "Button Type" → "Widget", "Normal Button" → "None", "Click a cell to edit its button" → "Click a button to edit it"
  - Updated JS: `padTileSizesCache` → `padButtonSizesCache`, `padGetTileSizes` → `padGetButtonSizes`
  - Updated user-facing docs (`pad-editor-guide.md`, `first-time-setup.md`, `README.md`) to use consistent terminology
  - Added enforced **Terminology Conventions** section to `.github/copilot-instructions.md` with definitions, retired terms, and rules

### Fixed
- **GitHub firmware update crash on ESP32-P4 boards** ([#14](https://github.com/jantielens/esp32-macropad/issues/14)) — OTA updates via GitHub Pages crashed on ESP32-P4 boards due to two independent bugs:
  1. The firmware update task used a PSRAM stack (`rtos_create_task_psram_stack`), but `Update.write()` triggers SPI flash writes that disable the SPI cache — making the PSRAM stack inaccessible and causing an assert in `cache_utils.c`. Fixed by forcing the task to use an internal RAM stack via `xTaskCreate()`.
  2. Concurrent image fetching during OTA exhausted the SDIO WiFi driver's RX buffer pool (the ESP32-P4 uses an external ESP32-C6 for WiFi over SDIO). Fixed by suspending image fetch slots before starting the OTA download and restoring them on error.

### Removed
- **NimBLE / BTHome BLE advertising** — removed the entire BLE BTHome v2 advertising subsystem (`ble_advertiser.cpp/h`, `NimBLE-Arduino` library dependency, `HAS_BLE` compile flag, `PublishTransport` enum, BLE timing config fields, web portal Transport Mode selector and BLE Advertising settings). This simplifies the codebase ahead of the BLE HID keyboard feature. Affected 23 files across firmware, web portal, build system, and documentation.

### Improved
- **Stable BLE identity** — BLE pairing no longer rotates the device's identity address or appends an address suffix to the device name. The device always advertises with the same hardware address and the configured device name, matching how standard BLE HID keyboards work. This fixes issues with Windows caching stale BLE device entries after re-pairing.
- **Quieter BLE re-pairing** — after triggering "Pair New Device", the old host (especially Windows) may keep reconnecting briefly with stale keys. These failed reconnection attempts are now logged at DEBUG level instead of WARN to avoid flooding the serial monitor. The old host eventually backs off, or the user removes the device from their Bluetooth settings before pairing again.
- **Health binding reference in tooltip** — replaced the single-line key list with organized tables grouped by category (System, Memory, WiFi, BLE), each key with a brief description of its value/format. Added color-mapping examples for WiFi status (green/red via `wifi_connected`) and BLE status (5-state color map via `ble_status`)
- **Pad editor Copy keeps dialog open** — the Copy button now saves the current button state and copies to clipboard without closing the editor, so you can continue editing or immediately paste elsewhere
- **Pad editor Paste keeps dialog open** — pasting a button now re-opens the editor showing the pasted content, so you can review or tweak before closing
- **Pad editor Paste preserves col/row span** — copied buttons now retain their column and row span values in the clipboard. On paste, spans are applied if they fit at the target position (within grid bounds and no overlap with existing buttons); otherwise they gracefully fall back to 1×1
- **Pad editor Fill strips span** — "Fill pad with copied button" now strips col/row span values so every cell gets a clean 1×1 button
- **Auto-quoting in expression bindings** — resolved binding text values (e.g. `connected`, `Living Room`) are now automatically quoted before expression evaluation in `[expr:]` bindings. Users can write `[expr:[health:ble_status]=="connected"?"#00ff00":"#ff0000"]` without needing to quote the inner binding. Handles spaces, hyphens, dots, and any special characters in resolved values. Numeric values pass through unquoted so arithmetic continues to work

---

## [1.9.0] - 2026-03-13

### Changed
- **Refactor: split large/complex source files** — four high-maintenance-risk files have been split into focused, single-concern units with no behavior changes:
  - `src/app/web/pads.html` (1043 → 453 lines): extracted 6 HTML fragments (`_widget_bar_chart.html`, `_widget_gauge.html`, `_widget_sparkline.html`, `_style_help.html`, `_health_widget.html`, `_reboot_overlay.html`) using the existing `{{PLACEHOLDER}}` template system; `{{REBOOT_OVERLAY}}` also applied to `network.html`
  - `src/app/display_manager.cpp` (822 → 419 lines): split into `display_task.cpp` (LVGL render + present tasks + flush callback), `display_screen_nav.cpp` (screen navigation), and `display_manager_api.cpp` (C-API wrappers); no interface changes
  - `src/app/screens/pad_screen.cpp` (1028 → 175 lines): split into `pad_tile_builder.cpp` (tile build/clear), `pad_screen_events.cpp` (event handlers), and `pad_screen_poll.cpp` (binding polling); compiled via the existing `screens.cpp` single-TU build; `pad_screen.h` unchanged
  - `src/app/web/portal.js` (4357 → 3-line shim): split into 7 focused modules (`portal_core.js`, `portal_config.js`, `portal_firmware.js`, `portal_health.js`, `portal_pad_colors.js`, `portal_pad_io.js`, `portal_pad_editor.js`) served as individual routes and loaded in dependency order; 7 new route handlers added to `web_portal_pages.cpp/h` and `web_portal_routes.cpp`

---

## [1.8.0] - 2026-03-13

### Fixed
- **Binding reference modal corner clipping** — the new docs-style binding popup now enforces consistent rounded corners on all sides by clipping inner header/body layers to the modal radius (`overflow: hidden`), fixing the one-corner-only rounding artifact
- **Dual-binding gauge overlay visibility** — in dual-binding mode, the negative overlay arc could mask the primary ring because it rendered its own main track layer on top. Dual overlay arcs now render indicator-only, so combined rings remain visible and correctly show positive/negative contributions
- **Widget color bindings not updating** — all binding-driven color fields in gauge, bar chart, and sparkline widgets (track color, needle color, tick color, bar background, arc colors, line colors, reference line colors, min/max marker colors) were only resolved once at widget creation and never re-resolved during display updates. Colors driven by binding expressions (e.g. `[expr:threshold([mqtt:topic;value], "#4CAF50", 50, "#FF9800", 80, "#F44336")]`) now update live every display cycle via the widget `tick()` callback. Also fixed the pad screen's `update()` gating — widget color re-resolution no longer depends on a data value change
- **Pad editor newline escape for labels** — label inputs now support explicit `\n` escapes: typing `Line1\nLine2` in the editor is converted to a real newline when saved, and converted back to `\n` when reopened for editing. Applied to top/center/bottom button labels and gauge start labels (`start_label`, `start_label_2`, `start_label_3`), so multi-line text is preserved round-trip instead of being stored as literal backslash+n characters

- **Gauge center label clipping** — the center label inside a gauge arc was clipped to the innermost ring width, causing bound labels (driven by binding templates) to vanish entirely when text was set dynamically. The label now uses a wide draw area (`content_w × 3`) so CLIP-mode rendering is stable after binding updates. `LV_OBJ_FLAG_OVERFLOW_VISIBLE` is set on both the tile and the label so text can render outside the tile bounds

### Added
- **Shared binding reference template** — extracted duplicated inline binding-help markup from `home.html` and `pads.html` into reusable `src/app/web/_binding_help.html`, injected at build time with `{{BINDING_HELP}}`
- **Binding reference docs-style UI** — redesigned the binding popup with sectioned navigation, sticky-ish top toolbar placement between header and content, neutral docs palette, and responsive card-based examples
- **Binding example copy buttons** — each binding example now has a one-click copy action with clipboard fallback and temporary copied-state feedback
- **Binding format-string reference** — added a dedicated Format section with clear `%d` / `%.1f` / `%.2f` / `%05d` / `%s` / `%%` guidance and practical examples for `mqtt`, `health`, `expr`, and `pad` bindings
- **Gauge dual-binding ring pairs** — gauges now support 4 binding slots and two optional pairing modes: Ring 1+2 and Ring 3+4 can be collapsed into shared rings where the first ring contributes positive direction and the second ring contributes negative direction. Includes per-slot colors (`arc_color`..`arc_color_4`) and start labels (`start_label`..`start_label_4`) plus updated runtime binding fan-out (`MAX_WIDGET_BINDINGS = 4`)
- **UI Offset per button** — new **UI Offset** field in the button editor Appearance section nudges all visual content (labels, icons, widget areas) inside a button by a fixed `x;y` pixel amount (e.g. `20;-10`). Positive x moves right, negative x moves left; positive y moves down, negative y moves up. Useful for fine-tuning label or widget placement inside the tile without affecting the tile geometry. Applies to all widget types (gauge, bar chart, sparkline) and plain label/icon buttons
- **Label style `x:` offset** — the per-label style DSL now supports `x:<int>` to shift a single label horizontally by up to ±999 px, independent of the button-level UI Offset. Example: `x:10;y:-4` shifts the center label 10 px right and 4 px up relative to its anchor
- **Gauge — zero centered mode** — new "Zero centered" option for gauges with negative-to-positive ranges (e.g., grid power -3 kW to +3 kW). The arc fills from the zero point instead of the start edge: negative values grow leftward and positive values grow rightward from zero. The zero point is derived from where 0 falls in the min/max range. Ideal for bidirectional metrics like electricity import/export, temperature deviation, or balance indicators
- **Gauge start labels (per ring)** — gauges now support optional start labels for outer, middle, and inner rings (`start_label`, `start_label_2`, `start_label_3`). Each label accepts plain text or binding templates, uses the corresponding ring color, and updates live with the widget
- **Pad bindings** — define named data sources once at the page level (`"bindings": {"power": "[mqtt:solar/power;$.value]"}`) and reference them across all buttons and widgets on that page via `[pad:name]` or `[pad:name;format]`. Avoids repeating the same MQTT topic in every field and makes it easy to switch data sources — change one binding instead of editing every button. Up to 16 named bindings per page. Works inside expressions, widget data bindings, color bindings, and sparkline sources. Includes full web portal UI for adding, editing, and deleting bindings
- **Expression `threshold()` function** — new built-in function `threshold(value, color0, t1, color1, ..., tN, colorN)` for mapping a numeric value to color strings based on ascending thresholds. Variable arity (1–N thresholds), returns a `"#RRGGBB"` string. Replaces verbose nested ternaries for multi-zone color bindings. Composable with any binding type — e.g. `[expr:threshold([mqtt:sensor;temp], "#4CAF50", 25, "#FF9800", 35, "#FF0000")]`

### Improved
- **Pad editor gauge terminology** — gauge editor labels now use consistent ring naming (`Ring 1..4 data binding`, `Ring 1..4 label`) and dual-binding toggle labels now describe ring pairs directly (`Dual Binding Ring 1 and 2`, `Dual Binding Ring 3 and 4`) with explicit guidance about negative minimum values
- **Pad grid preview newline rendering** — preview label elements now use newline-preserving white-space (`pre-line`), so stored multi-line label content renders as line breaks in the web editor preview instead of a single collapsed line
- **Gauge tick marks on all rings** — concentric gauge widgets now repeat their configured tick marks on the middle and inner rings instead of drawing them only on the outer ring, keeping multi-ring gauges visually consistent across all active arcs
- **Gauge multi-ring slot behavior** — inner-ring bindings now remain in slot 3 even when slot 2 is empty (no auto-promotion), so concentric gauges preserve intentional ring gaps from editor configuration through runtime updates
- **Gauge start-label placement** — start-label angle and placement math was refined for consistent readable orientation and tighter ring proximity across all quadrants
- **Widget registration macro** — new `REGISTER_WIDGET(name, stream_fn)` macro in `widget.h` replaces 10-line boilerplate registration blocks in each widget file with a one-liner; applied to bar chart, gauge, and sparkline widgets
- **Widget value clamping utility** — new `clamp_val<T>(v, lo, hi)` template in `widget.h` replaces ad-hoc ternary chains for two-sided clamping in widget config parsing (4 call sites in bar chart and gauge)
- **Widget utility tests** — new `test_widget_common.cpp` with 27 host-native tests covering `clamp_val` across int, uint8_t, float, and uint16_t types including widget-specific ranges; total test count: 191
- **Widget tick color caching** — gauge and bar chart `tick()` functions now cache resolved color values and skip LVGL style setters when the color hasn't changed. Eliminates redundant `lv_obj_set_style_*` calls every frame for static or stable colors. Gauge tick also caches tick mark line pointers at creation time, replacing the per-frame O(N) child scan with a direct array iteration
- **Sparkline snapshot change detection** — sparkline `tick()` now compares each data stream's ring buffer head against a cached value and skips the full redraw when no data has advanced. Chart area dimensions are also cached from creation time, removing a `lv_obj_update_layout()` call from each redraw. Combined with widget tick color caching, measured impact on ESP32-P4 test pad screen: CPU 50% → 25%, frame rate <30 → 56 FPS

### Changed
- **Web asset template pipeline** — `tools/minify-web-assets.sh` now supports `{{BINDING_HELP}}` replacement in addition to `{{HEADER}}`, `{{NAV}}`, and `{{FOOTER}}`
- **Gauge zero-centered wording** — the gauge editor and user docs now label this option as `Zero-Centered` and clarify applicability to single-mode rings, while dual-binding rings continue to use zero-centered semantics internally
- **Color picker — unified input** — the color picker popover now uses a single unified input field that accepts both static hex colors and binding expressions. The previous separate hex/binding dual-input design has been replaced with a wider 380px popover with a close button. Color fields that support bindings show an **fx** badge hint above the color swatch
- **Threshold colors → bindable color fields** — replaced the per-widget 4-zone threshold color system (use absolute, reversed/higher-is-better, color tiers, threshold sliders) with single bindable color fields per widget: `bar_color` for bar chart, `arc_color`/`arc_color_2`/`arc_color_3` for gauge rings. Sparkline already had individual `line_color` fields. All color fields accept `[expr:threshold(...)]` expressions for the same (and more flexible) multi-zone coloring. **Breaking:** existing configs using `threshold_1`–`threshold_3`, `color_good`/`color_ok`/`color_warn`/`color_bad`, `use_absolute`, `higher_is_better`, and `use_thresholds` will have those fields silently ignored — reconfigure colors using binding expressions or the new threshold generator

### Documentation
- **Template placeholder docs updated** — `docs/dev/web-portal.md` and `.github/copilot-instructions.md` now document `_binding_help.html` and `{{BINDING_HELP}}` alongside existing shared templates
- **Power-balance gauge walkthrough** — added a new user-guide example showing dual-binding visualization for house load, solar production, and grid import/export balance (Ring 1 + Ring 2 combined, Ring 3 as resulting grid power; Min/Max `-3/3`, `Zero-Centered` on, needle off) in `docs/pad-editor-guide.md`; linked from `docs/web-portal-guide.md`
- **Color picker — threshold expression generator** — the color picker popover now includes a collapsible "Generate Color by Threshold" helper. Pick 4 zone colors, set breakpoints, and the expression auto-generates as you type into a ready-to-use `[expr:threshold(...)]` binding. Empty thresholds auto-fill with even spacing over 0–100

## [1.7.0] - 2026-03-11

### Fixed
- **Health bubble on Pads page** — the floating health widget was not working on the Pads page because the expanded health overlay HTML (from `{{FOOTER}}`) was missing. Added the health widget expanded overlay directly to `pads.html`
- **Sparkline dot alignment** — current-value dots, min/max markers now use the exact pixel coordinates from the rendered line points, eliminating sub-pixel gaps between dots and the sparkline line

### Added
- **Sparkline smoothing** — new "Smoothing" setting (0–8) applies Gaussian kernel smoothing to sparkline data, producing visually smoother trend lines. Higher values = more smoothing (radius 8 averages 17 neighbors per sample). Min/max markers and current-value dots are repositioned to sit on the smoothed line. Set to 0 (default) for raw data rendering
- **Sparkline widget** — mini trend line that plots the last N data points over a configurable time window. Supports auto or manual min/max, configurable line width and color, optional color thresholds (4-zone, like bar chart), and any binding type (MQTT, health, time, expressions). Supports up to 3 overlaid lines (each with its own data binding and color) sharing the same Y-axis — ideal for comparing related metrics (e.g., solar production vs grid import). Unified auto-scale (default on) ensures all lines share the same Y-axis range for visual comparability; disable for independent per-line scaling. Backed by the new data stream registry for background data collection — sparklines have historical data ready even when navigating to a screen for the first time
- **Sparkline min/max markers** — configurable dot markers at the minimum and maximum data points in the visible range. Each marker has its own size (0 = off, 1–20 px), printf format string for numeric labels (e.g., `hi %.1f`), and optional color override (defaults to line color)
- **Sparkline current-value dot** — optional dot at the right edge of the chart showing the most recent value. Size configurable (0 = off, 1–20 px). Follows threshold color when "Color by value" is enabled
- **Sparkline reference lines** — up to 3 horizontal reference lines at fixed Y values, drawn behind the data. Each line has a configurable color and pattern (Solid, Dotted, or Dashed). Optional "Keep in view" toggle expands auto-scale to guarantee reference lines are always visible (explicit min/max take priority). Useful for target values, alert thresholds, or baseline indicators
- **Data stream registry** — demand-driven background data collection subsystem. History-based widgets (sparkline) register their data needs at config load time; the registry continuously resolves bindings and feeds ring buffers regardless of which screen is active. Per-widget ring buffers with PSRAM allocation and LOCF (Last Observation Carried Forward) for data gaps
- **Bar chart — horizontal orientation** — new "Orientation" dropdown in bar chart settings (Vertical / Horizontal). Horizontal mode fills the bar left-to-right instead of bottom-to-top; Bar Width % controls bar thickness (height) in horizontal mode. Useful for progress-bar style visualizations on wide buttons
- **Gauge widget** — arc-based gauge visualization for buttons, with configurable arc sweep (10–360°), start angle, ticks, needle, and up to 4 color thresholds. Supports data binding for live values (e.g. `[time:%S]`, `[mqtt:sensor/temperature]`). Configurable track, needle, and tick colors, needle width (0 = hidden), tick width, and arc thickness. Center label and icon are positioned inside the arc. Tick marks are placed at interior positions only (N ticks divide the arc into N+1 equal segments). Supports up to 3 concentric rings (outer/middle/inner) — each with its own data binding but sharing the same scale, thresholds, and colors — for Apple Health ring–style visualizations
- **Bar chart — reversed color mode** — new "Reversed, high values are better" checkbox in the bar chart widget settings. When toggled, the four color picker values swap in place (the positional labels Below T1 / T1–T2 / T2–T3 / Above T3 stay fixed). Ideal for battery level, signal strength, or any metric where higher is better

### Changed
- **Web portal — separate Pads page** — the pad editor has moved from the Home page to its own dedicated **Pads** page, accessible via a new tab in the navigation bar (Home | **Pads** | Network | Firmware). The Home page now shows a welcome card with quick links, plus device settings (Operating Mode, Display, Sensors). The Pads page has its own floating footer with Save Pad / Show on Device / More actions, completely separate from the device config Save & Reboot flow
- **Button editor — collapsible card groups** — the button editor dialog is reorganized into card-like collapsible `<details>` sections: Layout, Labels, Bar Chart (conditional), Actions, Icon, Image Background, Appearance, and State. Layout, Labels, and Actions are open by default; the rest are collapsed to reduce visual clutter
- **Unsaved pad changes protection** — switching between pads or navigating away from the Pads page with unsaved changes now shows a confirmation dialog. The browser's `beforeunload` event also warns before closing the tab
- **Home page section order** — reordered sections to Welcome → Display Settings → Operating Mode & Cadence. The welcome card now includes a link to the GitHub repo for docs, source code, and issue tracking
- **Removed `dummy_setting`** — cleaned up the unused example config field from the backend, REST API, JavaScript, and documentation

## [1.6.0] - 2026-03-08

### Added
- **Per-label style DSL** — each button label (top/center/bottom) now supports a compact style string for advanced typography control without cluttering the main UI. Toggle the **Aa** button next to any label field to reveal the style input. Properties are semicolon-separated key:value pairs:
  - `font:24` — override font size (available sizes: 12, 14, 18, 24, 32, 36 px)
  - `align:left` / `align:right` / `align:center` — horizontal text alignment
  - `y:-2` — vertical pixel offset (nudge label up or down)
  - `mode:clip` / `mode:scroll` / `mode:dot` / `mode:wrap` — LVGL long-text mode (clip truncates, dot adds "...", scroll auto-scrolls, wrap wraps to next line)
  - `color:#FF0` — override text color for this label (3, 4, 6, or 8 hex digits)
  - Example: `font:36;align:left;mode:dot` — large left-aligned text with ellipsis overflow
  - A built-in **?** help button documents all properties with examples
- **Sub-second time binding codes** — the `[time:]` scheme now supports custom format codes beyond strftime: `%ms` (000-999 milliseconds within second), `%cs` (00-99 centiseconds), `%ds` (0-9 deciseconds), and `%ums` (raw device uptime in ms, standalone, no NTP needed). Enables sub-second color cycling in expressions (e.g. `[expr:[time:%ums]%1000<500?"#ff0000":"#00ff00"]` toggles every 500ms)

- **Expression binding `[expr:]`** — new binding scheme for inline math, comparisons, and conditional text on button labels. Supports arithmetic (`+ - * / %`), comparisons (`> < >= <= == !=`), ternary (`cond ? "yes" : "no"`), parentheses, and cross-binding math (e.g. `[expr:[mqtt:solar;w] - [mqtt:grid;w];%.0f]`). Inner bindings are resolved first, then the expression is evaluated. Optional printf format suffix via `;` separator (e.g. `[expr:[health:heap_free]/1024;%.1f]`)
- **Host-native unit test infrastructure** — new `tests/` directory with `g++`-compiled tests that run on the development machine (no ESP32 needed). Includes `test_expr_eval.cpp` (66 pure evaluator tests) and `test_expr_binding.cpp` (22 integration tests with mock MQTT/health resolvers exercising the full template→resolve→evaluate pipeline). Run via `tests/run_tests.sh`
- **Binding-based coloring** — button background, text, and border colors now accept binding expressions (e.g. `[expr:[mqtt:topic;path]>50?"#ff0000":"#00ff00"]`) for dynamic color changes driven by MQTT data. Each color field has a default fallback color used until the binding resolves. Replaces the previous toggle-state feature with a more flexible approach
- **MQTT Wake binding** — new `screen_saver_wake_binding` config field that wakes the screensaver and keeps the screen on while a binding expression resolves to `ON`. Supports any binding scheme (`[mqtt:...]`, `[expr:...]`, etc.). When the value leaves ON, the normal idle timeout resumes. Configurable in the web portal under Screen Saver settings. Implemented in `mqtt_wake.cpp/h` (compile-time gated by `HAS_MQTT && HAS_DISPLAY`)
- **Dynamic button state** — new `btn_state` config field provides tri-state control per button: `enabled` (normal, default), `disabled` (visible but ignores tap/hold), `hidden` (invisible, gap preserved). Supports binding expressions for dynamic state (e.g. `[expr:[mqtt:alarm/state;.;%s]=="armed"?"disabled":"enabled"]`). LVGL `LV_STATE_DISABLED` provides native dimmed styling; `LV_OBJ_FLAG_HIDDEN` hides without reflow. Image fetch slots auto-pause when hidden
- **Pad Editor Guide** — new comprehensive documentation (`docs/pad-editor-guide.md`) covering all pad editor features: pad and button settings, label style DSL, icons, widgets, binding template syntax (MQTT, Health, Time, Expression), dynamic colors, bulk pad actions, and six complete real-world examples (energy dashboard, light controls, security cameras, world clock, server monitor, climate control). Web portal guide now links to this dedicated guide instead of inlining the full reference

### Improved
- **Tap indicator overlay** — replaced the old brighten-background tap flash with a semi-transparent overlay that covers the entire button including images, widgets, and bar charts. Uses adaptive color: black overlay (~31% opacity) on light backgrounds, white overlay (~20% opacity) on dark backgrounds, based on perceived luminance (BT.601). Overlay tracks dynamic background color changes from bindings. Eliminates per-tap heap allocation (no more `malloc`/`free` per press)
- **Pad Editor — dotted outline on configured buttons** — configured buttons in the web portal Pad Editor now keep the dashed outline indicator and hover effect; the outline follows the button's configured corner radius via CSS `outline` so the editor grid consistently shows cell boundaries for all buttons
- **Image fetch PSRAM optimization** — `image_fetch_pause_slot()` now frees the double-buffer PSRAM allocations (front_buf + back_buf) when a page is hidden, reclaiming 2 × W×H×2 bytes per slot; LVGL-side `owned_pixels` are preserved so the last frame remains visible on navigate-back; buffers are re-allocated automatically on resume; persistent HTTP connections for paused slots are also closed, freeing socket and TLS memory
- **Telemetry cleanup — reduced PSRAM bus contention** — comprehensive audit and simplification of the device telemetry subsystem targeting ESP32-P4 MIPI-DSI boards where PSRAM free-list walks stall the DPI DMA scan
  - **CPU monitor rewrite** — replaced `uxTaskGetSystemState()` (which suspended the scheduler and caused blue-flash buffer underruns, issue #7) with per-core `ulTaskGetIdleRunTimeCounterForCore()` via `esp_timer` high-resolution counters; zero scheduler suspension
  - **Health window simplified** — 200 ms timer now uses only counter reads (`heap_caps_get_free_size` for internal + PSRAM); `heap_caps_get_largest_free_block` removed from the timer and computed on-demand at `/api/health` request time; internal largest sparkline retains its line but loses min/max band
  - **Health window enabled on ESP32-P4** — re-enabled `DEVICE_TELEMETRY_HEALTH_WINDOW` on `esp32-p4-lcd4b` now that all free-list walks are eliminated from the timer
  - **PSRAM sparkline bands** — added `psram_free` min/max window tracking; PSRAM Free sparkline now shows a min/max band like Internal Free
  - **Temperature sensor** — switched from transient install/read/uninstall per sample to a persistent handle installed once at boot
  - **Removed fields** — dropped `heap_free`, `heap_min`, `heap_largest`, `heap_size`, `cpu_freq`, `psram_fragmentation`, `psram_largest` (redundant with capability-specific fields); removed `display_lv_timer_us` and `display_present_us` debug fields; removed `heap_caps_check_integrity_all()` from heartbeat
  - **Removed tripwire system** — eliminated `TRIPWIRE_ARMED` / `rtos_task_utils.h` / background task dumper entirely
  - **HA discovery** — removed `heap_fragmentation` sensor (was an undocumented internal metric); HA users with dashboards referencing this entity will need to remove it

### Breaking
- **HA MQTT entities removed** — `sensor.macropad_heap_fragmentation` no longer published; dashboards referencing it will show "unavailable"
- **`/api/health` fields removed** — `heap_free`, `heap_min`, `heap_largest`, `heap_size`, `cpu_freq`, `psram_fragmentation`, `psram_largest`, `display_lv_timer_us`, `display_present_us`, `heap_internal_largest_min_window`, `heap_internal_largest_max_window` dropped from the JSON response

## [1.5.0] - 2026-03-05

### Added
- **Wake Screen redirect** — per-pad `wake_screen` setting navigates to a target screen when the screensaver enters sleep (invisible under the sleep overlay); configurable in the web portal Pad Editor "Wake Screen" dropdown; default is to stay on the current screen
- **MQTT Active Screen control** — exposes active screen as an HA `select` entity (`~/screen/state` + `~/screen/set`); HA can navigate the device to any screen and wake the screensaver; navigating to the current screen while asleep wakes the device; inactivity timer resets on HA navigation (mimics a tap)
- **Pad background color** — per-pad `bg_color` setting with color picker and recently-used swatches in the web portal; default is pure black; applied as LVGL screen background on the device
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
- **ESP32-P4 health telemetry re-enabled** — background telemetry tasks (CPU monitor, health-window timer, sparklines) re-enabled on both P4 boards (`esp32-p4-lcd4b`, `jc4880p433`); originally disabled during flicker investigation, no longer needed

### Improved
- **Pad Editor aspect ratio** — grid preview now mirrors the device screen's aspect ratio using display dimensions from `/api/info`; portrait devices render as tall rectangles, square devices as squares; capped at 70vh with auto-centering to keep the editor compact
- **Pad Editor button rendering** — improved grid preview fidelity: background image buttons show a 🖼️ placeholder emoji (visible alongside labels/icons); top/bottom labels are pushed to cell edges matching device layout; binding tokens simplified to `[scheme:key]` in preview; spacer divs keep center content vertically centered when only one edge label is present
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
