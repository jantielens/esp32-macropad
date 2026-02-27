#!/bin/bash

# ESP32 Upload & Monitor Script
# Convenience script for upload and monitor cycle
# Usage: ./um.sh [upload-options...] [board-name] [port]
#   - upload-options: Any upload.sh flags (e.g., --baud 921600)
#   - board-name: Required when multiple boards configured
#   - port: Optional, auto-detected if not provided

set -e

# Load common configuration and functions
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/config.sh"

echo -e "${CYAN}=== ESP32 Upload & Monitor ===${NC}"
echo ""

# Split upload.sh options from positional args (board/port).
# We accept leading flags ("-*") and forward them to upload.sh.
UPLOAD_ARGS=()
POSITIONAL_ARGS=()
while [[ $# -gt 0 ]]; do
	case "$1" in
		--)
			shift
			POSITIONAL_ARGS+=("$@")
			break
			;;
		--baud)
			UPLOAD_ARGS+=("$1" "${2:-}")
			shift 2
			;;
		-*)
			UPLOAD_ARGS+=("$1")
			shift
			;;
		*)
			POSITIONAL_ARGS+=("$1")
			shift
			;;
	esac
done

# Parse and validate board and port arguments
parse_board_and_port_args "${POSITIONAL_ARGS[@]}"

# Auto-detect port here so upload + monitor use the same port.
if [[ -z "$PORT" ]]; then
	if PORT=$(find_serial_port); then
		echo -e "${GREEN}Auto-detected port: $PORT${NC}"
	else
		echo -e "${RED}Error: No serial port detected${NC}"
		exit 1
	fi
fi

# Run upload → monitor
"$SCRIPT_DIR/upload.sh" "${UPLOAD_ARGS[@]}" "$BOARD" "$PORT"
echo ""
"$SCRIPT_DIR/monitor.sh" "$PORT"
