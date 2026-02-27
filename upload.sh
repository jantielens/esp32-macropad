#!/bin/bash

# ESP32 Upload Script
# Uploads compiled firmware to ESP32 via serial port
# Usage: ./upload.sh [options] [board-name] [port]
#
# Options:
#   --full        Flash bootloader + partitions + boot_app0 + app at the correct offsets (preserves NVS by default)
#   --app-only    Flash only the app binary at the correct app offset
#   --merged      Flash merged image at 0x0 (destructive; will overwrite NVS on most layouts)
#   --baud <rate> Set esptool baud rate for full/app-only/merged/erase (default: auto; retries at 115200 on failure)
#   --erase-flash Erase entire flash before flashing (destructive)
#   --erase-nvs   Erase only the NVS partition before flashing (requires partitions.bin)
#
# Board/port arguments:
#   - board-name: Required when multiple boards configured
#   - port: Optional, auto-detected if not provided

set -e

# Load common configuration and functions
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/config.sh"

# Get arduino-cli path
ARDUINO_CLI=$(find_arduino_cli)

usage() {
    cat <<EOF
Usage:
  ${0##*/} [options] [board-name] [port]

Options:
    --full        Flash bootloader + partitions + boot_app0 + app at the correct offsets (preserves NVS by default)
    --app-only    Flash only the app binary at the correct app offset
    --merged      Flash merged image at 0x0 (destructive; overwrites NVS on most layouts)
        --baud <rate> Set esptool baud rate (default: auto; retries at 115200 on failure)
  --erase-flash Erase entire flash before flashing
  --erase-nvs   Erase only the NVS partition before flashing
  -h, --help    Show help
EOF
}

trim() {
    echo "$1" | xargs
}

get_fqbn_option() {
    local key="$1"
    local fqbn="$2"

    local _pkg _arch _board _opts
    IFS=':' read -r _pkg _arch _board _opts <<<"$fqbn"

    if [[ -z "${_opts:-}" ]]; then
        return 1
    fi

    local part
    IFS=',' read -ra parts <<<"$_opts"
    for part in "${parts[@]}"; do
        if [[ "$part" == "$key="* ]]; then
            echo "${part#*=}"
            return 0
        fi
    done
    return 1
}

get_bootloader_offset_for_chip() {
    local chip="$1"
    case "$chip" in
        # Per ESP32 Arduino core boards.txt (e.g. 3.3.x): classic ESP32 + ESP32-S2 use 0x1000.
        esp32|esp32s2)
            echo "0x1000"
            ;;
        # Newer families use 0x0.
        esp32s3|esp32c2|esp32c3|esp32c6|esp32h2)
            echo "0x0"
            ;;
        # ESP32-P4 uses 0x2000 (per ESP32 Arduino core 3.3.x boards.txt).
        esp32p4)
            echo "0x2000"
            ;;
        *)
            # Conservative fallback: match the core default.
            echo "0x1000"
            ;;
    esac
}

default_esptool_baud_for_port() {
    local port="$1"

    # UART adapters (/dev/ttyUSB*) are typically happy at 921600.
    if [[ "$port" == /dev/ttyUSB* ]]; then
        echo "921600"
        return 0
    fi

    # USB CDC (/dev/ttyACM*) can be finicky on some setups.
    if [[ "$port" == /dev/ttyACM* ]]; then
        echo "460800"
        return 0
    fi

    echo "115200"
}

find_esp32_core_dir() {
    local esp32_hw_base="$HOME/.arduino15/packages/esp32/hardware/esp32"
    if [[ ! -d "$esp32_hw_base" ]]; then
        return 1
    fi

    local esp32_dir
    esp32_dir="$(ls -1d "$esp32_hw_base"/*/ 2>/dev/null | sort -V | tail -n 1 || true)"
    esp32_dir="${esp32_dir%/}"
    if [[ -z "$esp32_dir" || ! -d "$esp32_dir" ]]; then
        return 1
    fi
    echo "$esp32_dir"
}

find_esptool() {
    local tools_dir="$HOME/.arduino15/packages/esp32/tools/esptool_py"

    # Newer ESP32 Arduino cores ship an `esptool` executable.
    local esptool_path
    esptool_path=$(find "$tools_dir" -name "esptool" -type f -executable 2>/dev/null | head -n 1 || true)
    if [[ -n "$esptool_path" ]]; then
        echo "$esptool_path"
        return 0
    fi

    # Older cores may ship `esptool.py`.
    esptool_path=$(find "$tools_dir" -name "esptool.py" -type f 2>/dev/null | head -n 1 || true)
    if [[ -n "$esptool_path" ]]; then
        echo "$esptool_path"
        return 0
    fi

    return 1
}

find_boot_app0_bin() {
    local esp32_dir
    esp32_dir=$(find_esp32_core_dir || true)
    if [[ -z "$esp32_dir" ]]; then
        return 1
    fi

    local candidate="$esp32_dir/tools/partitions/boot_app0.bin"
    if [[ -f "$candidate" ]]; then
        echo "$candidate"
        return 0
    fi

    return 1
}

partition_bin_find_offsets() {
    local partitions_bin="$1"
    local kind="$2"  # app-offset | nvs

    local parser="$SCRIPT_DIR/tools/parse_esp32_partitions.py"
    if [[ ! -f "$parser" ]]; then
        return 1
    fi

    if [[ ! -f "$partitions_bin" ]]; then
        return 1
    fi

    if [[ "$kind" == "app-offset" ]]; then
        python3 "$parser" "$partitions_bin" --app-offset --format hex
        return $?
    fi
    if [[ "$kind" == "nvs" ]]; then
        python3 "$parser" "$partitions_bin" --nvs --format hex
        return $?
    fi
    if [[ "$kind" == "otadata" ]]; then
        python3 "$parser" "$partitions_bin" --otadata --format hex
        return $?
    fi
    return 1
}

partition_csv_find_offsets() {
    local csv_path="$1"

    local app_offset=""
    local app_size=""
    local nvs_offset=""
    local nvs_size=""
    local otadata_offset=""

    while IFS=',' read -r name type subtype offset size flags; do
        name=$(trim "${name:-}")
        type=$(trim "${type:-}")
        subtype=$(trim "${subtype:-}")
        offset=$(trim "${offset:-}")
        size=$(trim "${size:-}")

        [[ -z "$name" ]] && continue
        [[ "$name" == \#* ]] && continue

        if [[ -z "$app_offset" && "$type" == "app" ]]; then
            if [[ "$name" == "app0" || "$subtype" == "ota_0" || "$subtype" == "factory" ]]; then
                app_offset="$offset"
                app_size="$size"
            fi
        fi

        if [[ -z "$nvs_offset" ]]; then
            if [[ "$name" == "nvs" || ( "$type" == "data" && "$subtype" == "nvs" ) ]]; then
                nvs_offset="$offset"
                nvs_size="$size"
            fi
        fi

        if [[ -z "$otadata_offset" ]]; then
            if [[ "$name" == "otadata" || ( "$type" == "data" && "$subtype" == "ota" ) ]]; then
                otadata_offset="$offset"
            fi
        fi
    done < "$csv_path"

    if [[ -z "$app_offset" ]]; then
        return 1
    fi

    echo "app_offset=$app_offset"
    echo "app_size=$app_size"
    if [[ -n "$nvs_offset" && -n "$nvs_size" ]]; then
        echo "nvs_offset=$nvs_offset"
        echo "nvs_size=$nvs_size"
    fi
    if [[ -n "$otadata_offset" ]]; then
        echo "otadata_offset=$otadata_offset"
    fi
}

get_partition_csv_path() {
    local board_build_path="$1"
    local fqbn="$2"

    # Prefer build output (if present) because it reflects exactly what was built.
    if [[ -f "$board_build_path/partitions.csv" ]]; then
        echo "$board_build_path/partitions.csv"
        return 0
    fi

    local scheme
    scheme=$(get_fqbn_option "PartitionScheme" "$fqbn" || true)
    if [[ -z "$scheme" ]]; then
        return 1
    fi

    local esp32_dir
    esp32_dir=$(find_esp32_core_dir || true)
    if [[ -z "$esp32_dir" ]]; then
        return 1
    fi

    local board_id
    board_id=$(echo "$fqbn" | cut -d':' -f3)

    local boards_txt="$esp32_dir/boards.txt"
    local partitions_dir="$esp32_dir/tools/partitions"

    if [[ -f "$boards_txt" ]]; then
        local partition_name_no_ext
        partition_name_no_ext=$(grep -E "^${board_id}\.menu\.PartitionScheme\.${scheme}\.build\.partitions=" "$boards_txt" | tail -n 1 | cut -d'=' -f2- | xargs || true)

        if [[ -n "$partition_name_no_ext" && -f "$partitions_dir/${partition_name_no_ext}.csv" ]]; then
            echo "$partitions_dir/${partition_name_no_ext}.csv"
            return 0
        fi
    fi

    # Fallback heuristics (some cores use scheme id as filename)
    if [[ -f "$partitions_dir/${scheme}.csv" ]]; then
        echo "$partitions_dir/${scheme}.csv"
        return 0
    fi
    if [[ -f "$partitions_dir/partitions_${scheme}.csv" ]]; then
        echo "$partitions_dir/partitions_${scheme}.csv"
        return 0
    fi

    return 1
}

parse_args() {
    MODE=""
    ERASE_FLASH=false
    ERASE_NVS=false
    BAUD_RATE=""

    REST_ARGS=()
    while [[ $# -gt 0 ]]; do
        case "$1" in
            --full)
                MODE="full"
                shift
                ;;
            --app-only)
                MODE="app-only"
                shift
                ;;
            --merged)
                MODE="merged"
                shift
                ;;
            --erase-flash)
                ERASE_FLASH=true
                shift
                ;;
            --erase-nvs)
                ERASE_NVS=true
                shift
                ;;
            --baud)
                if [[ -z "${2:-}" ]]; then
                    echo -e "${RED}Error: --baud requires a value${NC}"
                    usage
                    exit 1
                fi
                BAUD_RATE="$2"
                shift 2
                ;;
            -h|--help)
                usage
                exit 0
                ;;
            --)
                shift
                REST_ARGS+=("$@")
                break
                ;;
            -*)
                echo -e "${RED}Error: Unknown option: $1${NC}"
                usage
                exit 1
                ;;
            *)
                REST_ARGS+=("$1")
                shift
                ;;
        esac
    done
}

flash_with_esptool() {
    local chip_type="$1"
    local port="$2"
    local esptool_cmd="$3"
    local baud_rate="$4"
    shift 4

    local rc=0
    if [[ "$esptool_cmd" == *.py ]]; then
        python3 "$esptool_cmd" --chip "$chip_type" --port "$port" --baud "$baud_rate" "$@" || rc=$?
    else
        "$esptool_cmd" --chip "$chip_type" --port "$port" --baud "$baud_rate" "$@" || rc=$?
    fi

    if [[ $rc -ne 0 && "$baud_rate" != "115200" ]]; then
        echo -e "${YELLOW}esptool failed at baud $baud_rate; retrying at 115200...${NC}" >&2
        if [[ "$esptool_cmd" == *.py ]]; then
            python3 "$esptool_cmd" --chip "$chip_type" --port "$port" --baud 115200 "$@"
        else
            "$esptool_cmd" --chip "$chip_type" --port "$port" --baud 115200 "$@"
        fi
        return $?
    fi

    return $rc
}

parse_args "$@"

# Parse board and port arguments (positional after options)
parse_board_and_port_args "${REST_ARGS[@]}"

# Board-specific build directory
BOARD_BUILD_PATH="$BUILD_PATH/$BOARD"

# Auto-detect port if not specified
if [[ -z "$PORT" ]]; then
    if PORT=$(find_serial_port); then
        echo -e "${GREEN}Auto-detected port: $PORT${NC}"
    else
        echo -e "${RED}Error: No serial port detected${NC}"
        if [[ "${#FQBN_TARGETS[@]}" -gt 1 ]]; then
            echo "Usage: ${0##*/} <board-name> [port]"
            echo "Example: ${0##*/} $BOARD /dev/ttyUSB0"
        else
            echo "Usage: ${0##*/} [port]"
            echo "Example: ${0##*/} /dev/ttyUSB0"
        fi
        exit 1
    fi
fi

echo -e "${CYAN}=== Uploading ESP32 Firmware ===${NC}"

# Check if board build directory exists
if [ ! -d "$BOARD_BUILD_PATH" ]; then
    echo -e "${RED}Error: Build directory not found for board '$BOARD'${NC}"
    echo "Expected: $BOARD_BUILD_PATH"
    echo "Please run: ./build.sh $BOARD"
    exit 1
fi

# Display upload configuration
echo "Board: $BOARD"
echo "FQBN:  $FQBN"
echo "Port:  $PORT"
echo "Build: $BOARD_BUILD_PATH"
echo ""

PARTITION_SCHEME=$(get_fqbn_option "PartitionScheme" "$FQBN" || true)
if [[ -z "${MODE}" ]]; then
    if [[ -n "$PARTITION_SCHEME" ]]; then
        MODE="full"
    else
        MODE="arduino"
    fi
fi

echo "Mode:  $MODE"
if [[ -n "$PARTITION_SCHEME" ]]; then
    echo "PartitionScheme: $PARTITION_SCHEME"
fi

CHIP_TYPE=$(get_chip_type_from_fqbn "$FQBN")
echo "Chip:  $CHIP_TYPE"
ESPTOOL_CMD=""
if [[ "$MODE" == "full" || "$MODE" == "app-only" || "$MODE" == "merged" || "$ERASE_FLASH" == "true" || "$ERASE_NVS" == "true" ]]; then
    ESPTOOL_CMD=$(find_esptool || true)
    if [[ -z "$ESPTOOL_CMD" ]]; then
        echo -e "${RED}Error: esptool not found${NC}"
        echo "Please ensure ESP32 platform is installed (run setup.sh)"
        exit 1
    fi

    if [[ -z "${BAUD_RATE:-}" ]]; then
        BAUD_RATE=$(default_esptool_baud_for_port "$PORT")
    fi
fi

PARTITIONS_BIN="$BOARD_BUILD_PATH/app.ino.partitions.bin"

if [[ "$ERASE_FLASH" == "true" ]]; then
    echo -e "${YELLOW}Erasing entire flash...${NC}"
    flash_with_esptool "$CHIP_TYPE" "$PORT" "$ESPTOOL_CMD" "$BAUD_RATE" erase_flash
fi

if [[ "$ERASE_NVS" == "true" ]]; then
    nvs_info=$(partition_bin_find_offsets "$PARTITIONS_BIN" "nvs" || true)
    if [[ -z "$nvs_info" ]]; then
        # Backward-compatible fallback to partition CSV.
        PARTITION_CSV=$(get_partition_csv_path "$BOARD_BUILD_PATH" "$FQBN" || true)
        PART_INFO=$(partition_csv_find_offsets "$PARTITION_CSV" || true)
        local_nvs_offset=$(echo "$PART_INFO" | grep '^nvs_offset=' | cut -d'=' -f2- || true)
        local_nvs_size=$(echo "$PART_INFO" | grep '^nvs_size=' | cut -d'=' -f2- || true)
        if [[ -z "$local_nvs_offset" || -z "$local_nvs_size" ]]; then
            echo -e "${RED}Error: Failed to locate NVS partition offset/size${NC}"
            echo "Tried: $PARTITIONS_BIN and partition CSV mapping"
            exit 1
        fi
    else
        local_nvs_offset=$(echo "$nvs_info" | awk '{print $1}')
        local_nvs_size=$(echo "$nvs_info" | awk '{print $2}')
    fi
    echo -e "${YELLOW}Erasing NVS region ($local_nvs_offset, $local_nvs_size)...${NC}"
    flash_with_esptool "$CHIP_TYPE" "$PORT" "$ESPTOOL_CMD" "$BAUD_RATE" erase_region "$local_nvs_offset" "$local_nvs_size"
fi

if [[ "$MODE" == "arduino" ]]; then
    "$ARDUINO_CLI" upload \
        --fqbn "$FQBN" \
        --port "$PORT" \
        --input-dir "$BOARD_BUILD_PATH"
elif [[ "$MODE" == "full" ]]; then
    BOOTLOADER_BIN="$BOARD_BUILD_PATH/app.ino.bootloader.bin"
    if [[ ! -f "$BOOTLOADER_BIN" ]]; then
        echo -e "${RED}Error: bootloader binary not found${NC}"
        echo "Expected: $BOOTLOADER_BIN"
        echo "Please run: ./build.sh $BOARD"
        exit 1
    fi
    if [[ ! -f "$PARTITIONS_BIN" ]]; then
        echo -e "${RED}Error: partitions binary not found${NC}"
        echo "Expected: $PARTITIONS_BIN"
        echo "Please run: ./build.sh $BOARD"
        exit 1
    fi

    APP_BIN="$BOARD_BUILD_PATH/app.ino.bin"
    if [[ ! -f "$APP_BIN" ]]; then
        echo -e "${RED}Error: app binary not found${NC}"
        echo "Expected: $APP_BIN"
        echo "Please run: ./build.sh $BOARD"
        exit 1
    fi

    BOOT_APP0_BIN=$(find_boot_app0_bin || true)
    if [[ -z "$BOOT_APP0_BIN" ]]; then
        echo -e "${RED}Error: boot_app0.bin not found in ESP32 core${NC}"
        echo "Hint: run ./setup.sh to install the ESP32 core/toolchain"
        exit 1
    fi

    APP_OFFSET=$(partition_bin_find_offsets "$PARTITIONS_BIN" "app-offset" || true)
    OTADATA_OFFSET=$(partition_bin_find_offsets "$PARTITIONS_BIN" "otadata" || true)
    if [[ -z "$APP_OFFSET" || -z "$OTADATA_OFFSET" ]]; then
        # Backward-compatible fallback to partition CSV.
        PARTITION_CSV=$(get_partition_csv_path "$BOARD_BUILD_PATH" "$FQBN" || true)
        PART_INFO=$(partition_csv_find_offsets "$PARTITION_CSV" || true)
        if [[ -z "$APP_OFFSET" ]]; then
            APP_OFFSET=$(echo "$PART_INFO" | grep '^app_offset=' | cut -d'=' -f2- || true)
        fi
        if [[ -z "$OTADATA_OFFSET" ]]; then
            OTADATA_OFFSET=$(echo "$PART_INFO" | grep '^otadata_offset=' | cut -d'=' -f2- || true)
        fi
    fi
    if [[ -z "$APP_OFFSET" ]]; then
        echo -e "${RED}Error: Failed to determine app offset for full flash${NC}"
        echo "Tried: $PARTITIONS_BIN and partition CSV mapping"
        exit 1
    fi
    # Fall back to 0xE000 (standard ESP32 layout) if otadata offset not found.
    if [[ -z "$OTADATA_OFFSET" ]]; then
        OTADATA_OFFSET="0xE000"
    fi

    BOOTLOADER_OFFSET=$(get_bootloader_offset_for_chip "$CHIP_TYPE")
    echo -e "${YELLOW}Flashing multi-part image (preserve NVS): bootloader@${BOOTLOADER_OFFSET}, partitions@0x8000, boot_app0@${OTADATA_OFFSET}, app@$APP_OFFSET...${NC}"
    flash_with_esptool "$CHIP_TYPE" "$PORT" "$ESPTOOL_CMD" "$BAUD_RATE" write-flash -z \
        "$BOOTLOADER_OFFSET" "$BOOTLOADER_BIN" \
        0x8000 "$PARTITIONS_BIN" \
        "$OTADATA_OFFSET" "$BOOT_APP0_BIN" \
        "$APP_OFFSET" "$APP_BIN"
elif [[ "$MODE" == "app-only" ]]; then
    APP_BIN="$BOARD_BUILD_PATH/app.ino.bin"
    if [[ ! -f "$APP_BIN" ]]; then
        echo -e "${RED}Error: app binary not found${NC}"
        echo "Expected: $APP_BIN"
        echo "Please run: ./build.sh $BOARD"
        exit 1
    fi

    APP_OFFSET=$(partition_bin_find_offsets "$PARTITIONS_BIN" "app-offset" || true)
    if [[ -z "$APP_OFFSET" ]]; then
        # Backward-compatible fallback to partition CSV.
        PARTITION_CSV=$(get_partition_csv_path "$BOARD_BUILD_PATH" "$FQBN" || true)
        PART_INFO=$(partition_csv_find_offsets "$PARTITION_CSV" || true)
        APP_OFFSET=$(echo "$PART_INFO" | grep '^app_offset=' | cut -d'=' -f2- || true)
        if [[ -z "$APP_OFFSET" ]]; then
            echo -e "${RED}Error: Failed to determine app offset for app-only flash${NC}"
            echo "Tried: $PARTITIONS_BIN and partition CSV mapping"
            exit 1
        fi
    fi
    echo -e "${YELLOW}Flashing app at $APP_OFFSET (app-only)...${NC}"
    flash_with_esptool "$CHIP_TYPE" "$PORT" "$ESPTOOL_CMD" "$BAUD_RATE" write-flash -z "$APP_OFFSET" "$APP_BIN"
elif [[ "$MODE" == "merged" ]]; then
    MERGED_BIN="$BOARD_BUILD_PATH/app.ino.merged.bin"
    if [[ ! -f "$MERGED_BIN" ]]; then
        echo -e "${RED}Error: merged firmware not found${NC}"
        echo "Expected: $MERGED_BIN"
        echo "Please run: ./build.sh $BOARD"
        exit 1
    fi

    echo -e "${YELLOW}Flashing merged image at 0x0 (destructive; overwrites NVS)...${NC}"
    flash_with_esptool "$CHIP_TYPE" "$PORT" "$ESPTOOL_CMD" "$BAUD_RATE" write-flash -z 0x0 "$MERGED_BIN"
else
    echo -e "${RED}Error: Unknown mode: $MODE${NC}"
    exit 1
fi

echo ""
echo -e "${GREEN}=== Upload Complete ===${NC}"
echo "Run ./monitor.sh to view serial output"
