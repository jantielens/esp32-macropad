# Development Scripts

This project includes several bash scripts to streamline ESP32 development workflow.

## config.sh

**Purpose:** Common configuration and helper functions used by all scripts.

**What it contains:**
- Project configuration (`PROJECT_NAME`, `SKETCH_PATH`, `BUILD_PATH`)
- Board targets (`FQBN_TARGETS` associative array)
- Optionally sources `config.project.sh` for project-specific overrides (recommended for template-based projects)
- `find_arduino_cli()` - Locates arduino-cli (local or system-wide)
- `find_serial_port()` - Auto-detects `/dev/ttyUSB*` or `/dev/ttyACM*`
- `get_board_name()` - Returns board name (identity function for compatibility)
- `list_boards()` - Lists all configured boards
- `get_fqbn_for_board()` - Gets FQBN for a board name

**Usage:** Automatically sourced by other scripts. Do not run directly.

**Multi-Board Configuration:**
```bash
declare -A FQBN_TARGETS=(
  ["esp32-nodisplay"]="esp32:esp32:esp32"  # ESP32 Dev Module (no display)
  ["esp32c3_ota_1_9mb"]="esp32:esp32:nologo_esp32c3_super_mini:CDCOnBoot=cdc,PartitionScheme=ota_1_9mb"  # ESP32-C3 w/ custom partitions
  ["cyd-v2"]="esp32:esp32:esp32"  # CYD v2 (same FQBN, different board_overrides.h)
)
```

**Note:** This file centralizes common code to avoid duplication across scripts.

---

## setup.sh

**Purpose:** Install and configure the ESP32 development environment.

**Usage:**
```bash
./setup.sh
```

**What it does:**
- Downloads and installs `arduino-cli` to `./bin/`
- Configures ESP32 board support
- Installs ESP32 core platform
- Installs optional custom partition tables used by example board targets (if present)
- Installs libraries from `arduino-libraries.txt`
- Optionally installs Python Pillow (enables PNG→LVGL asset conversion during build)

**When to use:** Run once when setting up the project, or after a clean checkout.

---

## build.sh

**Purpose:** Compile the Arduino sketch into ESP32 firmware for one or more board variants.

**Usage:**
```bash
./build.sh              # Build all configured boards
./build.sh esp32-nodisplay        # Build only ESP32 Dev Module (no display)
./build.sh cyd-v2                 # Build CYD v2 display board
./build.sh esp32c3_ota_1_9mb  # Build ESP32-C3 using custom partitions (example)
BOARD_PROFILE=psram ./build.sh esp32-nodisplay  # Optional build profile (if defined in config.sh)
```

**What it does:**
- Optionally generates LVGL PNG assets from `assets/png/*.png` into `src/app/png_assets.cpp` + `src/app/png_assets.h` (only when building for at least one display-enabled board)
- Generates minified web assets (once for all builds)
  - Also generates `src/app/repo_slug_config.h` when `git remote origin` points at a GitHub repo (used to construct GitHub Pages links)
- Prints a per-board "Compile-time flags summary" (active `HAS_*` features + key selectors) to make it clear what the build will include
- Compiles `src/app/app.ino` for specified board(s)
- Creates board-specific directories: `./build/esp32-nodisplay/`, `./build/cyd-v2/`, etc.
- Generates `.bin`, `.bootloader.bin`, `.merged.bin`, and `.partitions.bin` files per board
- Detects build errors including:
  - Compilation failures (exit code ≠ 0)
  - Undefined symbol references (e.g., missing driver `.cpp` includes)
  - Common warning patterns that indicate problems
  - Provides helpful diagnostics for Arduino build system limitations
- If `src/boards/<board>/` exists, adds it to include path and defines:
    - `BOARD_<BOARDNAME>` - Board name sanitized to valid C++ macro (alphanumeric + underscore only)
      - Examples: `cyd-v2` → `BOARD_CYD_V2`, `esp32-4848S040` → `BOARD_ESP32_4848S040`
    - `BOARD_HAS_OVERRIDE` (triggers inclusion of `board_overrides.h`)
    - `BUILD_BOARD_NAME` (string literal; used to select the correct per-board app-only GitHub release asset for online updates)
    - Note: these flags are applied to both C++ and C compilation units (LVGL is built as C), so LVGL config can also react to board overrides.

**Board overrides and library builds:**

Some compile-time settings need to apply not only to the sketch, but also to separately-built Arduino libraries (which may compile as their own translation units). For a small allowlist of known-safe settings, `build.sh` propagates values found in `src/boards/<board>/board_overrides.h` into global compiler flags (C and C++).

- Currently allowlisted includes:
  - `CONFIG_ASYNC_TCP_STACK_SIZE` (AsyncTCP task stack size)
  - TFT_eSPI essentials needed for clean/CI builds (pins + SPI frequencies + controller/bus flags)
- Example (inside `src/boards/<board>/board_overrides.h`): `#define CONFIG_ASYNC_TCP_STACK_SIZE 16384`

**TFT_eSPI per-board setup (recommended for reproducibility):**

If a board depends on TFT_eSPI and needs a full TFT_eSPI `User_Setup.h` (or you want to avoid relying on a locally modified Arduino library install), you can add:

- `src/boards/<board>/User_Setup.h`

When present, `build.sh` will force-include that file for the board and set `USER_SETUP_LOADED=1` so TFT_eSPI skips its default setup.

**Build Output Structure:**
```
build/
├── esp32-nodisplay/
│   ├── app.ino.bin
│   ├── app.ino.bootloader.bin
│   ├── app.ino.merged.bin
│   └── app.ino.partitions.bin
└── cyd-v2/
    ├── app.ino.bin
    └── ...
```

**Requirements:** Must run `setup.sh` first.

---

## tools/png2lvgl_assets.py

**Purpose:** Convert top-level PNG files to LVGL 8.x `lv_img_dsc_t` symbols for use in the UI.

This is invoked automatically by `build.sh` when:
- `assets/png/` exists and contains at least one `*.png` at the top level, and
- you are building a display-enabled board (`#define HAS_DISPLAY true` in `src/boards/<board>/board_overrides.h`).

**Generated output:**
- `src/app/png_assets.h`
- `src/app/png_assets.cpp`

**Naming:** `assets/png/logo.png` → `img_logo`

**Manual usage:**
```bash
python3 tools/png2lvgl_assets.py assets/png src/app/png_assets.cpp src/app/png_assets.h --prefix img_
```

**Requirements:** Python 3 + Pillow (`python3 -m pip install --user pillow`).

---

## tools/generate-board-driver-table.py

**Purpose:** Generate a markdown table mapping **board → selected display/touch backends → basic hardware**.

**Source of truth:** `src/boards/<board>/board_overrides.h` (see the "Driver Selection (HAL)" block).

**Usage:**
```bash
# Print the table to stdout
python3 tools/generate-board-driver-table.py

# Update src/app/drivers/README.md between markers
python3 tools/generate-board-driver-table.py --update-drivers-readme

# Update an arbitrary markdown file (path relative to repo root)
python3 tools/generate-board-driver-table.py --update-file path/to/file.md
```

---

## tools/compile_flags_report.py

**Purpose:** Generate a compile-time flags report (flag list + per-board matrices + per-file preprocessor usage map) and print the active flags for a specific board.

**Outputs:**
- Documentation report: `docs/compile-time-flags.md`
- Build-time summary: printed to stdout

**Usage:**
```bash
# Update docs/compile-time-flags.md (updates marker sections in-place)
python3 tools/compile_flags_report.py md --out docs/compile-time-flags.md

# Print active flags for a board (useful for build logs)
python3 tools/compile_flags_report.py build --board cyd-v2
```

**CI enforcement:** GitHub Actions regenerates `docs/compile-time-flags.md` and fails if it differs, so changes to compile-time flags must be accompanied by a regenerated doc.

---

## tools/portal_stress_test.py

**Purpose:** Run repeatable stress scenarios against the web portal and REST API.

**Usage (examples):**
```bash
python3 tools/portal_stress_test.py --host 192.168.1.111 --no-reboot --cycles 10 --scenario api
python3 tools/portal_stress_test.py --host 192.168.1.111 --no-reboot --cycles 10 --scenario portal
```

**Notes:**
- Use `--no-reboot` when the device should remain up between cycles.

---

## tools/install-custom-partitions.sh

**Purpose:** Install/register template-provided custom partition tables into the Arduino ESP32 core.

Some FQBN options (like `PartitionScheme=ota_1_9mb`) only work if:
1. The partition CSV exists in the ESP32 core `tools/partitions/` directory, and
2. The scheme is registered in the ESP32 core `boards.txt`.

This script automates both steps.

**Usage:**
```bash
./tools/install-custom-partitions.sh
```

**When to use:**
- Automatically run by `./setup.sh`
- Run manually after upgrading the ESP32 Arduino core (the install path can change)

---

## upload.sh

**Purpose:** Upload compiled firmware to the ESP32 device.

**Usage:**

**Options:**
```bash
./upload.sh --full        # Flash bootloader + partitions + boot_app0 + app at the correct offsets (preserves NVS by default)
./upload.sh --app-only    # Flash only the app at the correct app offset
./upload.sh --merged      # Flash merged image at 0x0 (legacy; overwrites NVS on most layouts)
./upload.sh --baud 921600 # Set esptool baud rate for flashing (faster on many USB-UART adapters)
./upload.sh --erase-nvs   # Erase NVS only (WiFi/config reset)
./upload.sh --erase-flash # Erase entire flash (destructive)
```

**Single Board Configuration:**
```bash
./upload.sh              # Auto-detects port
./upload.sh /dev/ttyUSB0 # Specify port
```

**Multiple Boards Configuration:**
```bash
./upload.sh esp32-nodisplay              # Upload ESP32 build, auto-detect port
./upload.sh cyd-v2                       # Upload CYD v2 display build, auto-detect port
./upload.sh esp32c3_ota_1_9mb   # Upload ESP32-C3 build using custom partitions
./upload.sh esp32-nodisplay /dev/ttyUSB0 # Upload ESP32 build to specific port
```

**What it does:**
- Validates board name (required when multiple boards configured)
- Detects connected ESP32 boards
- Infers chip family from the FQBN board id (explicit `esp32*` token or a small heuristic) and prints the resolved chip
- Uploads firmware from board-specific `./build/<board>/` directory to the device
- Auto-detects serial port if not specified

**PartitionScheme note:**
- If the board FQBN includes `PartitionScheme=...`, `upload.sh` defaults to `--full` to ensure partition changes are applied and the app is flashed at the correct offset.
- Use `--merged` only if you explicitly want a merged-at-0x0 flash (destructive on most layouts).
- Use `--app-only` for faster updates when the partition layout is already correct.

**Requirements:** 
- Must run `build.sh [board]` first
- ESP32 device must be connected via USB
- User must be in `dialout` group (see [WSL Development Guide](wsl-development.md))

---

## monitor.sh

**Purpose:** Display serial output from the ESP32 device.

**Usage:**
```bash
./monitor.sh                  # Auto-detects port, 115200 baud
./monitor.sh /dev/ttyUSB0     # Custom port, default baud
./monitor.sh /dev/ttyUSB0 9600 # Custom port and baud rate
```

**What it does:**
- Opens serial monitor connection to ESP32
- Displays real-time output from `Serial.print()` statements
- Press `Ctrl+C` to exit

**Requirements:** 
- ESP32 device must be connected via USB
- User must be in `dialout` group

---

## clean.sh

**Purpose:** Remove build artifacts and temporary files.

**Usage:**
```bash
./clean.sh
```

**What it does:**
- Removes the `./build/` directory and all board subdirectories
- Lists board directories being removed
- Cleans up temporary files (*.tmp, *.bak, *~)
- Prepares for a fresh build

**When to use:** When you want to force a complete rebuild or clean up disk space.

---

## upload-erase.sh

**Purpose:** Completely erase the ESP32 flash memory.

**Usage:**

**Single Board Configuration:**
```bash
./upload-erase.sh              # Auto-detects port
./upload-erase.sh /dev/ttyUSB0 # Specify port
```

**Multiple Boards Configuration:**
```bash
./upload-erase.sh esp32-nodisplay              # Erase ESP32, auto-detect port
./upload-erase.sh cyd-v2                       # Erase CYD v2 display board, auto-detect port
./upload-erase.sh esp32-nodisplay /dev/ttyUSB0 # Erase ESP32 on specific port
```

**What it does:**
- Validates board name (required when multiple boards configured)
- Infers chip family from the FQBN board id (explicit `esp32*` token or a small heuristic)
- Uses esptool.py to completely erase ESP32 flash
- Prompts for confirmation before erasing
- Auto-detects serial port if not specified

**Requirements:**
- ESP32 device must be connected via USB
- User must be in `dialout` group

**Warning:** This erases ALL data including firmware, WiFi credentials, and stored settings.

---

## library.sh

**Purpose:** Manage Arduino libraries for the project.

**Usage:**
```bash
./library.sh search <keyword>    # Search for libraries
./library.sh add <library>       # Add and install library
./library.sh remove <library>    # Remove from config
./library.sh list                # Show configured libraries
./library.sh install             # Install all from config
./library.sh installed           # Show currently installed
```

**What it does:**
- Searches arduino-cli library index
- Adds libraries to `arduino-libraries.txt` and installs them
- Removes libraries from configuration
- Lists and manages project dependencies

**Requirements:** Must run `setup.sh` first.

**See also:** [Library Management Guide](library-management.md) for detailed documentation.

---

## Typical Workflow

**Single Board Project:**
```bash
# Initial setup (one time)
./setup.sh

# Add libraries as needed
./library.sh search bme280
./library.sh add "Adafruit BME280 Library"

# Development cycle
./build.sh              # Compile firmware
./upload.sh             # Upload to device
./monitor.sh            # View serial output

# Or use convenience scripts
./bum.sh                # Build + Upload + Monitor
./um.sh                 # Upload + Monitor

# You can pass upload.sh options through
./um.sh --baud 921600    # Faster flashing (if supported)

# Clean build when needed
./clean.sh
./build.sh

# Complete flash erase (when needed)
./upload-erase.sh
```

**Multi-Board Project:**
```bash
# Initial setup (one time)
./setup.sh

# Build all boards
./build.sh

# Or build specific board
./build.sh esp32-nodisplay
./build.sh cyd-v2
./build.sh esp32c3_ota_1_9mb

# Upload to specific board
./upload.sh esp32-nodisplay       # Auto-detects port
./upload.sh cyd-v2                # Auto-detects port
./upload.sh esp32c3_ota_1_9mb

# Full cycle for specific board
./bum.sh esp32-nodisplay          # Build + Upload + Monitor
./um.sh cyd-v2                    # Upload + Monitor

# With upload options
./um.sh --baud 921600 cyd-v2                  # Upload + Monitor at custom baud

# Clean all board builds
./clean.sh
```

## Troubleshooting

**Permission denied on /dev/ttyUSB0:**
```bash
sudo usermod -a -G dialout $USER
# Then fully restart WSL: wsl --terminate Ubuntu (in PowerShell)
```

**arduino-cli not found:**
- Run `./setup.sh` first
- Scripts use local `./bin/arduino-cli`, not system PATH

**Build directory not found:**
- Run `./build.sh [board]` before `./upload.sh [board]`
- Ensure board name matches configured boards in `config.sh`

**Partition scheme not found / build fails with PartitionScheme:**
- Re-run `./setup.sh`, or run `./tools/install-custom-partitions.sh`
- First flash after changing partition tables should be done over serial (USB)

**Board name required error:**
- When multiple boards are configured, specify board name: `./upload.sh esp32-nodisplay`
- List available boards by checking `config.sh` or running with invalid board name

For WSL-specific setup, see [WSL Development Guide](wsl-development.md).
