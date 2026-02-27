#!/bin/bash

# ESP32 Template Project Configuration
# This file contains common configuration and helper functions used by all scripts
# Source this file at the beginning of each script

# Get script directory (works when sourced)
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# ============================================================================
# PROJECT BRANDING CONFIGURATION
# ============================================================================
# These values define your project's identity across the entire system.
# Change these to customize your project name and branding.
#
# PROJECT_NAME (slug format - no spaces, lowercase with hyphens)
#   Used for:
#   - Build artifact filenames: {PROJECT_NAME}-{board}-v{version}.bin
#   - GitHub release files: {PROJECT_NAME}-esp32-v0.0.5.bin
#   - GitHub artifact names in CI/CD workflows
#   - WiFi Access Point SSID: {PROJECT_NAME}-1A2B3C4D (uppercase)
#   Example: "esp32-template-wifi" → "ESP32-TEMPLATE-WIFI-1A2B3C4D"
#
# PROJECT_DISPLAY_NAME (human-readable format)
#   Used for:
#   - Web portal page title: "{PROJECT_DISPLAY_NAME} Configuration Portal"
#   - Web portal header: "{PROJECT_DISPLAY_NAME} Configuration"
#   - Default device name: "{PROJECT_DISPLAY_NAME} 1A2B"
#   - Logs page title: "Device Logs - {PROJECT_DISPLAY_NAME}"
#   - REST API /api/info response (both values included)
#   Example: "ESP32 Template"
#
PROJECT_NAME="esp32-template"
PROJECT_DISPLAY_NAME="ESP32 Template"

# Board configuration (FQBN - Fully Qualified Board Name)
# Define target boards as an associative array: ["board-name"]="FQBN"
# - Board name is the key (used for directories, script arguments, CI/CD matrix)
# - FQBN is the value (used by arduino-cli for compilation)
# - Multiple board names can share the same FQBN (different board_overrides.h)
#
# USB Serial Support (CDCOnBoot):
# - ESP32-C3, C6, S3: Add `:CDCOnBoot=cdc` to enable USB serial output
# - ESP32 (classic): No USB-OTG, uses hardware UART - no CDC parameter needed
# - To check if a board needs CDC: `arduino-cli board details <FQBN> | grep CDCOnBoot`

# Custom Partition Schemes (PartitionScheme):
# Some ESP32 boards (notably ESP32-C3 “Super Mini” variants) can run out of flash
# space for OTA-enabled firmware as projects grow.
#
# Examples:
#   ["esp32-nodisplay"]="esp32:esp32:esp32"                                       # Classic ESP32 dev module (no display)
#   ["esp32c3_ota_1_9mb"]="esp32:esp32:nologo_esp32c3_super_mini:CDCOnBoot=cdc,PartitionScheme=ota_1_9mb"    # ESP32-C3 w/ custom partitions (example)
#   ["esp32c6"]="esp32:esp32:dfrobot_firebeetle2_esp32c6:CDCOnBoot=cdc"           # ESP32-C6 (USB CDC)
#   ["cyd-v2"]="esp32:esp32:esp32"                                                # CYD display v2 (same FQBN as classic ESP32)

declare -A FQBN_TARGETS=(
    ["esp32-nodisplay"]="esp32:esp32:esp32" # Classic ESP32 dev module (no display)
    ["cyd-v2"]="esp32:esp32:esp32:PartitionScheme=min_spiffs" # CYD v2 display (ESP32 + display; minimal spiffs)
    ["esp32-4848S040"]="esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi,PartitionScheme=app3M_fat9M_16MB,CDCOnBoot=default" # ESP32-S3-4848S040 (480x480 ST7701 RGB + GT911 touch; 16MB + OPI PSRAM; hardware UART)
    ["jc3248w535"]="esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi,PartitionScheme=app3M_fat9M_16MB,USBMode=hwcdc,CDCOnBoot=cdc" # ESP32-S3 JC3248W535 (16MB + OPI PSRAM)
    ["jc3636w518"]="esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi,PartitionScheme=app3M_fat9M_16MB,USBMode=hwcdc,CDCOnBoot=cdc" # ESP32-S3 JC3636W518 (16MB + OPI PSRAM)
    ["esp32c3-withsensors"]="esp32:esp32:nologo_esp32c3_super_mini:CDCOnBoot=cdc,PartitionScheme=ota_1_9mb" # ESP32-C3 Super Mini + sensors sample (OTA partitions)
    ["esp32-p4-lcd4b"]="esp32:esp32:esp32p4:FlashSize=32M,PSRAM=enabled,PartitionScheme=ota_8mb_32MB" # ESP32-P4 Waveshare WIFI6-Touch-LCD-4B (720x720 MIPI-DSI + GT911 touch; 32MB + 32MB PSRAM)
    ["jc4880p433"]="esp32:esp32:esp32p4:FlashSize=16M,PSRAM=enabled,PartitionScheme=ota_6mb_16MB,USBMode=hwcdc,CDCOnBoot=cdc" # ESP32-P4 GUITION JC4880P433 (480x800 MIPI-DSI ST7701 + GT911 touch; 16MB + 32MB PSRAM)
)

# Default board (used when only one board is configured)
DEFAULT_BOARD=""

# ----------------------------------------------------------------------------
# Optional project-specific overrides
# ----------------------------------------------------------------------------
# To make it easier to merge upstream template changes into downstream projects,
# projects can keep their branding and board list in a separate file.
#
# If present, this file is sourced AFTER defaults above, so it can override:
#   - PROJECT_NAME / PROJECT_DISPLAY_NAME
#   - DEFAULT_BOARD
#   - FQBN_TARGETS (redeclare the associative array)
#
# Recommended filename: config.project.sh (commit it in the project repo).

PROJECT_CONFIG_FILE="$SCRIPT_DIR/config.project.sh"
if [[ -f "$PROJECT_CONFIG_FILE" ]]; then
    source "$PROJECT_CONFIG_FILE"
fi

# Color definitions for terminal output
BLUE='\033[0;34m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# Project paths
SKETCH_PATH="$SCRIPT_DIR/src/app/app.ino"
BUILD_PATH="$SCRIPT_DIR/build"

# Find arduino-cli executable
# Checks for local installation first, then falls back to system-wide
find_arduino_cli() {
    # Allow explicit override
    if [[ -n "${ARDUINO_CLI:-}" ]]; then
        if command -v "$ARDUINO_CLI" >/dev/null 2>&1; then
            command -v "$ARDUINO_CLI"
            return 0
        fi

        if [[ -x "$ARDUINO_CLI" ]]; then
            echo "$ARDUINO_CLI"
            return 0
        fi
    fi

    # Prefer local toolchain installation
    if [[ -x "$SCRIPT_DIR/bin/arduino-cli" ]]; then
        echo "$SCRIPT_DIR/bin/arduino-cli"
        return 0
    fi

    # Global install (PATH)
    if command -v arduino-cli >/dev/null 2>&1; then
        command -v arduino-cli
        return 0
    fi

    # Common global locations (covers some distros/snap)
    for candidate in \
        /usr/local/bin/arduino-cli \
        /usr/bin/arduino-cli \
        /snap/bin/arduino-cli \
        "$HOME/.local/bin/arduino-cli"; do
        if [[ -x "$candidate" ]]; then
            echo "$candidate"
            return 0
        fi
    done

    echo "Error: arduino-cli is not found" >&2
    echo "Tried: \"$SCRIPT_DIR/bin/arduino-cli\", PATH lookup, and common locations" >&2
    echo "PATH=\"$PATH\"" >&2
    echo "Please run ./setup.sh or install arduino-cli system-wide" >&2
    exit 1
}

# Auto-detect serial port
# Prefers /dev/ttyUSB* (UART adapters) then /dev/ttyACM* (USB CDC)
# Returns exit code 1 if no port found
find_serial_port() {
    local port

    # Prefer ttyUSB devices first.
    port=$(ls -1 /dev/ttyUSB* 2>/dev/null | sort -V | head -n 1 || true)
    if [[ -n "$port" ]]; then
        echo "$port"
        return 0
    fi

    # Then fall back to ttyACM devices.
    port=$(ls -1 /dev/ttyACM* 2>/dev/null | sort -V | head -n 1 || true)
    if [[ -n "$port" ]]; then
        echo "$port"
        return 0
    fi

    return 1
}

# Infer ESP32 chip family from FQBN board segment (3rd segment).
# Prefer explicit esp32* tokens, otherwise fall back to readable heuristics.
get_chip_type_from_fqbn() {
    local fqbn="$1"

    local board_seg
    board_seg=$(echo "$fqbn" | cut -d':' -f3)
    if [[ -z "$board_seg" ]]; then
        echo "esp32"
        return 0
    fi

    local chip
    chip=$(echo "$board_seg" | grep -oE 'esp32[a-z0-9]*' | head -n 1 || true)
    if [[ -n "$chip" ]]; then
        echo "$chip"
        return 0
    fi

    case "$board_seg" in
        *s3*) echo "esp32s3" ;;
        *s2*) echo "esp32s2" ;;
        *c6*) echo "esp32c6" ;;
        *c3*) echo "esp32c3" ;;
        *c2*) echo "esp32c2" ;;
        *h2*) echo "esp32h2" ;;
        *) echo "esp32" ;;
    esac
}

# Get board name (identity function now - board names are the keys)
# Kept for backward compatibility with existing code
get_board_name() {
    echo "$1"
}

# List all configured boards
list_boards() {
    echo -e "${CYAN}Available boards:${NC}"
    for board_name in "${!FQBN_TARGETS[@]}"; do
        local fqbn="${FQBN_TARGETS[$board_name]}"
        echo -e "  ${GREEN}$board_name${NC} → $fqbn"
    done
}

# Get FQBN for a given board name
get_fqbn_for_board() {
    local target_board="$1"
    local fqbn="${FQBN_TARGETS[$target_board]}"
    if [[ -n "$fqbn" ]]; then
        echo "$fqbn"
        return 0
    fi
    return 1
}

# Parse board and port arguments for scripts that need them
# Usage: parse_board_and_port_args "$@"
# Sets global variables: BOARD, PORT, FQBN
# Behavior:
#   - Multiple boards: 1st arg = board (required), 2nd arg = port (optional)
#   - Single board: accepts either
#       * 1st arg = PORT (legacy behavior)
#       * or 1st arg = BOARD, 2nd arg = PORT (friendly/explicit form)
#   - Exits with error if board name is required but not provided
parse_board_and_port_args() {
    local board_count="${#FQBN_TARGETS[@]}"
    
    if [[ $board_count -gt 1 ]]; then
        # Multiple boards: first arg is board name, second is optional port
        if [[ -z "$1" ]]; then
            echo -e "${RED}Error: Board name required when multiple boards are configured${NC}"
            echo ""
            list_boards
            echo ""
            echo "Usage: ${0##*/} <board-name> [port]"
            exit 1
        fi
        BOARD="$1"
        PORT="$2"
    else
        # Single board: allow either explicit BOARD (arg1) or PORT (arg1)
        local arg1="$1"
        local arg2="$2"
        local default_board="${!FQBN_TARGETS[@]}"

        if [[ -n "$arg1" ]] && get_fqbn_for_board "$arg1" >/dev/null 2>&1; then
            # Explicit board name provided even though only one board exists
            BOARD="$arg1"
            PORT="$arg2"
        else
            # Legacy behavior: arg1 is treated as port (or empty)
            BOARD="$default_board"
            PORT="$arg1"
        fi
    fi
    
    # Get FQBN for the board
    FQBN=$(get_fqbn_for_board "$BOARD") || true
    if [[ -z "$FQBN" ]]; then
        echo -e "${RED}Error: Board '$BOARD' not found${NC}"
        echo ""
        list_boards
        exit 1
    fi
}
