# ESP32 Development Template

[![Deploy Release to Installer Site (Github Pages)](https://github.com/jantielens/esp32-template-wifi/actions/workflows/pages-from-release.yml/badge.svg)](https://github.com/jantielens/esp32-template-wifi/actions/workflows/pages-from-release.yml)

Skip the boilerplate and start building. ESP32 Arduino template with automated builds, captive portal WiFi setup, OTA updates, and flexible multi-board build system. Add new board variants in seconds with a simple config change, customize board-specific code when needed. Same build system powers local development and GitHub Actions workflows for CI/CD and releases.

## ✨ Features

- **🚀 Get Started Instantly**
  - **Zero Dependencies**: Local `arduino-cli` installation (no sudo, no system packages)
  - **One-Command Setup**: `./setup.sh` downloads toolchain and configures ESP32 platform
  - **Simple Scripts**: Build with `./build.sh`, upload with `./upload.sh`, monitor with `./monitor.sh`

- **📡 Production-Ready WiFi Management**
  - **Auto-AP Mode**: Device creates captive portal when unconfigured
  - **Web Configuration**: User-friendly interface at `192.168.4.1` for WiFi setup
  - **REST API**: Full `/api/*` endpoints for device control and monitoring
  - **OTA Updates**: Upload new firmware over WiFi without USB cable

- **⚡ Power Modes (Optional)**
  - **Always-On (default)**: Legacy always-connected WiFi behavior
  - **Duty-Cycle**: Wake → sample sensors → advertise BLE / publish MQTT → sleep
  - **Config/AP Auto-Sleep**: Portal auto-sleeps after inactivity timeout

- **📶 Optional BLE Telemetry (BTHome v2)**
  - **Compile-Time Gated**: Enable with `HAS_BLE` per board
  - **BTHome Advertising**: Broadcast sensor telemetry via BLE service data
  - **Burst Controls**: Configurable advertising burst/gap timing

- **🖥️ Optional Display + Touch Support**
  - **Works With or Without a Display**: Devices without displays build and run normally
  - **Display/Touch HAL**: Unified `DisplayDriver` / `TouchDriver` interfaces with `DisplayManager` / `TouchManager` lifecycle
  - **LVGL UI Framework**: Multi-screen UI support (when `HAS_DISPLAY` is enabled for a board)
  - **PNG Assets (LVGL)**: Top-level PNGs in `assets/png/` are converted to LVGL `lv_img_dsc_t` symbols (e.g. `img_logo`) during build for display-enabled boards

- **🎯 Multi-Board Made Easy**
  - **Flexible Build System**: Add ESP32 variants with single config line
  - **Board-Specific Code**: Optional overrides for different GPIO pins and hardware
  - **Matrix Builds**: GitHub Actions compiles all boards in parallel

- **🌍 Web Firmware Installer**
  - **Browser Flashing**: GitHub Pages + ESP Web Tools (no backend required)
  - **Release-Driven Deploy**: Hosted installer updates on stable releases (see [Web Firmware Installer](#web-firmware-installer-esp-web-tools))
  - **Device-Side Online Update**: Firmware page links to the GitHub Pages updater, which calls the device API with the selected OTA manifest

- **⚙️ Professional Development Workflow**
  - **Automated Releases**: Tag-based releases with firmware binaries and changelogs
  - **CI/CD Integration**: Same build scripts work locally and in GitHub Actions
  - **Version Management**: Semantic versioning with automatic firmware stamping

## 🚀 Quick Start

### Prerequisites

- Linux or WSL2 environment
- USB connection to ESP32 device
- Bash shell

### 1. Clone and Setup

```bash
git clone https://github.com/jantielens/esp32-template.git
cd esp32-template
./setup.sh
```

If you created your repo from this GitHub template (recommended), set up an upstream remote once so you can pull template updates later:

```bash
git remote add template https://github.com/jantielens/esp32-template-wifi.git
git fetch template

# Optional but strongly recommended: reuse conflict resolutions across merges
git config rerere.enabled true
```

### 2. Configure Project Branding (Optional)

Recommended: copy `config.project.sh.example` to `config.project.sh` and put your project-specific settings there:

```bash
PROJECT_NAME="my-iot-device"              # Slug for filenames/artifacts
PROJECT_DISPLAY_NAME="My IoT Device"      # Human-readable name for UI
```

Alternatively (standalone use), you can edit these values directly in `config.sh`.

This updates web portal titles, device names, AP SSID, and release artifacts. See [Build and Release Process](docs/build-and-release-process.md#project-branding-configuration) for details.

Tip (recommended for template-based projects): instead of editing `config.sh` directly, copy `config.project.sh.example` to `config.project.sh` and put your project-specific settings there. `config.sh` will automatically source it if present, which makes it much easier to merge template updates later.

### 3. Build, Upload, Monitor

```bash
./build.sh              # Compile firmware for all boards
./build.sh esp32-nodisplay        # Compile for specific board
BOARD_PROFILE=psram ./build.sh esp32-nodisplay  # Optional build profile (if defined in config.sh)
./upload.sh esp32-nodisplay       # Upload to ESP32 (board name required if multiple boards)
./monitor.sh            # View serial output

# Or use convenience scripts:
./bum.sh esp32-nodisplay          # Build + Upload + Monitor
./um.sh esp32-nodisplay           # Upload + Monitor
```

### 4. Start Developing

Edit `src/app/app.ino` with your code and repeat step 2.

## 📁 Project Structure

```
esp32-template-wifi/
├── assets/
│   └── png/                       # Optional PNG assets (top-level only; converted to LVGL C arrays at build time)
├── bin/                           # Local arduino-cli installation
├── build/                         # Compiled firmware binaries
│   ├── esp32-nodisplay/           # ESP32 Dev Module builds (no display)
│   └── cyd-v2/                    # CYD v2 display board builds
├── docs/                          # Documentation
│   └── *.md                        # Guides (scripts, web portal, sensors, etc.)
├── src/
│   ├── app/
│   │   ├── app.ino                # Main sketch file
│   │   ├── *_manager.cpp/h        # Power, WiFi, display, portal, config managers
│   │   ├── power_*.cpp/h          # Power/transport configuration + logic
│   │   ├── ble_*.cpp/h            # BLE advertising (compile-time gated)
│   │   ├── duty_cycle*.cpp/h      # Duty-cycle flow
│   │   ├── sensors/               # Sensor adapters (BME280, dummy, etc.)
│   │   ├── web/                   # Portal HTML/CSS/JS assets
│   │   └── web_portal_*.cpp/h     # Portal routes, auth, API handlers
│   ├── boards/                    # Board-specific overrides (per-board folders)
│   └── version.h                  # Firmware version tracking
├── partitions/                    # Optional custom partition tables (see docs)
│   └── partitions_ota_1_9mb.csv    # OTA-friendly layout with larger app partitions
├── .github/
│   └── workflows/
│       └── build.yml              # CI/CD pipeline
├── config.sh                      # Common configuration
├── build.sh                       # Compile sketch
├── upload.sh                      # Upload firmware
├── upload-erase.sh                # Erase flash memory
├── monitor.sh                     # Serial monitor
├── clean.sh                       # Clean build artifacts
├── library.sh                     # Library management
├── bum.sh / um.sh                 # Convenience scripts
├── setup.sh                       # Environment setup
└── arduino-libraries.txt          # Library dependencies
```

## 🖼️ PNG Assets (LVGL)

If you add PNG files to `assets/png/`, the build can auto-generate LVGL image descriptors.

- **Folder**: `assets/png/*.png` (top-level only; no recursion)
- **Generated output**: `src/app/png_assets.cpp` and `src/app/png_assets.h`
- **Naming**: each file `name.png` becomes `img_name` (so it’s predictable in code)
- **Resolution**: preserved from the PNG (no resizing)
- **When included**: only for boards that set `#define HAS_DISPLAY true` in `src/boards/<board>/board_overrides.h` (and compiled out when `HAS_DISPLAY` is false)

Usage example:

```cpp
#include "png_assets.h"

lv_obj_t* img = lv_img_create(parent);
lv_img_set_src(img, &img_logo);
```

Constraints:

- Filenames must already be “C identifier safe” (letters/digits/underscore, not starting with a digit). If not, the generator fails with a descriptive error.
- The generator requires Python + Pillow. Run `./setup.sh` (tries to install Pillow), or install manually with `python3 -m pip install --user pillow`.

## 🌐 Web Configuration Portal

The template includes a full-featured web portal with multi-page architecture for device configuration and monitoring.

### Portal Pages

| Page | URL | Available In | Purpose |
|------|-----|--------------|---------|
| **Home** | `/` or `/home.html` | Full Mode | Custom settings and welcome message |
| **Network** | `/network.html` | Both modes | WiFi, device, and network configuration |
| **Firmware** | `/firmware.html` | Full Mode | Online update, manual upload, and factory reset |


### Portal Modes

**Core Mode (AP)**: Runs when WiFi is not configured
- Device creates Access Point: `ESP32-XXXXXX`
- Captive portal at: `http://192.168.4.1`
- Only Network page accessible
- Configure WiFi credentials and device settings

**Full Mode (WiFi)**: Runs when connected to WiFi
- Access at device IP or mDNS hostname
- All three pages accessible
- Real-time health monitoring
- Manual firmware upload + GitHub Pages online updates

### Security (Optional Basic Auth)

The portal supports optional HTTP Basic Authentication:

- **AP/Core mode**: always open (no auth) to keep onboarding and recovery simple.
- **STA/Full mode**: when enabled, the web portal pages and all `/api/*` endpoints require Basic Auth.

Configure it on the Network page under **Security**.

Example (external systems / scripts):

```bash
curl -u username:password http://device.local/api/info
```

## 🔄 Keeping Projects Updated With Template Changes

If you maintain multiple projects based on this template, the simplest low-friction approach is to treat this template repo as an upstream remote and periodically merge changes.

### One-time setup (per project repo)

```bash
git remote add template https://github.com/jantielens/esp32-template-wifi.git
git fetch template

# Optional but strongly recommended: reuse conflict resolutions across merges
git config rerere.enabled true
```

### Pull in template updates

```bash
git fetch template
git merge template/main
```

### Reduce merge conflicts

- Prefer putting project-specific branding/boards in `config.project.sh` (copy from `config.project.sh.example`) instead of editing `config.sh`.
- Ensure build-generated files are not committed in project repos (they are auto-generated by `./build.sh` and ignored by default). If an older project has them tracked, remove them once:

```bash
git rm --cached \
  src/app/web_assets.h \
  src/app/project_branding.h \
  src/app/repo_slug_config.h \
  src/app/png_assets.cpp \
  src/app/png_assets.h
```

### Features

- **Multi-Page Architecture**: Organized into Home, Network, and Firmware pages
- **Responsive Design**: 2-column grid on desktop (≥768px), single column on mobile
- **Partial Config Updates**: Each page only updates its own settings
- **Real-Time Monitoring**: Health badge in header with 11-metric expandable overlay
- **Optimized Loading**: 7 fixed-width badges with format placeholders prevent layout shift
- **Automatic Reconnection**: Best-effort automatic redirect after device reboot
- **Floating Action Footer**: Fixed bottom bar with Save & Reboot, Save, and Reboot buttons

### Device Discovery

When connected to WiFi, devices can be discovered using multiple methods:

- **📡 Router/DHCP Table**: Device appears with configured hostname (e.g., `esp32-1234`)
- **🔍 mDNS/Bonjour**: Access via `<hostname>.local` (e.g., `esp32-1234.local`)
  - Works on macOS, Linux, iOS, Android
  - Includes rich service metadata (version, model, device type, features, URL)
- **📊 Network Scanners**: Tools like Fing, WiFiMan show device with hostname

The hostname is automatically set from the device name and includes the last 4 hex digits of the chip ID for uniqueness (e.g., "ESP32 1234" → hostname "esp32-1234").

### REST API Endpoints

Build-time gating:
- Display endpoints require `HAS_DISPLAY` (typically set per-board in `src/boards/<board>/board_overrides.h`).

| Method | Endpoint | Purpose |
|--------|----------|----------|
| GET | `/api/info` | Device info (firmware, chip, cores, flash, PSRAM, hostname, MAC; plus `display_coord_width/height` when HAS_DISPLAY) |
| GET | `/api/health` | Real-time health stats (CPU, memory, WiFi, uptime, hostname) |
| GET | `/api/config` | Current configuration |
| POST | `/api/config` | Save configuration (triggers reboot by default) |
| POST | `/api/config?no_reboot=1` | Save configuration without rebooting |
| DELETE | `/api/config` | Reset to defaults (triggers reboot) |
| GET | `/api/mode` | Portal mode (core vs full) |
| POST | `/api/update` | OTA firmware upload |
| POST | `/api/firmware/update` | Start OTA download+install from a provided URL (GitHub Pages manifest) |
| GET | `/api/firmware/update/status` | Online update status/progress |
| POST | `/api/reboot` | Reboot device without saving |
| PUT | `/api/display/brightness` | Set backlight brightness immediately (no persist) |
| GET | `/api/display/sleep` | Screen saver status snapshot |
| POST | `/api/display/sleep` | Force screen saver sleep now |
| POST | `/api/display/wake` | Force wake now |
| POST | `/api/display/activity` | Reset idle timer; optionally wake (`?wake=1`) |
| PUT | `/api/display/screen` | Switch runtime screen (no persist) |

See [docs/web-portal.md](docs/web-portal.md) for detailed guide.

## 🛠️ Available Scripts

| Script | Purpose | Usage |
|--------|---------|-------|
| `config.sh` | Common configuration (sourced by scripts) | Sourced automatically |
| `setup.sh` | Install arduino-cli and ESP32 core | Run once during initial setup |
| `build.sh` | Compile the Arduino sketch | `./build.sh [board-name]` |
| `upload.sh` | Upload firmware to ESP32 | `./upload.sh [board-name] [port]` |
| `monitor.sh` | Display serial output | `./monitor.sh [port] [baud]` |
| `bum.sh` | Build + Upload + Monitor (full cycle) | `./bum.sh [board-name] [port]` |
| `um.sh` | Upload + Monitor | `./um.sh [board-name] [port]` |
| `upload-erase.sh` | Completely erase ESP32 flash | `./upload-erase.sh [board-name] [port]` |
| `clean.sh` | Remove build artifacts | `./clean.sh` |
| `library.sh` | Manage Arduino libraries | `./library.sh [command]` |

See [docs/scripts.md](docs/scripts.md) for detailed documentation.

## 🔧 Configuration

### Target Boards

The project supports multiple ESP32 board variants configured in `config.sh` (or, for template-based projects, in `config.project.sh`):

```bash
# Default configuration includes:
declare -A FQBN_TARGETS=(
  ["esp32-nodisplay"]="esp32:esp32:esp32"  # ESP32 Dev Module (no display)
  ["esp32c3_ota_1_9mb"]="esp32:esp32:nologo_esp32c3_super_mini:CDCOnBoot=cdc,PartitionScheme=ota_1_9mb"  # ESP32-C3 Super Mini (custom partitions)
  ["cyd-v2"]="esp32:esp32:esp32"  # CYD v2 (same FQBN, different board_overrides.h)
)
```

### Custom Partition Scheme Example (ESP32-C3)

This template includes an optional custom partition table to increase OTA app partition size on 4MB ESP32-C3 boards.

- Partition CSV: `partitions/partitions_ota_1_9mb.csv`
- Enabled by using `PartitionScheme=ota_1_9mb` in the board FQBN (see `esp32c3_ota_1_9mb` above)
- Installed automatically by:
  - Local dev: `./setup.sh`
  - CI/CD: GitHub Actions workflows (build + release)
- Manual (if you changed core versions): `./tools/install-custom-partitions.sh`

**Important:** the first flash after changing the partition table should be done over serial (USB). OTA updates will work normally afterwards.

**Adding/Modifying Boards:**

1. Add/modify entries in `FQBN_TARGETS` (prefer doing this in `config.project.sh` for template-based projects)
2. Provide custom board name or leave empty to auto-extract from FQBN
3. Build outputs go to `build/<board-name>/` directory

**Board Override Macros & Includes:**
- If `src/boards/<board>/` exists, `build.sh` adds it to the include path and defines:
  - `BOARD_<BOARDNAME>` (uppercased, e.g., `BOARD_CYD_V2`)
  - `BOARD_HAS_OVERRIDE` (allows `src/app/board_config.h` to include `board_overrides.h`)
- These flags are applied to both C++ and C compilation units (LVGL is built as C), so LVGL config can also react to board overrides.
- No changes needed in `app.ino`; overrides are pulled automatically.

**Examples:**
```bash
# Build all boards
./build.sh

# Build specific board
./build.sh esp32-nodisplay
./build.sh cyd-v2

# Upload to specific board
./upload.sh esp32-nodisplay
./upload.sh cyd-v2 /dev/ttyACM0  # With explicit port
```

### Board-Specific Configuration

The project uses a flexible board configuration system for hardware-specific customization using compile-time defines and conditional compilation.

**How It Works:**

1. **Board Overrides Define Hardware**: Create `src/boards/[board-name]/board_overrides.h` to specify board capabilities
2. **Application Uses Conditional Compilation**: Use `#if HAS_xxx` in `app.ino` to compile board-specific code
3. **Compiler Eliminates Unused Code**: Zero runtime overhead - only relevant code is compiled per board

**Example: Board with Built-in LED**

```bash
# Create board-specific configuration
mkdir -p src/boards/esp32-nodisplay
cat > src/boards/esp32-nodisplay/board_overrides.h << 'EOF'
#ifndef BOARD_OVERRIDES_ESP32_H
#define BOARD_OVERRIDES_ESP32_H

#define HAS_BUILTIN_LED true
#define LED_PIN 2
#define LED_ACTIVE_HIGH true

#endif
EOF
```

**Application Code (works for all boards):**

```cpp
// src/app/app.ino
#include "board_config.h"

void setup() {
  #if HAS_BUILTIN_LED
  // Only compiled for boards with LED
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LED_ACTIVE_HIGH ? HIGH : LOW);
  Serial.println("LED initialized");
  #else
  Serial.println("No LED on this board");
  #endif
}
```

**Common Use Cases:**

- **Different GPIO pins**: LEDs, buttons, sensors on different pins per board
- **Optional hardware**: Battery monitors, displays, sensors present on some boards only
- **Different drivers**: Same peripheral (e.g., display) using different driver chips

**Defaults**: Boards without overrides use defaults from `src/app/board_config.h`. See that file for available options and usage examples.

**Build System**: Automatically detects board override directories and sets `-DBOARD_HAS_OVERRIDE=1` during compilation for both C++ and C sources.

### Serial Port

Scripts auto-detect `/dev/ttyUSB0` or `/dev/ttyACM0`. Manually specify if needed:

```bash
./upload.sh esp32 /dev/ttyUSB1
./monitor.sh /dev/ttyACM0 115200
```

## 🖥️ WSL2 Development

For Windows users with WSL2, USB passthrough is required:

```powershell
# Windows PowerShell (Administrator)
usbipd list
usbipd bind --busid 1-4
usbipd attach --wsl --busid 1-4
```

```bash
# WSL Terminal
sudo usermod -a -G dialout $USER
# Restart WSL: wsl --terminate Ubuntu (in PowerShell)
```

See [docs/wsl-development.md](docs/wsl-development.md) for complete guide.

## 📚 Library Management

Libraries are managed via `arduino-libraries.txt` for consistency across local and CI environments.

### Quick Commands

```bash
./library.sh search mqtt          # Find libraries
./library.sh add PubSubClient     # Add and install
./library.sh list                 # Show configured libraries
./library.sh installed            # Show installed libraries
```

Libraries in `arduino-libraries.txt` are automatically installed by `setup.sh` and in GitHub Actions.

**Note:** The template ships with a small set of libraries already configured; add or remove entries in `arduino-libraries.txt` as needed for your project.

## 🏠 Home Assistant + MQTT

When `HAS_MQTT` is enabled, the firmware can publish MQTT state and Home Assistant MQTT Discovery.

- Developer guide: [docs/home-assistant-mqtt.md](docs/home-assistant-mqtt.md)

See [docs/library-management.md](docs/library-management.md) for detailed guide.

## 🔍 Version Management

Firmware versions are tracked in `src/version.h`:

```cpp
#define VERSION_MAJOR 1
#define VERSION_MINOR 0
#define VERSION_PATCH 0
```

Version is automatically displayed at startup. Update before releases.

## 🧪 Testing & CI/CD

GitHub Actions automatically validates builds on push:

- Compiles firmware for all configured board variants (matrix build)
- Caches arduino-cli and libraries for faster builds
- Uploads separate build artifacts per board (.bin and .elf files)
- Generates build summary with firmware sizes

## 🌍 Web Firmware Installer (ESP Web Tools)

This repo includes a GitHub Pages installer site powered by ESP Web Tools, so users can flash firmware from the browser (no backend required).

- Site generator: [tools/build-esp-web-tools-site.sh](tools/build-esp-web-tools-site.sh)

### How it works

- The release workflow builds firmware for all configured boards.
- It attaches app-only and the **multi-part** flashing binaries (bootloader/partitions/boot_app0) to the GitHub Release.
- For **stable releases only** (non-prerelease), a separate Pages workflow runs when the release is published and deploys the installer site to GitHub Pages.
- Pages deployment is **latest-only**: each stable release overwrites what’s hosted.

### Enable + Deploy

1. In the GitHub repo settings, go to **Settings → Pages**.
2. Set **Build and deployment → Source** to **GitHub Actions**.
3. Push a stable tag (e.g. `vX.Y.Z`) to run the release workflow.

After the run finishes, the installer will be available at:

- `https://<owner>.github.io/<repo>/`

### Notes

- This works without CORS issues because the page, manifest, and firmware `.bin` files are all served from the same GitHub Pages origin.
- The installer uses multi-part flashing: bootloader @ `0x0`, partition table @ `0x8000`, `boot_app0` @ `0xE000`, and app @ the `app0` offset derived from `app.ino.partitions.bin`.
- This avoids accidentally overwriting NVS on custom partition layouts.
- Boards with names containing `beta` or `experimental` are excluded from the hosted installer list.

Separately from the browser installer, the device web portal links to the **GitHub Pages OTA updater** (Firmware page → “Online Update (GitHub Pages)”). The updater reads `ota/<board>.json` from Pages and calls the device API with the firmware URL.

## 🚀 Release Process

Releases are automated via GitHub Actions when version tags are pushed.

### Creating a Release

#### Option 1: Automated Script (Recommended)

```bash
# Run the release preparation script
./create-release.sh 0.0.5 "Improved logging and stability"

# This will:
# 1. Create release/v0.0.5 branch
# 2. Update src/version.h with version numbers
# 3. Update CHANGELOG.md (move [Unreleased] to [0.0.5])
# 4. Commit and push changes
# 5. Provide PR creation URL

# After PR is merged:
git checkout main
git pull
git tag -a v0.0.5 -m "Release v0.0.5"
git push origin v0.0.5
```

#### Option 2: Manual Process

```bash
# 1. Create release branch
git checkout -b release/v0.0.5

# 2. Update src/version.h
#    VERSION_MAJOR 0
#    VERSION_MINOR 0
#    VERSION_PATCH 5

# 3. Update CHANGELOG.md
#    Move [Unreleased] items to [0.0.5] - YYYY-MM-DD

# 4. Commit and push
git add src/version.h CHANGELOG.md
git commit -m "chore: bump version to 0.0.5"
git push origin release/v0.0.5

# 5. Create and merge PR: release/v0.0.5 → main

# 6. Tag the release
git checkout main
git pull
git tag -a v0.0.5 -m "Release v0.0.5"
git push origin v0.0.5
```

### What Happens Automatically

When you push a tag matching `v*.*.*`:

1. **GitHub Actions triggers** `.github/workflows/release.yml`
2. **Builds firmware** for all board variants
3. **Creates GitHub Release** with:
   - Release notes extracted from CHANGELOG.md
  - Firmware binaries: `$PROJECT_NAME-<board>-vX.Y.Z.bin` (app-only)
  - Firmware binaries: `$PROJECT_NAME-<board>-vX.Y.Z-bootloader.bin`
  - Firmware binaries: `$PROJECT_NAME-<board>-vX.Y.Z-partitions.bin`
  - Firmware binaries: `$PROJECT_NAME-<board>-vX.Y.Z-boot_app0.bin`
  - Firmware binaries: `$PROJECT_NAME-<board>-vX.Y.Z-merged.bin` (legacy; may overwrite NVS)
   - SHA256 checksums: `SHA256SUMS.txt`
4. **Marks as pre-release** if version contains hyphen (e.g., `v1.0.0-beta.1`)

For stable releases, after the GitHub Release is published, `.github/workflows/pages-from-release.yml` deploys the GitHub Pages installer.

### Release Artifacts

Each release includes firmware binaries for all board variants:
- `$PROJECT_NAME-esp32-nodisplay-v0.0.5.bin` (app-only)
- `$PROJECT_NAME-esp32-nodisplay-v0.0.5-bootloader.bin`
- `$PROJECT_NAME-esp32-nodisplay-v0.0.5-partitions.bin`
- `$PROJECT_NAME-esp32-nodisplay-v0.0.5-boot_app0.bin`
- `$PROJECT_NAME-esp32-nodisplay-v0.0.5-merged.bin` (legacy; flashes at offset 0)
- `SHA256SUMS.txt` - Checksums for verification

Debug symbols (`.elf`) and build metadata (`.txt`) are available in the workflow artifacts but not included in releases to keep downloads lightweight.

### Pre-Release Testing

Test release workflow with pre-release tags:

```bash
git tag -a v0.0.5-beta.1 -m "Pre-release v0.0.5-beta.1"
git push origin v0.0.5-beta.1
```

Pre-release tags automatically mark the release as "Pre-release" on GitHub.

## 🐛 Troubleshooting

### Permission Denied on Serial Port

```bash
sudo usermod -a -G dialout $USER
# Logout and login, or restart WSL
```

### arduino-cli Not Found

```bash
./setup.sh  # Reinstall arduino-cli
```

### Build Directory Not Found

```bash
./build.sh  # Build before uploading
```

### Device Not Detected

```bash
# Check if device is connected
ls -l /dev/ttyUSB* /dev/ttyACM*

# For WSL, check Windows PowerShell
usbipd list
```

## 📖 Documentation

- [Web Portal Guide](docs/web-portal.md) - Configuration portal & REST API
- [Script Reference](docs/scripts.md) - Detailed script usage
- [Build and Release Process](docs/build-and-release-process.md) - Project branding, build system, and release workflow
- [WSL Development Guide](docs/wsl-development.md) - Windows/WSL setup
- [Library Management](docs/library-management.md) - Managing dependencies
- [Home Assistant + MQTT](docs/home-assistant-mqtt.md) - MQTT topics, HA discovery, and how to extend
- [Changelog](CHANGELOG.md) - Version history and release notes

## 📄 License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

---

**Made with ❤️ for ESP32 developers**
