#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
compiler="${1:-c++}"
tmp="$(mktemp -d)"
trap 'rm -f "$tmp/sdkconfig.h" "$tmp/profile.cpp" "$tmp/error"; rmdir "$tmp"' EXIT

cat > "$tmp/sdkconfig.h" <<'EOF'
#define CONFIG_NIMBLE_ROLE_CENTRAL 1
#define CONFIG_NIMBLE_ROLE_OBSERVER 1
#define CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_INTERNAL 1
EOF
cat > "$tmp/profile.cpp" <<'EOF'
#include <stdbool.h>
#include "board_config.h"
#if BLE_BACKEND == BLE_BACKEND_NIMBLE_ARDUINO
#if CONFIG_BT_NIMBLE_ROLE_CENTRAL || CONFIG_BT_NIMBLE_ROLE_OBSERVER || !CONFIG_BT_NIMBLE_ROLE_PERIPHERAL || !CONFIG_BT_NIMBLE_ROLE_BROADCASTER
#error "Unexpected broadcast roles"
#endif
#if CONFIG_BT_NIMBLE_MAX_CONNECTIONS != 1 || CONFIG_BT_NIMBLE_ENABLE_CONN_REATTEMPT || !CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_EXTERNAL
#error "Unexpected broadcast memory/connection policy"
#endif
#if defined(CONFIG_NIMBLE_ROLE_CENTRAL) || defined(CONFIG_NIMBLE_ROLE_OBSERVER) || defined(CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_INTERNAL)
#error "Legacy alias or internal allocation survives"
#endif
#else
#ifdef CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_EXTERNAL
#error "Core profile activated external NimBLE settings"
#endif
#endif
#if LVGL_FAST_MEM_IN_IRAM != EXPECT_IRAM
#error "Unexpected LVGL policy"
#endif
EOF

flags=(-I"$root/src/app" -I"$tmp" -include "$root/src/app/ble_build_config.h")
for language in c c++; do
    "$compiler" "${flags[@]}" -x "$language" -fsyntax-only "$tmp/profile.cpp" \
        -DCONFIG_IDF_TARGET_ESP32S3=1 -DEXPECT_IRAM=1
    "$compiler" "${flags[@]}" -x "$language" -fsyntax-only "$tmp/profile.cpp" \
        -DHAS_BLE=0 -DHAS_BLE_HID=0 -DEXPECT_IRAM=0
    "$compiler" "${flags[@]}" -x "$language" -fsyntax-only "$tmp/profile.cpp" \
        -DBLE_BACKEND=1 -DHAS_BLE_HID=0 -DBOARD_HAS_PSRAM=1 \
        -DCONFIG_IDF_TARGET_ESP32S3=1 -DLVGL_FAST_MEM_IN_IRAM=0 -DEXPECT_IRAM=0
done

reject() {
    local diagnostic="$1"
    shift
    if "$compiler" "${flags[@]}" -x c++ -fsyntax-only "$tmp/profile.cpp" \
        -DEXPECT_IRAM=0 "$@" > "$tmp/error" 2>&1; then
        echo "Unsupported BLE profile compiled: $*" >&2
        exit 1
    fi
    grep -q "$diagnostic" "$tmp/error"
}
reject 'Invalid BLE_BACKEND' -DBLE_BACKEND=99
reject 'HID is not supported' -DBLE_BACKEND=1 -DBOARD_HAS_PSRAM=1 \
    -DCONFIG_IDF_TARGET_ESP32S3=1 -DLVGL_FAST_MEM_IN_IRAM=0
reject 'requires ESP32-S3' -DBLE_BACKEND=1 -DHAS_BLE_HID=0 -DBOARD_HAS_PSRAM=1
reject 'requires ESP32-S3' -DBLE_BACKEND=1 -DHAS_BLE_HID=0 \
    -DCONFIG_IDF_TARGET_ESP32S3=1 -DLVGL_FAST_MEM_IN_IRAM=0
reject 'requires HAS_PSRAM' -DBLE_BACKEND=1 -DHAS_BLE_HID=0 -DHAS_PSRAM=0 \
    -DBOARD_HAS_PSRAM=1 -DCONFIG_IDF_TARGET_ESP32S3=1 -DLVGL_FAST_MEM_IN_IRAM=0
echo "BLE build profiles passed"
