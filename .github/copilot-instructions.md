# Copilot Instructions for ESP32 Macropad Project

## Project Overview

ESP32 Macropad — a feature-rich, configurable macropad firmware for ESP32 devices with touch screens. Built with `arduino-cli` for headless builds. Designed for WSL2/Linux environments with local toolchain installation (no system dependencies). The primary boards target display + touch hardware; a small set of headless reference boards (e.g. `esp32c3-withsensors`) also build against the same firmware for BTHome / MQTT sensor-node use cases.

## Architecture

- **Build System**: Custom bash scripts wrapping `arduino-cli` (installed locally to `./bin/`)
- **Sketch Location**: Main Arduino file at `src/app/app.ino`
- **Board Configuration**: `src/app/board_config.h` (defaults) + `src/boards/[board-name]/board_overrides.h` (per-board overrides). Uses `#if HAS_xxx` conditional compilation.
- **Device Class Branding** (`device_class_registry.h` + `class_branding.h`): Compile-time device class resolved by `device_class_detect()`'s `#if` ladder (in precedence order): `IS_SHUTTER_TESTER` → `IS_COFFEE_SCALE` → `IS_DARKROOM_TIMER` → `IS_VOICE_ASSISTANT` → `HAS_EPAPER` → `!HAS_DISPLAY` (headless) → else `macropad`. The specialized `IS_*` product variants take precedence over the display-capability fallbacks. Single source of truth: `device_class_detect()` (one `#if` ladder) plus the `DESCRIPTORS[]` table in `src/app/device_class_registry.cpp` hold every branding string for every class; `class_branding.{h,cpp}` are thin `const char*` wrappers. Drives web portal title, default device name, AP SSID, HTTP auth realm, HA `mdl`, and the ESP Web Tools flash page card label. Bash mirror in `config.sh` (`device_class_for_board`, `device_class_brand_prefix`); drift is caught by `tests/test_branding_mirror.sh`. Per-board flash-page metadata in `src/boards/<board>/metadata.json`. See `docs/dev/build-and-release-process.md` → Device Class Branding.
- **Display & Touch**: HAL-based architecture with LVGL. See `docs/dev/display-touch-architecture.md` and `.github/instructions/display-touch.instructions.md`.
- **Image Fetch** (`HAS_IMAGE_FETCH`): Slot-based FreeRTOS background HTTP(S) image fetcher with JPEG/PNG decode, bilinear scaling, and MJPEG streaming.
- **Storage Facade** (`storage.h`): Compile-time `Storage` macro resolves to `LittleFS` (default) or `SD_MMC` when `USE_SD_STORAGE` is enabled. All persistent file I/O (pad configs, icons, sounds, boot/swipe/timer/button-default configs, indexed stores) goes through this facade. SD card path mounts SDMMC Slot 0 in `sd_storage_mount()` and halts boot with a splash if the card is missing. Throttled usage publish via `storage_publish_usage()`.
- **Icon Store** (`HAS_DISPLAY`): PNG icon storage on the `Storage` facade (LittleFS or SD) with PSRAM-cached ARGB8888 draw buffers.
- **Custom Fonts** (`HAS_CUSTOM_FONTS`): 3 font families (DSEG7, Bebas, Doto) × 7 sizes. LabelStyle DSL: `font_family:dseg7`, `font_family:bebas`, `font_family:doto`.
- **Widget Subsystem** (`HAS_DISPLAY`): Extensible widget types — gauge, sparkline, bar chart, table, rocker, numeric rocker, list. Each widget renders inside a button.
- **Data Stream Registry** (`HAS_DISPLAY && HAS_MQTT`): Demand-driven per-widget ring buffers in PSRAM for history-based widgets. Slots are bucketed on wall-clock time once NTP is valid (ring reset once on the transition), so history from external sources lines up with live samples.
- **HA History Backfill** (`HAS_HA_HISTORY` = `HAS_DISPLAY && HAS_MQTT && HAS_PSRAM`): Fills the unsampled part of a sparkline stream from Home Assistant Recorder long-term statistics. `ha_stats.{h,cpp}` — background PSRAM-stacked FreeRTOS task, one request in flight, POSTs `recorder/get_statistics`; `ha_stats_resample.{h,cpp}` — pure, host-testable ISO 8601 parsing / bucket resampling / ring merge (`tests/test_ha_stats_resample.cpp`). Live samples always win over backfilled values.
- **Binding Template Engine** (`HAS_MQTT`): Scheme-extensible `[scheme:params]` token resolver. Schemes: `mqtt`, `health`, `time`, `expr`, `pad`, `timer`, `list`, `net`. Pipe fallback: `[scheme:params|fallback]`. Called only from LVGL task. Each scheme registers a `BindingSchemeSpec` containing its parameter and finite-key metadata; the core validator, `GET /api/bindings`, and MCP `get_capabilities` consume that single registry. `binding_schema.{h,cpp}` owns the shared JSON serialization.
- **Pad Validate + Resolve core** (`HAS_DISPLAY`): Single source of truth shared by BOTH front-ends (MCP tools + web portal). `pad_validate.{h,cpp}` — `pad_validate(pad, tolerate_offgrid=false)` validates a full pad JSON (grid/spans, widget types + config caps, colors, action arrays, binding tokens, one-level `[pad:]` rule); the portal save path passes `tolerate_offgrid=true` to keep hidden off-grid buttons, MCP stays strict. `pad_resolve_request.{h,cpp}` — `pad_resolve_request(args, result)` resolves `[scheme:params]` tokens against live data for `resolve_bindings` (MCP) and `POST /api/pad/resolve` (portal); resolution runs on the main loop via the bridge (below) using `pad_resolve()` in `pad_binding.cpp`. Extracting these killed the former three-way drift (MCP `validate_pad_doc`, portal `validate_pad_json`, JS `portal_binding_validator.js`).
- **Deferred dispatch slots** (`deferred_dispatch_slot.h`): Shared fixed-buffer, single-slot completion primitive for cross-task synchronous work. It uses a `StaticSemaphore_t` binary semaphore and internal-DRAM state, rejects invalid, unavailable, ISR/consumer-task, and oversized requests explicitly, and retains a timed-out slot until its sole consumer finishes. At the completion/timeout boundary it consumes the matching completion give before reuse. An abandoned cleanup callback runs exactly once on the consumer task, outside the slot lock; normal success, busy, invalid, unavailable, and oversized paths never run it. `main_loop_bridge.{h,cpp}` instantiates the 256-byte main-loop slot, is independent of `HAS_MCP`, and is drained by Arduino `loopTask` through `web_portal_handle()`. `DisplayManager` instantiates the 64-byte display slot, drained only by the LVGL task under its existing mutex. Pointer-bearing contexts are shallow copies, so callers must transfer payload ownership with abandoned cleanup or have execution own and release it.
- **MCP Core Server** (`web_mcp.{cpp,h}`): Built-in Model Context Protocol server — single `POST /mcp` JSON-RPC 2.0 endpoint (protocol `2025-06-18`, Streamable HTTP, JSON-only, stateless). Gated by the `HAS_MCP` compile-time flag (default true; independent of `HAS_DISPLAY`). Self-registers via `REGISTER_ROUTES` using a custom `AsyncWebHandler` subclass (the stock callback handler rejects `Accept: text/event-stream` requests via `isHTTP()`, which all MCP clients send). Off by default, STA-mode only, dedicated bearer token (hardware-RNG, constant-time compare, fail-closed). Extensible tool registry (`mcp_tool_registry.{h,cpp}` + `REGISTER_MCP_TOOL`); core universal read/control tools in `mcp_tools_core.cpp` (device/screen/pad-press/system) and `mcp_tools_config.cpp` (device-settings `get_config`/`set_config`, `notify`, `set_volume`, `timer_control`, `get_component_config`/`set_component_config`); pad authoring + capability manifest in `mcp_tools_pads.cpp`; device-class tools aggregate through `mcp_components.cpp`. Control tools (gated by `mcp_control_enabled`) NEVER call `action_dispatch`/LVGL/blocking I/O on the web task — they defer to the main loop via `mcp_control_dispatch()` (spinlock slot + binary semaphore, bounded wait), drained in `web_mcp_loop()`. Reboot is deferred with a grace period so the response flushes first. See `docs/mcp-guide.md`.
- **Timer Subsystem** (`HAS_DISPLAY`): 3 independent timers. Start/Toggle actions explicitly carry mode and countdown duration; `/config/timers.json` stores only per-slot expiry-action lists that are snapshotted at countdown start. Shared strict prepare/execute logic lives in `timer_command.{h,cpp}`; engine state is mutex-protected. Includes `[timer:N]` bindings and the Timer component REST API.
- **Screen Saver** (`HAS_DISPLAY`): Inactivity-based display sleep with fade animation, per-screen wake redirect, panel hardware sleep (`displaySleep()`/`displayWake()` on `DisplayDriver`), and LVGL task throttle (`SCREENSAVER_SLEEP_TICK_MS`).
- **MQTT Screen Control** (`HAS_MQTT && HAS_DISPLAY`): HA `select` entity for remote screen navigation.
- **MQTT Wake** (`HAS_MQTT && HAS_DISPLAY`): Binding-driven screensaver wakeup with idle-timer keep-alive.
- **Notification Bubble** (`HAS_DISPLAY`): Message overlay with fade animation, tap-to-dismiss, HA remote trigger, `ACTION_TYPE_NOTIFY`.
- **Visual Alert** (`HAS_DISPLAY`): Full-screen pulsing color overlay (`breathe`/`blink`/`solid`) on `lv_layer_top()`, wake-first, tap-to-dismiss, `ACTION_TYPE_VISUAL_ALERT`. Bindable color; deferred `portMUX` show/stop drained by `visual_alert_loop()` (`visual_alert.{h,cpp}`). Symmetric with `beep` — fires from every trigger source; MCP control tool `visual_alert`.
- **Swipe Actions** (`HAS_DISPLAY`): 4-direction configurable swipe gestures with full ButtonAction parity.
- **Boot Actions** (`HAS_DISPLAY`): Device-level actions dispatched once at boot after first screen.
- **Button Defaults** (`HAS_DISPLAY`): Device-wide default button appearance (colors, border, radius, label styles).
- **Action System** (`HAS_DISPLAY || HAS_BUTTON`): `ActionTypeDef` is the single runtime contract for parsing, serialization, dispatch, availability, authoring validation, bindings, and portal/MCP catalog metadata. `action_parse.cpp` and `action_dispatch.cpp` only route through that registry; action-specific code belongs in one self-registering module. Built-ins live in `src/app/actions/<type>_action.cpp` and are compiled through the one manifest `src/app/actions/action_modules.inc`, included by sketch-root `actions.cpp` because arduino-cli compiles only sketch-root `.cpp` files. Each built-in module must call `DEFINE_AND_REGISTER_ACTION_TYPE(...)` exactly once and provide `describe()` metadata; add the module to the manifest when adding a built-in. Device-class action modules stay under their owning `device_classes/<class>/` folder and use the same self-registration macro, but are included by their existing device-class aggregators. `pad_config.h` remains the shared persisted type and tagged-union layout. `action_list_parse()` / `action_list_dispatch()` serve array-of-actions consumers (boot, timer, future). `tests/test_action_catalog_completeness.sh` enforces manifest and catalog coverage.
- **BLE HID** (`HAS_BLE_HID`): NimBLE keyboard with key sequence DSL, single-owner pairing, auto-re-pair. Runtime-toggled.
- **Audio** (`HAS_AUDIO`): Board-selected ES8311 or PCM510xA I2S output, beep pattern DSL, volume control, and async FreeRTOS playback. See `docs/dev/audio-architecture.md` and `.github/instructions/audio.instructions.md`.
- **Sound Player** (`HAS_SOUND_PLAYER`): MP3 decode (minimp3) + resample + I2S playback from LittleFS.
- **MQTT Audio** (`HAS_AUDIO && HAS_MQTT`): HA siren, volume, beep buttons, custom tone entities.
- **Power + Transport**: Power modes, BLE/MQTT transport selection, duty-cycle runtime, WiFi manager, portal idle timeout.
- **Web Portal**: Multi-page async web server with captive portal. Board variants can define `PORTAL_PRIMARY_*` flags in `board_overrides.h` to promote a custom nav category to first position with startup routing and a welcome hero card. See `.github/instructions/web-portal.instructions.md` and `docs/dev/web-portal.md`.
  - **Portal components aggregation**: Files in `src/app/components/*.cpp` and `src/app/device_classes/*/components/*.cpp` are NOT compiled directly by arduino-cli (it only compiles `.cpp` files in the sketch root). They are `#include`-aggregated into `src/app/portal_components.cpp`.
    - Shared portal components: include as `#include "components/<name>_component.cpp"`
    - Device-class portal components: include as `#include "device_classes/<class>/components/<name>_component.cpp"`
    - **When adding a new component, you MUST add the matching include to `portal_components.cpp` under the correct feature-flag block** — otherwise the component's `REGISTER_COMPONENT()` static initializer never runs and the component is silently absent from the nav and registry.
    - Same aggregation rule applies to widgets (`widgets.cpp`), screens (`screens.cpp`), drivers (`display_drivers.cpp` / `touch_drivers.cpp`), custom fonts (`custom_fonts.cpp`), and MCP tools (`mcp_components.cpp`).
- **Pad Config**: `pad_config.cpp/h` — JSON parser for pad/button/widget configuration, `LabelStyle` DSL, `ButtonAction` types, `ButtonDefaults` cascade, `template_pad` inheritance.
- **Pad Building Blocks** (`HAS_DISPLAY`): Registration-based catalog of pre-configured button groups. `pad_block.h/cpp` — `pad_block_register()` API for feature branches to add blocks independently. REST endpoint `GET /api/pad/blocks`.
- **ListProvider Registry** (`HAS_DISPLAY`): Pluggable data source registry for list widgets. `list_provider.h/cpp` — `list_provider_register()` / `list_provider_find()`. Feature branches register providers from their own init functions. Built-in provider: `pads` (lists configured pads with custom names, item IDs `pad_N`).
- **Pad Layout**: `pad_layout.h` — Layout computation, UI scale tiers, label style resolver helpers.
- **Output**: Compiled binaries in `./build/<board-name>/`

### Board Targets

| Board | SoC | Display | Flash/PSRAM |
|---|---|---|---|
| esp32-4848S040 | ESP32-S3 | 480×480 ST7701 RGB + GT911 | 16MB + OPI PSRAM |
| jc3248w535 | ESP32-S3 | — | 16MB + OPI PSRAM |
| jc3636w518 | ESP32-S3 | — | 16MB + OPI PSRAM |
| esp32-p4-lcd4b | ESP32-P4 | 720×720 MIPI-DSI + GT911 | 32MB + 32MB PSRAM |
| jc4880p433 | ESP32-P4 | 480×800 MIPI-DSI ST7701 + GT911 | 16MB + 32MB PSRAM |
| jc1060p470c | ESP32-P4 | 1024×600 MIPI-DSI JD9165 + GT911 | 16MB + 32MB PSRAM |

The table lists the base hardware variants. Several **device-class variant boards** reuse a base board's hardware while selecting a specialized `IS_*` device class via `board_overrides.h` — e.g. `jc4880p433-shutter` (Shutter Tester), `jc4880p433-darkroom` (Darkroom Timer), `jc4880p433-nau7802` / `jc4880p433-hx711` (Coffee Scale). Run `./build.sh` with no argument or `list_boards()` in `config.sh` for the full target list.

## Critical Developer Workflows

### First-time Setup

```bash
./setup.sh  # Downloads arduino-cli, installs ESP32 core, configures environment
```

### Build-Upload-Monitor Cycle

```bash
./build.sh                     # Build all configured boards
./build.sh esp32-4848S040      # Build specific board
./upload.sh jc4880p433         # Flash firmware via serial
./monitor.sh                   # Serial monitor at 115200 baud
./bum.sh jc3248w535            # Build + Upload + Monitor
./um.sh esp32-p4-lcd4b         # Upload + Monitor
```

All scripts use absolute paths via `SCRIPT_DIR` resolution — they work from any directory.

## Project-Specific Conventions

### Script Design Pattern

- All scripts source `config.sh` for common configuration and helper functions
- `config.sh` provides: `SCRIPT_DIR`, `find_arduino_cli()`, `find_serial_port()`, project constants, board management (`FQBN_TARGETS`, `get_board_name()`, `list_boards()`, `get_fqbn_for_board()`)
- Multi-board scripts require board name parameter when multiple targets are configured
- `config.sh` can source an optional `config.project.sh` with project-specific overrides

### Arduino Code Standards

- Use `Serial.begin(115200)` for consistency with monitor.sh default
- Include startup diagnostics (chip model, revision, CPU freq, flash size) using `ESP.*` functions
- Implement heartbeat pattern with `millis()` for long-running loops (5s interval)

### OTA Activity Contract

`ota_activity.{h,cpp}` owns the single race-safe lifecycle for manual and
online firmware updates. OTA entry points call `ota_activity_try_begin()` before
they can reach `Update.begin()` and call `ota_activity_finish()` on every
recoverable failure. Successful updates retain ownership through reboot.

New background subsystems must decide whether their work competes with OTA flash
writes. For nonessential network I/O, decoding, rendering, polling, or
PSRAM-heavy computation, check `ota_activity_is_active()` before starting work
and at an owned safe point in long-running work. Return, defer, or close the
subsystem's own operation there; do not suspend another task or add a global
pause framework.

Keep WiFi, AsyncTCP, portal responses and OTA status polling, OTA transport,
reboot processing, and safety-critical device-class loops operational. Do not
reuse the image-fetch screen-saver suspension for OTA: it has independent
ownership. If a new subsystem needs a checkpoint, add focused coverage to
`tests/` and exercise it during OTA stress validation.

## Key Files

### Scripts

| Script | Purpose |
|---|---|
| `config.sh` | Common configuration and helper functions (sourced by all scripts) |
| `setup.sh` | Downloads arduino-cli, configures ESP32 platform, installs libraries |
| `build.sh` | Compiles to `./build/<board-name>/*.bin` (all boards or specific board) |
| `upload.sh` | Flashes firmware via serial |
| `upload-erase.sh` | Completely erases ESP32 flash memory |
| `monitor.sh` | Serial console (Ctrl+C to exit) |
| `clean.sh` | Removes all build artifacts |
| `library.sh` | Manages Arduino library dependencies |
| `bum.sh` | Build + Upload + Monitor |
| `um.sh` | Upload + Monitor |

### Tools

| Tool | Purpose |
|---|---|
| `tools/minify-web-assets.sh` | Minifies and embeds web assets into `web_assets.h` (template replacement, CSS/JS minification, gzip) |
| `tools/png2lvgl_assets.py` | Converts `assets/png/*.png` into LVGL `lv_img_dsc_t` symbols |
| `tools/generate-fonts.sh` | Downloads TTF fonts and generates LVGL C source files (requires `npx lv_font_conv`) |
| `tools/generate-board-driver-table.py` | Generates board→drivers table from board overrides |
| `tools/test-portal-api.sh` | Curl-based HTTP integration tests for web portal (12 test cases, requires live device) |

### Tests

- `tests/run_tests.sh` — Builds and runs all host-native tests (no ESP32 needed)
- `tests/test_expr_eval.cpp` — Expression evaluator tests
- `tests/test_expr_binding.cpp` — Expression binding integration tests
- `tests/test_key_sequence.cpp` — Key sequence DSL parser tests
- `tests/test_action_parse.cpp` — ButtonAction JSON round-trip tests
- `tests/test_binding_template.cpp` — Binding template engine tests
- `tests/test_pad_binding.cpp` — Pad binding scheme tests
- `tests/test_widget_common.cpp` — Widget common utility tests
- `tests/test_health_table_builder.cpp` — Health table builder tests
- `tests/test_wifi_reconnect.cpp` — WiFi reconnect backoff and tier escalation tests
- `tests/test_timer_format.cpp` — Timer format output tests
- `tests/test_list_provider.cpp` — ListProvider registry and [list:provider_id.selected] binding tests

### Documentation

- `docs/dev/logging-guidelines.md` — Logging rules and format (LOGx macros, severity, modules)
- `docs/dev/web-portal.md` — Web portal and REST API guide
- `docs/dev/display-touch-architecture.md` — Display/touch HAL and screen architecture
- `docs/dev/audio-architecture.md` — Audio output, I2S, memory, and MP3 playback architecture
- `docs/dev/adding-a-device-class.md` — Device class extension contract (registry, aggregators, board overrides, optional subsystems)
- `docs/pad-editor-guide.md` — Pad editor, binding templates, widgets, and real-world examples

### Configuration

- `config.sh` — Project paths, FQBN_TARGETS array, and helper functions
- `arduino-libraries.txt` — List of required Arduino libraries (auto-installed by setup.sh)
- `.github/workflows/build.yml` — CI/CD pipeline with matrix builds for all board variants

### Code Review Infrastructure

- `.github/prompts/sanitycheck.prompt.md` — Pre-commit expert panel code review prompt
- `.github/agents/code-review.agent.md` — Code Review agent with batch triage workflow
- `.github/agents/subagents/code-reviewer.agent.md` — Expert reviewer subagent (non-user-invocable)
- `.github/instructions/review-experts/*.instructions.md` — Expert reviewer instruction files

## Library Management

- **Configuration File**: `arduino-libraries.txt` lists all required Arduino libraries
- **Management Script**: `./library.sh` provides commands to search, add, remove, and list libraries
- **Auto-Installation**: `setup.sh` reads `arduino-libraries.txt` and installs all listed libraries
- **Required Libraries**: ArduinoJson@7.2.1, ESP Async WebServer@3.9.0, Async TCP@3.4.9

## WSL-Specific Requirements

Serial port access requires:

1. `usbipd-win` to bind USB devices from Windows host
2. User must be in `dialout` group: `sudo usermod -a -G dialout $USER`
3. Full WSL restart after group change: `wsl --terminate Ubuntu` (PowerShell)

See `docs/dev/wsl-development.md` for complete USB/IP setup guide.

## Common Pitfalls

- **Permission denied on /dev/ttyUSB0**: User not in dialout group or WSL not restarted
- **arduino-cli not found**: Scripts support both local (`./bin/arduino-cli`) and system-wide installations
- **Upload with sudo fails**: Root user lacks ESP32 core installation; use dialout group instead

## Scoped Instruction Files

Domain-specific conventions are in separate `.github/instructions/*.instructions.md` files that load automatically based on `applyTo` file patterns:

| File | Scope | Content |
|---|---|---|
| `terminology.instructions.md` | All files | Enforced naming: screen, pad, button, widget |
| `agent-guidelines.instructions.md` | All files | Approval workflow, build verification, docs checklist |
| `web-portal.instructions.md` | Web portal files | Multi-page architecture, REST API, UI design |
| `adding-config-settings.instructions.md` | Config/portal files | NVS settings checklist (backend, API, frontend) |
| `display-touch.instructions.md` | Driver/display files | HAL conventions, compilation units, board overrides |
| `compile-time-flags.instructions.md` | Board config files | HAS_* feature gates, driver selectors, compilation units |
| `binding-system.instructions.md` | Binding engine files | Scheme registration, token syntax, thread safety |
| `diagramming.instructions.md` | Markdown files | Mermaid-only diagram requirement |
| `review-experts/*.instructions.md` | All files | Code review expert panels (architecture, binding, DRY, etc.) |
