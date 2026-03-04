#!/bin/bash

# ESP32 Serial Monitor Script
# Connects to ESP32 serial port for real-time output
# Optional logging to file with timestamps via --log flag

set -e

# Load common configuration and functions
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/config.sh"

# Parse arguments
LOG_FILE=""
POSITIONAL=()
for arg in "$@"; do
    case $arg in
        --log)
            LOG_FILE="$SCRIPT_DIR/monitor_$(date +%Y%m%d_%H%M%S).log"
            ;;
        --log=*)
            LOG_FILE="${arg#*=}"
            ;;
        *)
            POSITIONAL+=("$arg")
            ;;
    esac
done

# Configuration
BAUD="${POSITIONAL[1]:-115200}"         # Default baud rate, can be overridden by second argument

# Get arduino-cli path
ARDUINO_CLI=$(find_arduino_cli)

# Auto-detect port if not specified
if [ -z "${POSITIONAL[0]}" ]; then
    if PORT=$(find_serial_port); then
        echo -e "${GREEN}Auto-detected port: $PORT${NC}"
    else
        echo -e "${RED}Error: No serial port detected${NC}"
        echo "Usage: $0 [PORT] [BAUD] [--log[=file.log]]"
        echo "Example: $0 /dev/ttyUSB0 115200 --log"
        exit 1
    fi
else
    PORT="${POSITIONAL[0]}"
fi

echo -e "${CYAN}=== ESP32 Serial Monitor ===${NC}"

# Display connection info
echo "Connecting to: $PORT"
echo "Baud rate: $BAUD"
if [ -n "$LOG_FILE" ]; then
    echo -e "Logging to: ${GREEN}$LOG_FILE${NC}"
fi
echo -e "${YELLOW}Press Ctrl+C to exit${NC}"
echo "---"

# Start serial monitor, optionally tee to log file with timestamps
if [ -n "$LOG_FILE" ]; then
    echo "=== Monitor started $(date -Iseconds) ===" >> "$LOG_FILE"
    "$ARDUINO_CLI" monitor -p "$PORT" -c baudrate="$BAUD" | while IFS= read -r line; do
        stamped="[$(date +%H:%M:%S.%3N)] $line"
        printf '%s\n' "$line"
        printf '%s\n' "$stamped" >> "$LOG_FILE"
    done
else
    "$ARDUINO_CLI" monitor -p "$PORT" -c baudrate="$BAUD"
fi
