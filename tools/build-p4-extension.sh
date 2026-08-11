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

if [[ ! -x "$CXX" ]]; then
    echo "ESP32-P4 RISC-V compiler not found: $CXX" >&2
    exit 1
fi

mkdir -p "$(dirname "$OUTPUT")"

"$CXX" \
    -std=gnu++17 -Os -fPIC -ffreestanding -fno-exceptions -fno-rtti \
    -fdata-sections -ffunction-sections -nostdlib \
    -I"$PROJECT_DIR/src/app" \
    -shared -Wl,-Bsymbolic -Wl,--gc-sections \
    -Wl,--undefined=native_extension_create_instance \
    -Wl,--undefined=native_extension_destroy_instance \
    "$SOURCE" -o "$OUTPUT"

READELF="$TOOLCHAIN_DIR/riscv32-esp-elf-readelf"
if "$READELF" -r "$OUTPUT" | grep -q 'contains [1-9]'; then
    echo "Extension ELF contains unsupported relocations" >&2
    rm -f "$OUTPUT"
    exit 1
fi

echo "Built $OUTPUT"