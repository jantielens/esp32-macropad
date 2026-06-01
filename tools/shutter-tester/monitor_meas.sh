#!/bin/bash

# ESP32 Shutter Tester — Measurement CSV Export Script
# Connects to serial, filters [MEAS] lines, and writes a timestamped CSV file.
# Usage: ./monitor_meas.sh [PORT] [BAUD] [--raw]
#   PORT  — serial port (auto-detected if omitted)
#   BAUD  — baud rate (default: 115200)
#   --raw — also write unfiltered serial to meas_raw_YYYYMMDD_HHMMSS.log

# Load common configuration and helper functions
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/config.sh"

# ============================================================================
# Argument parsing
# ============================================================================
RAW_MODE=false
POSITIONAL=()
for arg in "$@"; do
    case $arg in
        --raw)
            RAW_MODE=true
            ;;
        *)
            POSITIONAL+=("$arg")
            ;;
    esac
done

BAUD="${POSITIONAL[1]:-115200}"

# Get arduino-cli path
ARDUINO_CLI=$(find_arduino_cli)

# Auto-detect port if not specified
if [ -z "${POSITIONAL[0]}" ]; then
    if PORT=$(find_serial_port); then
        echo -e "${GREEN}Auto-detected port: $PORT${NC}"
    else
        echo -e "${RED}Error: No serial port detected${NC}"
        echo "Usage: $0 [PORT] [BAUD] [--raw]"
        exit 1
    fi
else
    PORT="${POSITIONAL[0]}"
fi

# ============================================================================
# Output file setup
# ============================================================================
TIMESTAMP=$(date +%Y%m%d_%H%M%S)
CSV_FILE="$SCRIPT_DIR/meas_${TIMESTAMP}.csv"
RAW_FILE="$SCRIPT_DIR/meas_raw_${TIMESTAMP}.log"

echo -e "${CYAN}=== ESP32 Shutter Tester — Measurement Export ===${NC}"
echo "Port:     $PORT"
echo "Baud:     $BAUD"
echo -e "CSV:      ${GREEN}$CSV_FILE${NC}"
if $RAW_MODE; then
    echo -e "Raw log:  ${GREEN}$RAW_FILE${NC}"
    echo "=== Raw log started $(date -Iseconds) ===" > "$RAW_FILE"
fi
echo -e "${YELLOW}Press Ctrl+C to stop capturing${NC}"
echo "---"

# ============================================================================
# CSV header
# ============================================================================
# The header is generated here from the known field contract rather than
# captured from the firmware's boot-time [MEAS] header line. This makes
# the script work correctly regardless of when it is connected.
#
# TOP-LEVEL FIELDS — must match the Serial.print() in shutter_measure.cpp
# shutter_measure_init(). If you change the firmware CSV contract, update
# this string AND the per-sensor loop below.
CSV_HEADER_TOP="#,preset_id,capture_id,timestamp_ms,matched_speed,matched_ms,target_manual,speed_locked,avg_ms,dev_pct,dev_stops,spread_ms,spread_pct,verdict,sensor_count,valid_sensor_count,capping_x,capping_y,skew_diff_us_mm,detected_travel,c1_skew_left_us,c1_skew_right_us,c2_skew_left_us,c2_skew_right_us"
# Derived from CSV_HEADER_TOP — do not set manually.
CSV_TOP_FIELD_COUNT=$(awk -F',' '{print NF}' <<< "$CSV_HEADER_TOP")
# Per-sensor column count — must match SHUTTER_SENSOR_CSV_COL_COUNT in shutter_measure.h.
# Each sensor block is 10 columns: ms,min,base,rms,depth,snr_db,threshold,start,end,total
CSV_SENSOR_COL_COUNT=10
CSV_SENSOR_COLS="ms,min,base,rms,depth,snr_db,threshold,start,end,total"

# ============================================================================
# State
# ============================================================================
MEAS_COUNT=0
HEADER_WRITTEN=false

# ============================================================================
# Cleanup / summary (fires on EXIT, including Ctrl+C)
# ============================================================================
cleanup() {
    echo
    echo -e "${CYAN}--- Capture complete ---${NC}"
    echo "Measurements captured: $MEAS_COUNT"
    if [ -f "$CSV_FILE" ]; then
        CSV_BYTES=$(wc -c < "$CSV_FILE")
        echo -e "CSV file:  ${GREEN}$CSV_FILE${NC} (${CSV_BYTES} bytes)"
    else
        echo "CSV file:  $CSV_FILE (no data written)"
    fi
    if $RAW_MODE && [ -f "$RAW_FILE" ]; then
        RAW_BYTES=$(wc -c < "$RAW_FILE")
        echo -e "Raw log:   ${GREEN}$RAW_FILE${NC} (${RAW_BYTES} bytes)"
    fi
}
trap cleanup EXIT

# ============================================================================
# Main capture loop
# ============================================================================
# Process substitution keeps the while loop in the current shell so that
# MEAS_COUNT and HEADER_WRITTEN are accessible in the EXIT trap.
while IFS= read -r line; do

    # Optionally write every line to the raw log with a timestamp
    if $RAW_MODE; then
        printf '[%s] %s\n' "$(date +%H:%M:%S.%3N)" "$line" >> "$RAW_FILE"
    fi

    # Skip firmware header lines — the script generates the header itself.
    if [[ "$line" == "[MEAS] #,"* ]] || [[ "$line" == "[MEAS:HDR] #,"* ]]; then
        continue
    fi

    # Data line: [MEAS] <number>,...
    if [[ "$line" == "[MEAS] "* ]]; then
        stripped="${line#\[MEAS\] }"

        # On the first data row: derive sensor count from the field count, then
        # write the generated header before the first data row.
        if ! $HEADER_WRITTEN; then
            field_count=$(awk -F',' '{print NF}' <<< "$stripped")
            sensor_count=$(( (field_count - CSV_TOP_FIELD_COUNT) / CSV_SENSOR_COL_COUNT ))
            header="$CSV_HEADER_TOP"
            for s in $(seq 1 "$sensor_count"); do
                IFS=',' read -ra cols <<< "$CSV_SENSOR_COLS"
                for col in "${cols[@]}"; do
                    header="$header,S${s}_${col}"
                done
            done
            printf '%s\n' "$header" >> "$CSV_FILE"
            HEADER_WRITTEN=true
        fi

        printf '%s\n' "$stripped" >> "$CSV_FILE"
        MEAS_COUNT=$((MEAS_COUNT + 1))
        printf '\r\033[K📊 Captured: %d measurements' "$MEAS_COUNT"
    fi

done < <("$ARDUINO_CLI" monitor -p "$PORT" -c baudrate="$BAUD" 2>/dev/null)
