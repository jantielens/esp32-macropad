#!/bin/bash

# ESP32 Serial Monitor Script
# Connects to ESP32 serial port for real-time output

set -e

# Load common configuration and functions
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/config.sh"

# Configuration
BAUD="${2:-115200}"         # Default baud rate, can be overridden by second argument

# Get arduino-cli path
ARDUINO_CLI=$(find_arduino_cli)

# Auto-detect port if not specified
if [ -z "$1" ]; then
    if PORT=$(find_serial_port); then
        echo -e "${GREEN}Auto-detected port: $PORT${NC}"
    else
        echo -e "${RED}Error: No serial port detected${NC}"
        echo "Usage: $0 [PORT] [BAUD]"
        echo "Example: $0 /dev/ttyUSB0 115200"
        exit 1
    fi
else
    PORT="$1"
fi

echo -e "${CYAN}=== ESP32 Serial Monitor ===${NC}"

# Display connection info
echo "Connecting to: $PORT"
echo "Baud rate: $BAUD"
echo -e "${YELLOW}Press Ctrl+C to exit${NC}"
echo "---"

# Start serial monitor
"$ARDUINO_CLI" monitor -p "$PORT" -c baudrate="$BAUD"
