#!/bin/bash
# Reject allocator diagnostics that can disrupt continuous MIPI-DSI scan-out.

set -euo pipefail
cd "$(dirname "$0")/.."

PSRAM_WALK='heap_caps_get_largest_free_block\(MALLOC_CAP_SPIRAM\)'

unexpected_psram_walks="$(rg -l "$PSRAM_WALK" src/app --glob '*.{cpp,h}' | grep -vx 'src/app/device_telemetry.cpp' || true)"
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

if rg -n 'heap_caps_get_info' src/app --glob '*.{cpp,h}'; then
    echo "FAIL: heap_caps_get_info() walks allocator pools and is forbidden in firmware telemetry." >&2
    exit 1
fi

if rg -n 'heap_caps_get_largest_free_block' src/app/health_binding.cpp; then
    echo "FAIL: health bindings must use cached telemetry values, not allocator walks." >&2
    exit 1
fi

echo "PASS: display-safe telemetry heap-walk guard"