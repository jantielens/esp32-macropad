#!/bin/bash
# Reject allocator diagnostics that can disrupt continuous MIPI-DSI scan-out.

set -euo pipefail
cd "$(dirname "$0")/.."

for required_command in awk grep; do
    if ! command -v "$required_command" >/dev/null 2>&1; then
        echo "FAIL: required command is unavailable: $required_command" >&2
        exit 1
    fi
done

PSRAM_WALK='heap_caps_get_largest_free_block\(MALLOC_CAP_SPIRAM\)'

unexpected_psram_walks="$(grep -rlE --include='*.cpp' --include='*.h' "$PSRAM_WALK" src/app | grep -vx 'src/app/device_telemetry.cpp' || true)"
if [[ -n "$unexpected_psram_walks" ]]; then
    echo "FAIL: PSRAM largest-block walks are allowed only in device_telemetry.cpp:" >&2
    echo "$unexpected_psram_walks" >&2
    exit 1
fi

if ! awk '
    /^[[:space:]]*#if[[:space:]]+TELEMETRY_ALLOW_PSRAM_POOL_WALK/ { allowed = 1; next }
    /^[[:space:]]*#endif/ { allowed = 0 }
    /heap_caps_get_largest_free_block\(MALLOC_CAP_SPIRAM\)/ && !allowed { failed = 1 }
    END { exit failed }
' src/app/device_telemetry.cpp; then
    echo "FAIL: PSRAM largest-block walk must be gated by TELEMETRY_ALLOW_PSRAM_POOL_WALK." >&2
    exit 1
fi

if grep -rnE --include='*.cpp' --include='*.h' 'heap_caps_get_info' src/app; then
    echo "FAIL: heap_caps_get_info() walks allocator pools and is forbidden in firmware telemetry." >&2
    exit 1
fi

if grep -n 'heap_caps_get_largest_free_block' src/app/health_binding.cpp; then
    echo "FAIL: health bindings must use cached telemetry values, not allocator walks." >&2
    exit 1
fi

echo "PASS: display-safe telemetry heap-walk guard"