#!/bin/bash

# ESP32 Development Environment Setup Script
# This script downloads and configures arduino-cli with ESP32 board support

set -e

echo "=== ESP32 Development Environment Setup ==="

# Verify host-side dependencies that the release tooling expects.
# `jq` is required by tools/build-esp-web-tools-site.sh to read per-board
# src/boards/<board>/metadata.json. Without it `create-release.sh` and the
# GitHub Pages flash-page deploy will fail with a hard error.
if ! command -v jq &> /dev/null; then
    echo "Installing jq (required by tools/build-esp-web-tools-site.sh)..."
    if command -v apt-get &> /dev/null; then
        sudo apt-get update -y && sudo apt-get install -y jq
    elif command -v brew &> /dev/null; then
        brew install jq
    else
        echo "WARNING: jq is not installed and no supported package manager (apt-get, brew) was found." >&2
        echo "         Install jq manually before running tools/build-esp-web-tools-site.sh." >&2
    fi
else
    echo "jq is already installed"
fi

# Configure the 'ours' merge driver used by .gitattributes for auto-generated files.
git config merge.ours.driver true

# Download and install arduino-cli if not present
if ! command -v arduino-cli &> /dev/null; then
    echo "Installing arduino-cli..."
    curl -fsSL https://raw.githubusercontent.com/arduino/arduino-cli/master/install.sh | sh
    
    # Add to PATH for current session
    export PATH=$PATH:$PWD/bin
    echo "arduino-cli installed successfully"
else
    echo "arduino-cli is already installed"
fi

# Initialize arduino-cli configuration
echo "Initializing arduino-cli configuration..."
arduino-cli config init --overwrite

# Add ESP32 board manager URL
echo "Adding ESP32 board manager URL..."
arduino-cli config add board_manager.additional_urls https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json

# Add Soldered (Inkplate) board manager URL — required for Inkplate-class boards.
# Harmless on systems that never build Inkplate; arduino-cli accepts multiple URLs.
echo "Adding Soldered (Inkplate) board manager URL..."
arduino-cli config add board_manager.additional_urls https://raw.githubusercontent.com/SolderedElectronics/Dasduino-Board-Definitions-for-Arduino-IDE/master/package_Dasduino_Boards_index.json

# Update board index
echo "Updating board index..."
arduino-cli core update-index

# Update library index
echo "Updating library index..."
arduino-cli lib update-index

# Install ESP32 board support
ESP32_CORE_VERSION="3.3.7"
echo "Installing ESP32 board support (esp32:esp32@${ESP32_CORE_VERSION})..."
arduino-cli core install "esp32:esp32@${ESP32_CORE_VERSION}"

# Install Soldered (Inkplate) board support. Required for the Inkplate 5V2
# target; bundles the InkplateLibrary sources used by HAS_EPAPER builds.
# Skipped silently if no Inkplate board is present in FQBN_TARGETS.
SCRIPT_DIR_EARLY="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
if grep -q '"Inkplate_Boards' "$SCRIPT_DIR_EARLY/config.sh" \
        || ([[ -f "$SCRIPT_DIR_EARLY/config.project.sh" ]] && grep -q '"Inkplate_Boards' "$SCRIPT_DIR_EARLY/config.project.sh"); then
    echo "Installing Soldered (Inkplate) board support (Inkplate_Boards:esp32)..."
    arduino-cli core install "Inkplate_Boards:esp32"
    echo "Installing InkplateLibrary..."
    arduino-cli lib install "InkplateLibrary"
fi

# Install Seeed_GxEPD2 for the reTerminal E1003 e-paper target. This library is
# NOT in the Arduino registry (only upstream GxEPD2, which lacks IT8951
# support), so it is installed from git. Skipped silently if no reTerminal
# board is present in FQBN_TARGETS.
if grep -q '"reterminal-e1003"' "$SCRIPT_DIR_EARLY/config.sh" \
        || ([[ -f "$SCRIPT_DIR_EARLY/config.project.sh" ]] && grep -q '"reterminal-e1003"' "$SCRIPT_DIR_EARLY/config.project.sh"); then
    echo "Installing Seeed_GxEPD2 (git; reTerminal E1003 IT8951 support)..."
    # git-url installs require the unsafe-install flag.
    arduino-cli config set library.enable_unsafe_install true
    arduino-cli lib install --git-url https://github.com/Seeed-Projects/Seeed_GxEPD2.git \
        || echo "Warning: Seeed_GxEPD2 install failed (reterminal-e1003 builds will fail until installed)"
fi

# Install/register template-provided custom partition schemes.
#
# This is required for any board FQBN using `PartitionScheme=...`.
# Safe to run multiple times (idempotent).
if [ -f "./tools/install-custom-partitions.sh" ]; then
    echo "Installing custom partition schemes (optional)..."
    chmod +x ./tools/install-custom-partitions.sh
    ./tools/install-custom-partitions.sh || echo "Warning: custom partition install failed (builds using PartitionScheme may fail)"
else
    echo "Note: tools/install-custom-partitions.sh not found; skipping custom partitions"
fi

# Install libraries from arduino-libraries.txt
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LIBRARIES_FILE="$SCRIPT_DIR/arduino-libraries.txt"

if [ -f "$LIBRARIES_FILE" ]; then
    echo "Installing Arduino libraries from $LIBRARIES_FILE..."
    while IFS= read -r line || [ -n "$line" ]; do
        # Skip empty lines and comments
        if [[ -z "$line" || "$line" =~ ^[[:space:]]*# ]]; then
            continue
        fi
        
        # Trim whitespace
        library=$(echo "$line" | xargs)
        
        if [ -n "$library" ]; then
            echo "  - Installing: $library"
            if ! arduino-cli lib install "$library"; then
                echo "Error: Failed to install $library" >&2
                exit 1
            fi
        fi
    done < "$LIBRARIES_FILE"
    echo "Library installation complete"
else
    echo "Warning: $LIBRARIES_FILE not found. No additional libraries will be installed."
fi

echo ""
echo "=== Setup Complete ==="
echo "ESP32 development environment is ready!"
echo ""

# Optional: PNG asset generation dependency (Pillow)
# Used by tools/png2lvgl_assets.py when converting assets/png/*.png to LVGL C arrays.
if command -v python3 >/dev/null 2>&1; then
    if python3 -c "import PIL" >/dev/null 2>&1; then
        echo "Python Pillow already available (PNG asset conversion enabled)."
    else
        echo "Installing Python Pillow (optional; enables PNG asset conversion)..."
        python3 -m pip install --user pillow || echo "Warning: Failed to install Pillow. PNG conversion will be skipped/fail until installed."
    fi
else
    echo "Note: python3 not found; PNG asset conversion tool will be unavailable."
fi

echo "To verify installation:"
echo "  arduino-cli board listall esp32"
