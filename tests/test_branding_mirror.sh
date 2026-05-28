#!/bin/bash
# =============================================================================
# Branding mirror guard
# =============================================================================
# Asserts that device-class branding stays in sync between the C++ runtime
# (src/app/device_class_registry.cpp :: DESCRIPTORS[] table, full_name field)
# and the bash build tooling (config.sh :: device_class_brand_prefix).
#
# These two sources are intentionally separate (config.sh runs before any
# C++ compilation; device_class_registry.cpp runs on the device), but they
# MUST agree on the user-facing brand prefix for each device class. This
# test catches drift before a release ships with mismatched portal title,
# device name, SSID, HA model, or flash-page label.
#
# Extracts each row of the DESCRIPTORS[] table textually (no compilation
# required) and compares the 3rd quoted string (full_name) of each row to
# the bash helper output. Row matching is keyed on the DeviceClass::XXX
# enum tag, so table reordering does not break the test.

set -e
cd "$(dirname "$0")/.."

SRC="src/app/device_class_registry.cpp"
CFG="config.sh"

if [[ ! -f "$SRC" ]]; then
    echo "FAIL: missing $SRC" >&2
    exit 1
fi
if [[ ! -f "$CFG" ]]; then
    echo "FAIL: missing $CFG" >&2
    exit 1
fi

# Extract the full_name (3rd quoted string; the 1st field is the enum tag,
# which is not quoted) for a given DeviceClass tag from the DESCRIPTORS[]
# table. Walks only rows inside the table block to avoid matching stray
# literals in comments or other arrays.
extract_full_name() {
    local tag="$1"
    awk -v tag="$tag" '
        /static const DeviceClassDescriptor DESCRIPTORS\[\]/ { in_table = 1; next }
        in_table && /^\};/                                  { exit }
        in_table && index($0, "DeviceClass::" tag ",") {
            # Capture every double-quoted substring on the row.
            n = 0
            s = $0
            while (match(s, /"[^"]*"/)) {
                n++
                fields[n] = substr(s, RSTART + 1, RLENGTH - 2)
                s = substr(s, RSTART + RLENGTH)
            }
            if (n >= 3) { print fields[3] }
            exit
        }
    ' "$SRC"
}

cpp_macropad="$(extract_full_name MACROPAD)"
cpp_epaper="$(  extract_full_name EPAPER)"
cpp_headless="$(extract_full_name HEADLESS)"

# Sanity check: every extraction must yield a non-empty literal. An empty
# value means the table format changed in a way the extractor cannot parse,
# and we MUST fail loud rather than silently compare empty strings.
for pair in "macropad=$cpp_macropad" "epaper=$cpp_epaper" "headless=$cpp_headless"; do
    cls="${pair%%=*}"
    val="${pair#*=}"
    if [[ -z "$val" ]]; then
        echo "FAIL: could not extract full_name for class '$cls' from $SRC" >&2
        echo "      (DESCRIPTORS[] table format may have changed)"          >&2
        exit 1
    fi
done

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
        echo "FAIL: branding mismatch for class '$cls':"          >&2
        echo "  C++  (device_class_registry.cpp DESCRIPTORS[]): '$cpp'" >&2
        echo "  bash (config.sh prefix helper)                : '$sh'"  >&2
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
