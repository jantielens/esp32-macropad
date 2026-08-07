#!/bin/bash
# Ensure required SD validation completes before Wi-Fi hardware initialization.

set -euo pipefail
cd "$(dirname "$0")/.."

if ! awk '
    /if \(storage_boot_should_halt\(sd_storage_mount\(\)\)\)/ { in_gate = 1; next }
    in_gate && /^[[:space:]]*#endif/ { gate_end = NR; in_gate = 0 }
    /wifi_manager_early_init\(\)/ { wifi_line = NR }
    END { exit !(gate_end && wifi_line > gate_end) }
' src/app/app.ino; then
    echo "FAIL: wifi_manager_early_init() must follow the required-SD validation gate." >&2
    exit 1
fi

echo "PASS: required SD validation precedes Wi-Fi initialization"