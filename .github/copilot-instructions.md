# Copilot Instructions for ESP32 Template Project

## Project Overview

ESP32 Arduino development template using `arduino-cli` for headless builds. Designed for WSL2/Linux environments with local toolchain installation (no system dependencies).

## Architecture

- **Build System**: Custom bash scripts wrapping `arduino-cli` (installed locally to `./bin/`)
- **PNG Assets (LVGL)**: Optional `assets/png/*.png` conversion to `lv_img_dsc_t` symbols (generated into `src/app/png_assets.cpp/h` by `tools/png2lvgl_assets.py` when building display-enabled boards)
- **Sketch Location**: Main Arduino file at `src/app/app.ino`
- **Board Configuration**: Flexible system with optional board-specific overrides
  - `src/app/board_config.h` - Default configuration for all boards
  - `src/boards/[board-name]/board_overrides.h` - Optional board-specific compile-time defines
  - Build system automatically detects and applies overrides when present
  - Application uses `#if HAS_xxx` conditional compilation for board-specific logic
- **Display & Touch Subsystem**: HAL-based architecture with LVGL integration (see `docs/display-touch-architecture.md`)
  - `display_driver.h` - DisplayDriver HAL interface (`RenderMode`, `present()`, `configureLVGL()`)
  - `display_manager.cpp/h` - Hardware lifecycle, LVGL init, FreeRTOS rendering task
  - `touch_driver.h` - TouchDriver HAL interface
  - `touch_manager.cpp/h` - Touch input registration and calibration
  - `display_drivers.cpp` - Sketch-root “translation unit” that conditionally includes exactly one selected display driver `.cpp`
  - `touch_drivers.cpp` - Sketch-root “translation unit” that conditionally includes exactly one selected touch driver `.cpp`
  - `drivers/` - Driver implementations (TFT_eSPI, Arduino_GFX, ST77916, ST7701_RGB, MIPI-DSI base, ST7703_DSI, ST7701_DSI, XPT2046, AXS15231B, CST816S, GT911)
  - `screens/` - Screen base class and implementations (splash, info, test, touch test)
  - Conditional compilation: Only selected drivers are compiled via `display_drivers.cpp` / `touch_drivers.cpp` (Arduino doesn’t auto-compile subdir `.cpp`)
- **Power + Transport Subsystem**: Power modes, BLE/MQTT transport selection, and duty-cycle runtime
  - `power_config.cpp/h` - Power mode and transport parsing helpers
  - `power_manager.cpp/h` - Boot mode selection, backoff tracking, LED modes, sleep helpers
  - `duty_cycle.cpp/h` - Duty-cycle execution path (BLE/MQTT publish then sleep)
  - `ble_advertiser.cpp/h` - BTHome v2 BLE advertising (compile-time gated)
  - `wifi_manager.cpp/h` - WiFi connect, cached BSSID, and mDNS startup
  - `portal_idle.cpp/h` - Portal idle timeout and sleep (Config/AP only)
- **Web Portal**: Multi-page async web server with captive portal support
  - `web_portal.cpp/h` - Server and REST API implementation
  - `web_assets.h` - Embedded HTML/CSS/JS (from `src/app/web/`) (auto-generated)
  - `project_branding.h` - Project branding defines (`PROJECT_NAME`, `PROJECT_DISPLAY_NAME`) (auto-generated)
  - `config_manager.cpp/h` - NVS configuration storage
  - Multi-page architecture: Home, Network, Firmware
  - Template system: `_header.html`, `_nav.html`, `_footer.html` for DRY
- **Output**: Compiled binaries in `./build/<board-name>/` directories
- **Board Targets**: Multi-board support via `FQBN_TARGETS` associative array in `config.sh`
  - Board name → FQBN mapping allows multiple board variants with same FQBN
  - `["esp32-nodisplay"]="esp32:esp32:esp32"` → `build/esp32-nodisplay/` (ESP32 Dev Module, no display)
  - `["esp32c6"]="esp32:esp32:esp32c6:CDCOnBoot=cdc"` → `build/esp32c6/` (ESP32-C6 Dev Module)
  - `["cyd-v2"]="esp32:esp32:esp32"` → `build/cyd-v2/` (CYD v2 - same FQBN as esp32, different config)
  - `["esp32-p4-lcd4b"]="esp32:esp32:esp32p4:FlashSize=32M,PSRAM=enabled,PartitionScheme=ota_8mb_32MB"` → `build/esp32-p4-lcd4b/` (Waveshare ESP32-P4-WIFI6-Touch-LCD-4B, 720×720 MIPI-DSI + GT911 touch, 32MB Flash + 32MB PSRAM)

## Critical Developer Workflows

### First-time Setup
```bash
./setup.sh  # Downloads arduino-cli, installs ESP32 core, configures environment
```

### Build-Upload-Monitor Cycle
```bash
# Build all configured boards
./build.sh

# Or build specific board
./build.sh esp32-nodisplay                   # Compile for ESP32 Dev Module (no display)
./build.sh cyd-v2                            # Compile for CYD v2 display board
./build.sh esp32c6      # Compile for ESP32-C6 Dev Module

# Upload (board name required when multiple boards configured)
./upload.sh esp32-nodisplay                   # Auto-detects /dev/ttyUSB0 or /dev/ttyACM0
./upload.sh cyd-v2                            # Auto-detects /dev/ttyUSB0 or /dev/ttyACM0

# Monitor
./monitor.sh            # Serial monitor at 115200 baud

# Convenience scripts
./bum.sh esp32-nodisplay                   # Build + Upload + Monitor
./um.sh cyd-v2                             # Upload + Monitor
```

All scripts use absolute paths via `SCRIPT_DIR` resolution - they work from any directory.

## Project-Specific Conventions

### Script Design Pattern
- All scripts source `config.sh` for common configuration and helper functions
- `config.sh` provides:
  - `SCRIPT_DIR` resolution for absolute path handling
  - `find_arduino_cli()` - Checks local `$SCRIPT_DIR/bin/arduino-cli` first, then system-wide
  - `find_serial_port()` - Auto-detects `/dev/ttyUSB0` first, fallback to `/dev/ttyACM0`
  - Project constants: `PROJECT_NAME`, `SKETCH_PATH`, `BUILD_PATH`
  - Board management: `FQBN_TARGETS` array, `get_board_name()`, `list_boards()`, `get_fqbn_for_board()`
- Scripts work from any directory due to absolute path resolution
- Multi-board scripts require board name parameter when multiple targets are configured
- To reduce merge conflicts in template-based projects, `config.sh` can source an optional `config.project.sh` with project-specific overrides.

### Arduino Code Standards
- Use `Serial.begin(115200)` for consistency with monitor.sh default
- Include startup diagnostics (chip model, revision, CPU freq, flash size) using `ESP.*` functions
- Implement heartbeat pattern with `millis()` for long-running loops (5s interval)

### Web Portal Conventions

**Multi-Page Architecture**:
- **Home** (`/` or `/home.html`): Custom settings and welcome message (Full Mode only)
- **Network** (`/network.html`): WiFi, device, and network configuration (both modes)
- **Firmware** (`/firmware.html`): Online update (GitHub Releases), manual upload, and factory reset (Full Mode only)
- Template fragments: `_header.html`, `_nav.html`, `_footer.html` used via `{{HEADER}}`, `{{NAV}}`, `{{FOOTER}}` placeholders
- Build-time template replacement in `tools/minify-web-assets.sh`

**Portal Modes**:
- **Core Mode**: AP with captive portal (192.168.4.1) - WiFi not configured
  - Only Network page accessible (Home/Firmware redirect to Network)
  - Navigation tabs for Home/Firmware hidden via JavaScript
- **Full Mode**: Connected to WiFi - portal at device IP/hostname
  - All three pages accessible

**Responsive Design**:
- Container max-width: 900px
- 2-column grid on desktop (≥768px) using `.grid-2col` class
- Sections stack vertically on mobile (<768px)
- Network page: WiFi + Device Settings side-by-side, Network Config full-width
- Home page: Hello World + Sample Settings side-by-side

**REST API Design**:
- All endpoints under `/api/*` namespace
- Use semantic names: `/api/info` (device info), `/api/health` (real-time stats), `/api/config` (settings)
- Return JSON responses with proper HTTP status codes
- POST `/api/config` triggers device reboot (use `?no_reboot=1` to skip)
- Partial config updates: Backend only updates fields present in JSON request via `doc.containsKey()`

**Health Monitoring**:
- `/api/health` provides real-time metrics (CPU, memory, WiFi, temperature, uptime)
- CPU usage calculated via IDLE task: `100 - (idle_runtime/total_runtime * 100)`
- Temperature sensor with `SOC_TEMP_SENSOR_SUPPORTED` guards for cross-platform compatibility
- Update interval: 10s (compact widget), 5s (expanded widget)

**UI Design**:
- Minimalist card-based layout with gradient header
- 6 header badges with fixed widths and format placeholders:
  - Firmware version (`Firmware v-.-.-` → `Firmware v0.0.1`) - 140px
  - Chip info (`--- rev -` → `ESP32-C6 rev 2`) - 140px
  - CPU cores (`- Core` → `1 Core`) - 75px
  - CPU frequency (`--- MHz` → `160 MHz`) - 85px
  - Flash size (`-- MB Flash` → `8 MB Flash`) - 110px
  - PSRAM status (`No PSRAM` → `No PSRAM` or `2 MB PSRAM`) - 105px
- Floating health widget with compact/expanded views
- Breathing animation on status updates
- Tabbed navigation with active page highlighting

## WSL-Specific Requirements

Serial port access requires:
1. `usbipd-win` to bind USB devices from Windows host
2. User must be in `dialout` group: `sudo usermod -a -G dialout $USER`
3. Full WSL restart after group change: `wsl --terminate Ubuntu` (PowerShell)

See `docs/wsl-development.md` for complete USB/IP setup guide.

## Key Files

### Scripts
- `config.sh` - Common configuration and helper functions (sourced by all scripts)
- `setup.sh` - Downloads arduino-cli v0.latest, configures ESP32 platform, installs libraries
- `build.sh` - Compiles to `./build/<board-name>/*.bin` files (all boards or specific board)
- `upload.sh` - Flashes firmware via serial (requires board name with multiple boards)
- `upload-erase.sh` - Completely erases ESP32 flash memory (requires board name with multiple boards)
- `monitor.sh` - Serial console (Ctrl+C to exit)
- `clean.sh` - Removes all build artifacts (all board directories)
- `library.sh` - Manages Arduino library dependencies
- `bum.sh` - Build + Upload + Monitor convenience script
- `um.sh` - Upload + Monitor convenience script

### Source
- `src/app/app.ino` - Main sketch file (standard Arduino structure)
- `src/app/board_config.h` - Default board configuration (LED pins, WiFi settings)
- `src/app/png_assets.cpp/h` - Generated LVGL PNG assets (auto-generated when `assets/png/*.png` exists and building a display-enabled board)
- `src/boards/[board-name]/board_overrides.h` - Optional board-specific compile-time configuration
- `src/app/web_portal.cpp/h` - Async web server and REST API endpoints
- `src/app/web_assets.h` - Embedded HTML/CSS/JS from `src/app/web/` (auto-generated)
- `src/app/project_branding.h` - Project branding defines (`PROJECT_NAME`, `PROJECT_DISPLAY_NAME`) (auto-generated)
- `src/app/config_manager.cpp/h` - NVS-based configuration storage
- `src/app/power_manager.cpp/h` - Power mode selection, backoff, and sleep helpers
- `src/app/power_config.cpp/h` - Power mode and transport parsing
- `src/app/duty_cycle.cpp/h` - Duty-cycle runner
- `src/app/ble_advertiser.cpp/h` - BLE BTHome advertiser (compile-time gated by `HAS_BLE`)
- `src/app/wifi_manager.cpp/h` - WiFi connect + mDNS (replaces inline connect logic)
- `src/app/portal_idle.cpp/h` - Portal idle timeout in Config/AP modes
- `src/app/display_driver.h` - Display HAL interface with configureLVGL() hook
- `src/app/display_manager.cpp/h` - Display lifecycle, LVGL init, FreeRTOS rendering task
- `src/app/touch_driver.h` - Touch HAL interface
- `src/app/touch_manager.cpp/h` - Touch input device registration and calibration
- `src/app/display_drivers.cpp` - Display driver compilation unit (selected driver `.cpp` includes live here)
- `src/app/touch_drivers.cpp` - Touch driver compilation unit (selected driver `.cpp` includes live here)
- `src/app/drivers/tft_espi_driver.cpp/h` - TFT_eSPI display driver (hardware rotation)
- `src/app/drivers/xpt2046_driver.cpp/h` - XPT2046 resistive touch driver
- `src/app/drivers/arduino_gfx_driver.cpp/h` - Arduino_GFX display backend (AXS15231B QSPI)
- `src/app/drivers/arduino_gfx_st77916_driver.cpp/h` - Arduino_GFX ST77916 QSPI display driver (JC3636W518)
- `src/app/drivers/st7703_dsi_driver.cpp/h` - ST7703 MIPI-DSI display subclass (Waveshare ESP32-P4-WIFI6-Touch-LCD-4B)
- `src/app/drivers/st7701_dsi_driver.cpp/h` - ST7701 MIPI-DSI display subclass (GUITION JC4880P433)
- `src/app/drivers/mipi_dsi_driver.cpp/h` - Shared MIPI-DSI base class with DMA2D async flush (ESP32-P4)
- `src/app/drivers/axs15231b_touch_driver.cpp/h` - AXS15231B touch backend wrapper
- `src/app/drivers/axs15231b/vendor/AXS15231B_touch.cpp/h` - Vendored AXS15231B touch implementation (driver-scoped vendor code)
- `src/app/drivers/wire_cst816s_touch_driver.cpp/h` - CST816S Wire I2C touch driver (JC3636W518)
- `src/app/drivers/README.md` - Driver selection conventions + generated board→drivers table
- `src/app/screens/screen.h` - Screen base class interface
- `src/app/screens/splash_screen.cpp/h` - Boot splash with animated spinner
- `src/app/screens/info_screen.cpp/h` - Device info and real-time stats
- `src/app/screens/test_screen.cpp/h` - Display calibration and color testing
- `src/app/screens/touch_test_screen.cpp/h` - Touch accuracy test (red dots + connecting lines, PSRAM canvas, HAS_TOUCH only)
- `src/app/screens.cpp` - Screen compilation unit (includes all screen .cpp files)
- `src/app/web/_header.html` - Common HTML head template
- `src/app/web/_nav.html` - Navigation tabs and loading overlay wrapper
- `src/app/web/_footer.html` - Form buttons template
- `src/app/web/home.html` - Home page (Hello World + Sample Settings)
- `src/app/web/network.html` - Network configuration page
- `src/app/web/firmware.html` - Firmware page (online update, manual upload, factory reset)
- `src/app/web/portal.css` - Styles (gradients, animations, responsive grid)
- `src/app/web/portal.js` - Client-side logic (multi-page support, API calls, health updates)
- `src/version.h` - Firmware version tracking

### Documentation
- docs/logging-guidelines.md - Logging rules and format (LOGx macros, severity, modules)

### Tools
- `tools/minify-web-assets.sh` - Minifies and embeds web assets into `web_assets.h`
  - Replaces `{{HEADER}}`, `{{NAV}}`, `{{FOOTER}}` placeholders in HTML files
  - Minifies CSS and JavaScript
  - Gzips all assets for efficient storage
  - Excludes template fragments (files starting with `_`)

- `tools/png2lvgl_assets.py` - Converts `assets/png/*.png` into LVGL `lv_img_dsc_t` symbols (requires Python + Pillow)

- `tools/generate-board-driver-table.py` - Generates the board→drivers table from `src/boards/*/board_overrides.h`
- `tools/generate-board-driver-table.py` - Generates the board→drivers table from `src/boards/*/board_overrides.h`
  - Auto-discovers available display/touch backends from `src/app/display_drivers.cpp` and `src/app/touch_drivers.cpp`
  - `python3 tools/generate-board-driver-table.py --update-drivers-readme`

### Configuration
- `config.sh` - Project paths, FQBN_TARGETS array, and helper functions
- `arduino-libraries.txt` - List of required Arduino libraries (auto-installed by setup.sh)
- `.github/workflows/build.yml` - CI/CD pipeline with matrix builds for all board variants

## Library Management

- **Configuration File**: `arduino-libraries.txt` lists all required Arduino libraries
- **Management Script**: `./library.sh` provides commands to search, add, remove, and list libraries
- **Auto-Installation**: `setup.sh` reads `arduino-libraries.txt` and installs all listed libraries
- **CI/CD Integration**: GitHub Actions automatically installs libraries via `setup.sh`
- **Required Libraries**:
  - `ArduinoJson@7.2.1` - JSON serialization for REST API
  - `ESP Async WebServer@3.9.0` - Non-blocking web server
  - `Async TCP@3.4.9` - Async TCP dependency

### Library Commands
```bash
./library.sh search <keyword>    # Find libraries
./library.sh add <library>       # Add to config and install
./library.sh list                # Show configured libraries
```

## Adding New Configuration Settings

When adding new configuration settings (e.g., MQTT, custom features), follow this complete checklist. For more details on the web portal architecture and REST API, see `docs/web-portal.md`.

### 1. Backend: Configuration Storage

**Update `config_manager.h`:**
- Add `#define` constants for maximum field lengths (e.g., `CONFIG_MQTT_BROKER_MAX_LEN`)
- Add new fields to the `DeviceConfig` struct
- For strings: Use `char field_name[CONFIG_XXX_MAX_LEN]`
- For numbers: Use appropriate types (`uint16_t`, `int`, `float`, etc.)

**Update `config_manager.cpp`:**
- Add `#define` keys for NVS storage (e.g., `KEY_MQTT_BROKER "mqtt_broker"`)
- Update `config_manager_load()` to load new fields from NVS
  - Use `preferences.getString()` for strings
  - Use `preferences.getUShort()`, `preferences.getInt()`, etc. for numbers
  - Provide sensible defaults (second parameter)
- Update `config_manager_save()` to save new fields to NVS
  - Use `preferences.putString()` for strings
  - Use `preferences.putUShort()`, `preferences.putInt()`, etc. for numbers
- Update `config_manager_print()` to log new settings for debugging

### 2. Backend: Web API

**Update `web_portal.cpp`:**
- In `handleGetConfig()`: Add new fields to JSON response
  - Use `doc["field_name"] = config->field_name`
  - For passwords: Return empty string (`doc["password_field"] = ""`)
- In `handlePostConfig()`: Handle new fields from JSON request
  - Use `if (doc.containsKey("field_name"))` for partial updates
  - Use `doc["field_name"] | default_value` syntax for safe extraction
  - Handle passwords specially (only update if non-empty)

### 3. Frontend: HTML Form

**Update appropriate HTML page (e.g., `network.html`, `home.html`):**
- Add form section with descriptive heading
- Add input fields with proper attributes:
  - `id` and `name` must match the backend field name exactly
  - `type` (text, number, password, etc.)
  - `maxlength` should match the backend max length constant
  - `placeholder` with helpful examples
  - `required` attribute if field is mandatory
- Add `<small>` helper text under each field
- Use `.grid-2col` class for side-by-side layout on desktop

### 4. Frontend: JavaScript

**Update `portal.js`:**
- In `buildConfigFromForm()` function:
  - Add new field names to the `fields` array (around line 484-486)
  - Fields are automatically read from form inputs by the existing code
- In `loadConfig()` function:
  - Add `setValueIfExists('field_name', config.field_name)` calls
  - For passwords: Set placeholder text if saved, leave value empty
  - For numbers: Use `setValueIfExists()` with numeric values
- Optionally add validation in `validateConfig()` if needed

### 5. Usage in Application Code

**Initialize with loaded config:**
```cpp
// In setup() or after config_manager_load()
if (strlen(device_config.mqtt_broker) > 0) {
    // Use the configuration
    some_manager_init(&device_config);
}
```

**Access configuration:**
```cpp
// Configuration is available in device_config global
Serial.printf("Broker: %s:%d\n", device_config.mqtt_broker, device_config.mqtt_port);
```

### Example: Adding MQTT Settings

Complete example of adding MQTT configuration (as implemented in this project):

1. **config_manager.h**: Added 6 MQTT fields (broker, port, username, password, topic_solar, topic_grid)
2. **config_manager.cpp**: Added 6 NVS keys and load/save/print logic
3. **web_portal.cpp**: Added MQTT fields to GET/POST `/api/config` handlers
4. **network.html**: Added MQTT Settings section with 6 input fields in 2-column grid
5. **portal.js**: Added 6 MQTT fields to `fields` array and loadConfig function
6. **Build**: Ran `./build.sh` to regenerate web assets

### Common Mistakes to Avoid

❌ **Forgetting to update portal.js fields array** → Settings won't be saved
❌ **Mismatched field names** between HTML `id`, JS, and backend → Data won't transfer
❌ **Not rebuilding after HTML/JS changes** → Old code still embedded in firmware
❌ **Missing default values in load function** → Uninitialized data
❌ **Not using `doc.containsKey()`** in POST handler → Can't do partial updates

## Common Pitfalls

- **Permission denied on /dev/ttyUSB0**: User not in dialout group or WSL not restarted
- **arduino-cli not found**: Scripts support both local (`./bin/arduino-cli`) and system-wide installations
- **Upload with sudo fails**: Root user lacks ESP32 core installation; use dialout group instead

## Copilot Agent Guidelines

### Before Making Significant Changes

Before implementing significant changes or starting major work, the agent must:

1. **Create a concise summary** of the proposed changes including:
   - What will be changed and why
   - Which files will be affected
   - Expected impact on the project
   - Any potential risks or breaking changes

2. **Ask for user approval and/or feedback** and wait for confirmation

3. **Only proceed with implementation** after receiving user approval

### After Significant Changes

After every significant change, the agent must:

1. **Verify the changes by building** the code:
   - Run `./build.sh` to ensure the code compiles successfully
   - Check for any compilation errors or warnings
   - Only proceed if the build completes without errors

2. **Check if documentation needs updates** by reviewing:
   - `README.md` - Main project documentation
   - `docs/web-portal.md` - Web portal and REST API guide
   - `docs/display-touch-architecture.md` - Display/touch HAL and screen architecture
   - `docs/scripts.md` - Script usage guide
   - `docs/library-management.md` - Library management guide
   - `docs/build-and-release-process.md` - Project branding, build system, and release workflow guide
   - `docs/wsl-development.md` - WSL setup guide
   - `.github/copilot-instructions.md` - This file
   - `.github/workflows/build.yml` - CI/CD build pipeline
   - `.github/workflows/release.yml` - CI/CD release pipeline

3. **Update existing documentation** if changes affect documented behavior

4. **Before creating new documentation files**, ask the user:
   - "Should I create a new doc file for [topic]?"
   - Wait for confirmation before creating new docs

### Examples of Significant Changes

- Adding new scripts or tools
- Modifying build/upload/monitor workflows
- Changing project structure
- Adding new dependencies or requirements
- Updating CI/CD pipeline
- Changing library management approach

### Examples of Documentation Updates Needed

- New script added → Update `README.md` script table and `docs/scripts.md`
- Library management changed → Update `docs/library-management.md`
- Build workflow modified → Update `README.md` CI/CD section and `docs/build-and-release-process.md`
- Board configuration system changed → Update `README.md` board configuration section and `docs/build-and-release-process.md`
- Release workflow modified → Update `docs/build-and-release-process.md` and `README.md` release section
- New requirement added → Update `README.md` prerequisites
- REST API endpoint added/changed → Update `docs/web-portal.md` and `README.md` API table
- Web UI feature changed → Update `docs/web-portal.md` features section
- Display/touch driver added/changed → Update `docs/display-touch-architecture.md` driver sections
- Screen management changed → Update `docs/display-touch-architecture.md` screen lifecycle
- New version released → Update `CHANGELOG.md` with changes, update `src/version.h` with new version number
- Release process changed → Update `docs/build-and-release-process.md` with new workflow

### Build Verification

Always verify code changes by building:

```bash
./build.sh  # Must complete successfully after code changes
```

If the build fails:
- Review and fix compilation errors
- Check library dependencies in `arduino-libraries.txt`
- Verify Arduino code syntax and ESP32 compatibility

## Display/Touch Driver Conventions (v1)

- **Single source of defaults**: default `DISPLAY_DRIVER` / `TOUCH_DRIVER` live in `src/app/board_config.h`.
- **Per-board selection**: each board override should have a clear **Driver Selection (HAL)** block that explicitly sets the HAL selectors:
  - `#define DISPLAY_DRIVER DISPLAY_DRIVER_...`
  - `#define TOUCH_DRIVER TOUCH_DRIVER_...` (or `#define HAS_TOUCH false`)
- **Direct vs Buffered**:
  - Direct drivers push pixels during the LVGL flush callback.
  - Buffered drivers return `renderMode() == Buffered` and implement `present()`.
- **Arduino build limitation**: do not include driver `.cpp` files in manager files; add conditional includes to `src/app/display_drivers.cpp` or `src/app/touch_drivers.cpp` instead.
- **Board→driver visibility**: after editing board overrides, regenerate the table in `src/app/drivers/README.md`:
  - `python3 tools/generate-board-driver-table.py --update-drivers-readme`
- **Vendored code placement**: third-party source that is not an Arduino library should live under the driver that uses it (driver-scoped vendor code), not in a shared `drivers/vendor/` bucket.
