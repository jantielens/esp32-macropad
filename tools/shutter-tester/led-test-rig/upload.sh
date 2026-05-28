#!/bin/bash
# Upload the LED test rig firmware. Usage: ./upload.sh [port]
set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FQBN="esp32:esp32:esp32s3:CDCOnBoot=cdc"
PORT="${1:-/dev/ttyACM0}"

arduino-cli upload \
  --fqbn "$FQBN" \
  --port "$PORT" \
  --input-dir "$SCRIPT_DIR/build" \
  "$SCRIPT_DIR"

echo "Upload complete on $PORT"
