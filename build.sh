#!/bin/bash

# ESP32 Build Script
# Compiles the Arduino sketch to board-specific build directories
# Usage: ./build.sh [board-name]
#   - No parameter: Build all configured boards
#   - With board name: Build only that specific board

set -e

# Load common configuration and functions
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/config.sh"

# Get arduino-cli path
ARDUINO_CLI=$(find_arduino_cli)

# Parse command line arguments
TARGET_BOARD="$1"
# Optional board profile for build properties (e.g., PSRAM/flash options)
PROFILE="${BOARD_PROFILE:-${PROFILE:-}}"

COMPILE_FLAGS_DOC="$SCRIPT_DIR/docs/compile-time-flags.md"
COMPILE_FLAGS_DOC_HASH=""
if [[ -f "$COMPILE_FLAGS_DOC" ]]; then
    COMPILE_FLAGS_DOC_HASH=$(python3 - "$COMPILE_FLAGS_DOC" <<'PY'
import hashlib
from pathlib import Path
import sys

path = Path(sys.argv[1])
data = path.read_bytes()
print(hashlib.sha256(data).hexdigest())
PY
    )
fi

board_has_display() {
    local board_name="$1"
    local overrides_file="$SCRIPT_DIR/src/boards/$board_name/board_overrides.h"

    # Default config has HAS_DISPLAY=false; require explicit override.
    if [[ ! -f "$overrides_file" ]]; then
        return 1
    fi

    # Match: #define HAS_DISPLAY true (allow whitespace)
    if grep -qE '^[[:space:]]*#define[[:space:]]+HAS_DISPLAY[[:space:]]+true[[:space:]]*$' "$overrides_file"; then
        return 0
    fi

    return 1
}

should_generate_png_assets() {
    local target_board="$1"

    # Only run if assets folder exists.
    if [[ ! -d "$SCRIPT_DIR/assets/png" ]]; then
        return 1
    fi

    # Only run if there is at least one PNG file at the top level.
    if ! find "$SCRIPT_DIR/assets/png" -maxdepth 1 -type f \( -iname '*.png' \) -print -quit | grep -q .; then
        return 1
    fi

    if [[ -n "$target_board" ]]; then
        board_has_display "$target_board"
        return $?
    fi

    # Building all boards: generate if ANY configured board has a display.
    for board_name in "${!FQBN_TARGETS[@]}"; do
        if board_has_display "$board_name"; then
            return 0
        fi
    done

    return 1
}

# Build a single board
build_board() {
    local fqbn="$1"
    local board_name="$2"
    local board_build_path="$BUILD_PATH/$board_name"
    local board_override_dir="$SCRIPT_DIR/src/boards/$board_name"
    local board_overrides_file="$board_override_dir/board_overrides.h"
    
    echo -e "${CYAN}=== Building for $board_name ===${NC}"
    echo "Board:     $board_name"
    echo "FQBN:      $fqbn"
    echo "Output:    $board_build_path"
    
    # Check for board-specific configuration overrides
    EXTRA_FLAGS=()

    # Some Arduino libraries are compiled as separate translation units and do not
    # see macros defined only in board_overrides.h.
    # Keep this allowlisted to avoid arbitrary #define injection.
    EXTRA_GLOBAL_DEFINES=""
    EXTRA_GLOBAL_FLAGS=""
    if [[ -f "$board_overrides_file" ]]; then
        # Numeric-valued defines (exported as -DNAME=value)
        # NOTE: Keep this allowlist tight; only export values needed by libraries.
        numeric_allowlist=(
            CONFIG_ASYNC_TCP_STACK_SIZE

            # TFT_eSPI pinout and SPI frequencies
            TFT_MISO
            TFT_MOSI
            TFT_SCLK
            TFT_CS
            TFT_DC
            TFT_RST
            TFT_BL
            SPI_FREQUENCY
            SPI_READ_FREQUENCY
            SPI_TOUCH_FREQUENCY

            # NimBLE tuning (optional BLE builds)
            CONFIG_BT_NIMBLE_ROLE_PERIPHERAL
            CONFIG_BT_NIMBLE_ROLE_BROADCASTER
            CONFIG_BT_NIMBLE_ROLE_CENTRAL
            CONFIG_BT_NIMBLE_ROLE_OBSERVER
            CONFIG_BT_NIMBLE_MAX_CONNECTIONS
            CONFIG_BT_NIMBLE_MAX_BONDS
            CONFIG_BT_NIMBLE_MAX_CCCDS
            CONFIG_BT_NIMBLE_MSYS1_BLOCK_COUNT
            CONFIG_BT_NIMBLE_LOG_LEVEL
            CONFIG_NIMBLE_CPP_LOG_LEVEL
        )

        for macro in "${numeric_allowlist[@]}"; do
            value=$(grep -E "^[[:space:]]*#define[[:space:]]+${macro}[[:space:]]+" "$board_overrides_file" \
                | head -n 1 \
                | awk '{print $3}')
            if [[ -n "${value:-}" ]]; then
                if [[ "$value" =~ ^-?[0-9]+$ ]]; then
                    EXTRA_GLOBAL_DEFINES+=" -D${macro}=${value}"
                else
                    echo -e "${YELLOW}Warning: ignoring invalid ${macro} value in $board_overrides_file: '$value'${NC}"
                fi
            fi
        done

        # Flag-only defines (exported as -DNAME)
        flag_allowlist=(
            # TFT_eSPI controller + bus selection
            ILI9341_2_DRIVER
            USE_HSPI_PORT
        )

        for macro in "${flag_allowlist[@]}"; do
            if grep -Eq "^[[:space:]]*#define[[:space:]]+${macro}([[:space:]]+|$)" "$board_overrides_file"; then
                EXTRA_GLOBAL_DEFINES+=" -D${macro}"
            fi
        done

        # TFT_eSPI: support per-board setup without modifying global Arduino libraries.
        # If a board provides src/boards/<board>/User_Setup.h, force-include it and
        # mark USER_SETUP_LOADED so TFT_eSPI skips its default setup.
        if [[ -f "$board_override_dir/User_Setup.h" ]]; then
            EXTRA_GLOBAL_FLAGS+=" -DUSER_SETUP_LOADED=1 -include $board_override_dir/User_Setup.h"
        fi

    fi

    # Always embed board name for runtime identification (used by firmware update UX).
    # Use a string literal define: BUILD_BOARD_NAME="cyd-v2".
    # Important: do NOT include backslashes in the final define value.
    local BOARD_NAME_DEFINE="-DBUILD_BOARD_NAME=\"$board_name\""
    
    if [[ -d "$board_override_dir" ]]; then
        echo -e "${YELLOW}Config:    Using board-specific overrides from src/boards/$board_name/${NC}"
        # Add include path and define BOARD_HAS_OVERRIDE to trigger board_overrides.h inclusion
        # Important: apply to BOTH C++ and C compilation units (LVGL is built as C).
        # Sanitize board name for valid C++ macro (alphanumeric + underscore only)
        board_macro="BOARD_${board_name^^}"
        board_macro="${board_macro//[^A-Z0-9_]/_}"
        EXTRA_FLAGS+=(--build-property "compiler.cpp.extra_flags=-I$board_override_dir -D$board_macro -DBOARD_HAS_OVERRIDE=1 $BOARD_NAME_DEFINE$EXTRA_GLOBAL_DEFINES$EXTRA_GLOBAL_FLAGS")
        EXTRA_FLAGS+=(--build-property "compiler.c.extra_flags=-I$board_override_dir -D$board_macro -DBOARD_HAS_OVERRIDE=1 $BOARD_NAME_DEFINE$EXTRA_GLOBAL_DEFINES$EXTRA_GLOBAL_FLAGS")
    else
        echo "Config:    Using default configuration"
        EXTRA_FLAGS+=(--build-property "compiler.cpp.extra_flags=$BOARD_NAME_DEFINE$EXTRA_GLOBAL_DEFINES$EXTRA_GLOBAL_FLAGS")
        EXTRA_FLAGS+=(--build-property "compiler.c.extra_flags=$BOARD_NAME_DEFINE$EXTRA_GLOBAL_DEFINES$EXTRA_GLOBAL_FLAGS")
    fi
    echo ""

    # Print active compile-time flags for this board (for build logs)
    python3 "$SCRIPT_DIR/tools/compile_flags_report.py" build --board "$board_name"
    echo ""
    
    # Create board-specific build directory
    mkdir -p "$board_build_path"
    
    # Board-specific build properties (from config.sh), optional profile
    BUILD_PROPS_ARR=()
    if declare -f get_build_props_for_board >/dev/null; then
        mapfile -t BUILD_PROPS_ARR < <(get_build_props_for_board "$board_name" "$PROFILE")
        if [[ ${#BUILD_PROPS_ARR[@]} -gt 0 ]]; then
            echo "Build props: ${BUILD_PROPS_ARR[*]}"
        fi
    fi

    # Compile the sketch with optional board-specific includes and build props
    # Run compile and capture exit code (disable set -e temporarily to capture output even on failure)
    local build_output
    local build_exit_code
    set +e
    build_output=$("$ARDUINO_CLI" compile \
        --fqbn "$fqbn" \
        "${EXTRA_FLAGS[@]}" \
        "${BUILD_PROPS_ARR[@]}" \
        --output-dir "$board_build_path" \
        "$SKETCH_PATH" 2>&1)
    build_exit_code=$?
    set -e
    
    # Always show the build output
    echo "$build_output"
    
    # Check for build failure
    if [[ $build_exit_code -ne 0 ]]; then
        echo ""
        echo -e "${RED}✗ Build failed for $board_name (exit code: $build_exit_code)${NC}"
        return 1
    fi
    
    # Check for undefined references (silent linker warnings)
    if echo "$build_output" | grep -q "undefined reference to"; then
        echo ""
        echo -e "${RED}✗ Build completed but has undefined symbol errors:${NC}"
        echo "$build_output" | grep "undefined reference to" | sed 's/^/  /'
        echo ""
        echo -e "${YELLOW}Common causes:${NC}"
        echo "  - Missing driver .cpp inclusion in display_drivers.cpp or touch_drivers.cpp"
        echo "  - Arduino doesn't compile subdirectory .cpp files automatically"
        echo "  - Check that driver implementation files are included via #include"
        return 1
    fi
    
    # Check for other common warning patterns that indicate problems
    if echo "$build_output" | grep -qE "(warning:.*will not be executed|error:|fatal error:)"; then
        echo ""
        echo -e "${YELLOW}⚠ Build completed with warnings/errors:${NC}"
        echo "$build_output" | grep -E "(warning:.*will not be executed|error:|fatal error:)" | sed 's/^/  /'
        echo ""
    fi
    
    echo ""
    echo -e "${GREEN}✓ Build complete for $board_name${NC}"
    ls -lh "$board_build_path"/*.bin 2>/dev/null || echo "Binary files generated"
    echo ""
}

# Generate LVGL PNG assets (only when building for a display-enabled board)
if should_generate_png_assets "$TARGET_BOARD"; then
    echo "Generating LVGL PNG assets from assets/png..."
    python3 "$SCRIPT_DIR/tools/png2lvgl_assets.py" \
        "$SCRIPT_DIR/assets/png" \
        "$SCRIPT_DIR/src/app/png_assets.cpp" \
        "$SCRIPT_DIR/src/app/png_assets.h" \
        --prefix "img_"
    echo ""
else
    echo "Skipping PNG asset generation (no display build or no PNGs)."
    echo ""
fi

# Generate web assets (once for all builds)
echo "Generating web assets..."
"$SCRIPT_DIR/tools/minify-web-assets.sh" "$PROJECT_NAME" "$PROJECT_DISPLAY_NAME"
echo ""

# Determine which boards to build
if [[ -n "$TARGET_BOARD" ]]; then
    # Build specific board
    FQBN=$(get_fqbn_for_board "$TARGET_BOARD")
    if [[ -z "$FQBN" ]]; then
        echo -e "${RED}Error: Board '$TARGET_BOARD' not found${NC}"
        echo ""
        list_boards
        exit 1
    fi
    
    build_board "$FQBN" "$TARGET_BOARD"
else
    # Build all configured boards
    echo -e "${CYAN}=== Building ESP32 Firmware for All Boards ===${NC}"
    echo "Project:   $PROJECT_NAME"
    echo "Sketch:    $SKETCH_PATH"
    echo ""
    
    for board_name in "${!FQBN_TARGETS[@]}"; do
        fqbn="${FQBN_TARGETS[$board_name]}"
        build_board "$fqbn" "$board_name"
    done
    
    echo -e "${GREEN}=== All Builds Complete ===${NC}"
fi

# Update compile-time flags documentation and warn if it changed.
echo "Checking compile-time flags report..."
python3 "$SCRIPT_DIR/tools/compile_flags_report.py" md --out "docs/compile-time-flags.md"

COMPILE_FLAGS_DOC_HASH_AFTER=""
if [[ -f "$COMPILE_FLAGS_DOC" ]]; then
    COMPILE_FLAGS_DOC_HASH_AFTER=$(python3 - "$COMPILE_FLAGS_DOC" <<'PY'
import hashlib
from pathlib import Path
import sys

path = Path(sys.argv[1])
data = path.read_bytes()
print(hashlib.sha256(data).hexdigest())
PY
    )
fi

if [[ -n "$COMPILE_FLAGS_DOC_HASH" && -n "$COMPILE_FLAGS_DOC_HASH_AFTER" && "$COMPILE_FLAGS_DOC_HASH" != "$COMPILE_FLAGS_DOC_HASH_AFTER" ]]; then
    echo -e "${YELLOW}⚠ Note: docs/compile-time-flags.md was updated. Please commit the changes.${NC}"
elif [[ -n "$COMPILE_FLAGS_DOC_HASH" && -n "$COMPILE_FLAGS_DOC_HASH_AFTER" ]]; then
    echo -e "${GREEN}✓ Compile-time flags report unchanged.${NC}"
fi
