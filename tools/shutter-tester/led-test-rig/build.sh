#!/bin/bash
# Build the LED test rig sketch for ESP32-S3 Super Mini.
# Self-contained: does NOT use the root build.sh.
set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FQBN="esp32:esp32:esp32s3:CDCOnBoot=cdc"
BUILD_DIR="$SCRIPT_DIR/build"

MODE_ARG="${1:-}"

usage() {
  cat <<'EOF'
Usage: ./build.sh <mode>

Modes:
  binary-simultaneous   all sensors fire together with hard binary edges
  binary                alias for binary-simultaneous
  simultaneous          alias for binary-simultaneous
  binary-travel         hard binary edges with 5 ms sensor-to-sensor travel
  travel                alias for binary-travel
  vertical              generic vertical focal-plane simulation with shaped ramps
  vertical-sim          alias for vertical
  om1                   Olympus OM-1-inspired vertical focal-plane simulation
  bad-camera            deterministic uneven-travel and exposure-skew simulation
  bad                   alias for bad-camera
  m6                    Leica M6-inspired horizontal focal-plane simulation

You can also pass a raw LED_TEST_RIG_MODE_* macro name.
EOF
}

if [[ -z "$MODE_ARG" ]]; then
  echo "Missing required mode." >&2
  echo >&2
  usage >&2
  exit 1
fi

mode_macro=""
case "$MODE_ARG" in
  -h|--help|help) usage; exit 0 ;;
  binary-simultaneous|binary|simultaneous) mode_macro="LED_TEST_RIG_MODE_BINARY_SIMULTANEOUS" ;;
  binary-travel|travel) mode_macro="LED_TEST_RIG_MODE_BINARY_TRAVEL" ;;
  vertical|vertical-sim) mode_macro="LED_TEST_RIG_MODE_VERTICAL_SIM" ;;
  om1) mode_macro="LED_TEST_RIG_MODE_OM1" ;;
  bad-camera|bad) mode_macro="LED_TEST_RIG_MODE_BAD_CAMERA" ;;
  m6) mode_macro="LED_TEST_RIG_MODE_M6" ;;
  LED_TEST_RIG_MODE_*) mode_macro="$MODE_ARG" ;;
  *)
    echo "Unknown mode: $MODE_ARG" >&2
    echo >&2
    usage >&2
    exit 1
    ;;
esac

extra_flags="-I$SCRIPT_DIR/boards/s3-super-mini"
extra_flags="$extra_flags -DLED_TEST_RIG_MODE=$mode_macro"
echo "Building LED test rig mode: $mode_macro"

arduino-cli compile \
  --fqbn "$FQBN" \
  --build-property "build.extra_flags=$extra_flags" \
  --output-dir "$BUILD_DIR" \
  "$SCRIPT_DIR"

echo "Build complete: $BUILD_DIR"
