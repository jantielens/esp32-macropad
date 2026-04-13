# Copilot Instructions for ESP32 Macropad Project

## Project Overview

ESP32 Macropad — a feature-rich, configurable macropad firmware for ESP32 devices with touch screens. Built with `arduino-cli` for headless builds. Designed for WSL2/Linux environments with local toolchain installation (no system dependencies). All supported boards have a display and touch input.

## Architecture

- **Build System**: Custom bash scripts wrapping `arduino-cli` (installed locally to `./bin/`)
- **Sketch Location**: Main Arduino file at `src/app/app.ino`
- **Board Configuration**: `src/app/board_config.h` (defaults) + `src/boards/[board-name]/board_overrides.h` (per-board overrides). Uses `#if HAS_xxx` conditional compilation.
- **Display & Touch**: HAL-based architecture with LVGL. See `docs/dev/display-touch-architecture.md` and `.github/instructions/display-touch.instructions.md`.
- **Image Fetch** (`HAS_IMAGE_FETCH`): Slot-based FreeRTOS background HTTP(S) image fetcher with JPEG/PNG decode, bilinear scaling, and MJPEG streaming.
- **Icon Store** (`HAS_DISPLAY`): PNG icon storage on LittleFS with PSRAM-cached ARGB8888 draw buffers.
- **Custom Fonts** (`HAS_CUSTOM_FONTS`): 3 font families (DSEG7, Bebas, Doto) × 7 sizes. LabelStyle DSL: `font_family:dseg7`, `font_family:bebas`, `font_family:doto`.
- **Widget Subsystem** (`HAS_DISPLAY`): Extensible widget types — gauge, sparkline, bar chart, table, rocker. Each widget renders inside a button.
- **Data Stream Registry** (`HAS_DISPLAY && HAS_MQTT`): Demand-driven per-widget ring buffers in PSRAM for history-based widgets.
- **Binding Template Engine** (`HAS_MQTT`): Scheme-extensible `[scheme:params]` token resolver. Schemes: `mqtt`, `health`, `time`, `expr`, `pad`, `timer`. Pipe fallback: `[scheme:params|fallback]`. Called only from LVGL task.
- **Timer Subsystem** (`HAS_DISPLAY`): 3 independent count-up/down timers with expire actions, `[timer:N]` binding scheme, REST API.
- **Screen Saver** (`HAS_DISPLAY`): Inactivity-based display sleep with fade animation and per-screen wake redirect.
- **MQTT Screen Control** (`HAS_MQTT && HAS_DISPLAY`): HA `select` entity for remote screen navigation.
- **MQTT Wake** (`HAS_MQTT && HAS_DISPLAY`): Binding-driven screensaver wakeup with idle-timer keep-alive.
- **Notification Bubble** (`HAS_DISPLAY`): Message overlay with fade animation, tap-to-dismiss, HA remote trigger, `ACTION_TYPE_NOTIFY`.
- **Swipe Actions** (`HAS_DISPLAY`): 4-direction configurable swipe gestures with full ButtonAction parity.
- **Boot Actions** (`HAS_DISPLAY`): Device-level actions dispatched once at boot after first screen.
- **Button Defaults** (`HAS_DISPLAY`): Device-wide default button appearance (colors, border, radius, label styles).
- **Action System** (`HAS_DISPLAY`): Shared `action_dispatch()` for buttons, swipe, boot, timer expire. `action_parse()` for DRY JSON serialization.
- **BLE HID** (`HAS_BLE_HID`): NimBLE keyboard with key sequence DSL, single-owner pairing, auto-re-pair. Runtime-toggled.
- **Audio** (`HAS_AUDIO`): ES8311 codec + I2S, beep pattern DSL, volume control, async FreeRTOS playback.
- **Sound Player** (`HAS_SOUND_PLAYER`): MP3 decode (minimp3) + resample + I2S playback from LittleFS.
- **MQTT Audio** (`HAS_AUDIO && HAS_MQTT`): HA siren, volume, beep buttons, custom tone entities.
- **Power + Transport**: Power modes, BLE/MQTT transport selection, duty-cycle runtime, WiFi manager, portal idle timeout.
- **Web Portal**: Multi-page async web server with captive portal. See `.github/instructions/web-portal.instructions.md`.
- **Pad Config**: `pad_config.cpp/h` — JSON parser for pad/button/widget configuration, `LabelStyle` DSL, `ButtonAction` types, `ButtonDefaults` cascade, `template_pad` inheritance.
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

### Documentation

- `docs/dev/logging-guidelines.md` — Logging rules and format (LOGx macros, severity, modules)
- `docs/dev/web-portal.md` — Web portal and REST API guide
- `docs/dev/display-touch-architecture.md` — Display/touch HAL and screen architecture
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
