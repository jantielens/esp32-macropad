#!/bin/bash
# =============================================================================
# Branding mirror guard
# =============================================================================
# Asserts that device-class branding stays in sync between the C++ runtime
# (src/app/class_branding.cpp :: device_class_get_full_name) and the bash
# build tooling (config.sh :: device_class_brand_prefix).
#
# These two sources are intentionally separate (config.sh runs before any
# C++ compilation; class_branding.cpp runs on the device), but they MUST
# agree on the user-facing brand prefix for each device class. This test
# catches drift before a release ships with mismatched portal title,
# device name, SSID, HA model, or flash-page label.
#
# Extracts the three string literals from the C++ #if ladder textually
# (no compilation required) and compares to the bash helper output.

set -e
cd "$(dirname "$0")/.."

SRC="src/app/class_branding.cpp"
CFG="config.sh"

if [[ ! -f "$SRC" ]]; then
    echo "FAIL: missing $SRC" >&2
    exit 1
fi
if [[ ! -f "$CFG" ]]; then
    echo "FAIL: missing $CFG" >&2
    exit 1
fi

# Extract the device_class_get_full_name() body and pull the three literals.
# We rely on the ladder order: HAS_EPAPER, !HAS_DISPLAY, default (macropad).
fn_body="$(awk '
    /const char\* device_class_get_full_name\(\)/ { in_fn = 1 }
    in_fn { print }
    in_fn && /^\}/ { exit }
' "$SRC")"

cpp_epaper="$(  echo "$fn_body" | grep -oE 'return "[^"]+"' | sed -n '1p' | sed -E 's/return "(.*)"/\1/')"
cpp_headless="$(echo "$fn_body" | grep -oE 'return "[^"]+"' | sed -n '2p' | sed -E 's/return "(.*)"/\1/')"
cpp_macropad="$(echo "$fn_body" | grep -oE 'return "[^"]+"' | sed -n '3p' | sed -E 's/return "(.*)"/\1/')"

# Bash side.
# shellcheck disable=SC1091
source "$CFG"
sh_epaper="$(  device_class_brand_prefix epaper)"
sh_headless="$(device_class_brand_prefix headless)"
sh_macropad="$(device_class_brand_prefix macropad)"

fail=0
check() {
    local cls="$1" cpp="$2" sh="$3"
    if [[ "$cpp" != "$sh" ]]; then
        echo "FAIL: branding mismatch for class '$cls':"  >&2
        echo "  C++  (class_branding.cpp)     : '$cpp'"   >&2
        echo "  bash (config.sh prefix helper): '$sh'"    >&2
        fail=1
    else
        echo "  OK  $cls : '$cpp'"
    fi
}

check macropad "$cpp_macropad" "$sh_macropad"
check epaper   "$cpp_epaper"   "$sh_epaper"
check headless "$cpp_headless" "$sh_headless"

# Bonus: catch silently-added unknown classes by asserting the bash helper
# returns empty for one. Keeps the whitelist honest.
unknown="$(device_class_brand_prefix bogus-class)"
if [[ -n "$unknown" ]]; then
    echo "FAIL: device_class_brand_prefix returned non-empty for unknown class: '$unknown'" >&2
    fail=1
fi

if [[ $fail -ne 0 ]]; then
    exit 1
fi
echo "branding mirror OK"
