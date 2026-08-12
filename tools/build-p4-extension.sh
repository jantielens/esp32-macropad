#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 2 ]]; then
    echo "Usage: $0 <extension-source.cpp> <output.elf>" >&2
    exit 2
fi

SOURCE=$1
OUTPUT=$2
SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
PROJECT_DIR=$(cd "$SCRIPT_DIR/.." && pwd)
TOOLCHAIN_DIR=${ESP32_P4_TOOLCHAIN_DIR:-"$HOME/.arduino15/packages/esp32/tools/esp-rv32/2511/bin"}
CXX="$TOOLCHAIN_DIR/riscv32-esp-elf-g++"
READELF="$TOOLCHAIN_DIR/riscv32-esp-elf-readelf"
API_HEADER="$PROJECT_DIR/src/app/native_extension_api.h"
SDK_FLAGS=${ESP32_P4_SDK_CPP_FLAGS:-$(find "$HOME/.arduino15/packages/esp32/tools/esp32p4_es-libs" -path '*/flags/cpp_flags' -type f 2>/dev/null | sort -V | tail -n 1)}

if [[ ! -x "$CXX" ]]; then
    echo "ESP32-P4 RISC-V compiler not found: $CXX" >&2
    exit 1
fi

if [[ ! -f "$API_HEADER" || ! -f "$SDK_FLAGS" ]]; then
    echo "Native extension ABI header or ESP32-P4 SDK flags not found" >&2
    exit 1
fi

ABI_VERSION=$(sed -n 's/^#define NATIVE_EXTENSION_ABI_VERSION \([0-9][0-9]*\)u$/\1/p' "$API_HEADER")
TARGET_ABI=$(sed -n 's/^#define NATIVE_EXTENSION_TARGET_ABI "\([^"]*\)"$/\1/p' "$API_HEADER")
if [[ -z "$ABI_VERSION" || -z "$TARGET_ABI" ]]; then
    echo "Unable to read native extension ABI metadata" >&2
    exit 1
fi
mapfile -t TARGET_FLAGS < <(tr ' ' '\n' < "$SDK_FLAGS" | grep -E '^-march=|^-mabi=' | tail -n 2)
if [[ ${#TARGET_FLAGS[@]} -ne 2 ]]; then
    echo "Unable to derive ESP32-P4 target flags" >&2
    exit 1
fi

mkdir -p "$(dirname "$OUTPUT")"

"$CXX" \
    -std=gnu++17 -Os -fPIC -ffreestanding -fno-exceptions -fno-rtti \
    "${TARGET_FLAGS[@]}" \
    -fdata-sections -ffunction-sections -nostdlib \
    -I"$PROJECT_DIR/src/app" \
    -shared -Wl,-Bsymbolic -Wl,--gc-sections \
    -Wl,--undefined=native_extension_create_instance \
    -Wl,--undefined=native_extension_destroy_instance \
    -Wl,--undefined=native_extension_descriptor \
    "$SOURCE" "$PROJECT_DIR/extensions/extension_runtime.cpp" -o "$OUTPUT"

if "$READELF" -r "$OUTPUT" | grep -q 'contains [1-9]'; then
    echo "Extension ELF contains unsupported relocations" >&2
    rm -f "$OUTPUT"
    exit 1
fi

if ! "$READELF" -sW "$OUTPUT" | grep -q '[[:space:]]native_extension_descriptor$'; then
    echo "Extension package is missing native_extension_descriptor" >&2
    rm -f "$OUTPUT"
    exit 1
fi

echo "Built $OUTPUT (ABI $ABI_VERSION, target $TARGET_ABI)"