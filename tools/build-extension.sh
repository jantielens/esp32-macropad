#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 3 ]]; then
    echo "Usage: $0 <p4|s3> <extension-source.cpp> <output.elf>" >&2
    exit 2
fi

TARGET=$1
SOURCE=$2
OUTPUT=$3
SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
PROJECT_DIR=$(cd "$SCRIPT_DIR/.." && pwd)
API_HEADER="$PROJECT_DIR/src/app/native_extension_api.h"

case "$TARGET" in
    p4)
        TOOLCHAIN_DIR=${ESP32_P4_TOOLCHAIN_DIR:-"$HOME/.arduino15/packages/esp32/tools/esp-rv32/2511/bin"}
        CXX="$TOOLCHAIN_DIR/riscv32-esp-elf-g++"
        READELF="$TOOLCHAIN_DIR/riscv32-esp-elf-readelf"
        TARGET_ABI="rv32imafc-ilp32f"
        SDK_FLAGS=${ESP32_P4_SDK_CPP_FLAGS:-$(find "$HOME/.arduino15/packages/esp32/tools/esp32p4_es-libs" -path '*/flags/cpp_flags' -type f 2>/dev/null | sort -V | tail -n 1)}
        mapfile -t TARGET_FLAGS < <(tr ' ' '\n' < "$SDK_FLAGS" | grep -E '^-march=|^-mabi=' | tail -n 2)
        [[ ${#TARGET_FLAGS[@]} -eq 2 ]] || { echo "Unable to derive ESP32-P4 target flags" >&2; exit 1; }
        LINK_FLAGS=(-shared -Wl,-Bsymbolic)
        TARGET_LIBS=()
        EXPECTED_MACHINE="RISC-V"
        ;;
    s3)
        TOOLCHAIN_DIR=${ESP32_S3_TOOLCHAIN_DIR:-"$HOME/.arduino15/packages/esp32/tools/esp-x32/2511/bin"}
        CXX="$TOOLCHAIN_DIR/xtensa-esp32s3-elf-g++"
        READELF="$TOOLCHAIN_DIR/xtensa-esp32s3-elf-readelf"
        TARGET_ABI="xtensa-esp32s3"
        TARGET_FLAGS=()
        LINK_FLAGS=(-shared -Wl,-Bsymbolic)
        TARGET_LIBS=(-lgcc)
        EXPECTED_MACHINE="Tensilica Xtensa Processor"
        ;;
    *)
        echo "Unknown extension target: $TARGET (expected p4 or s3)" >&2
        exit 2
        ;;
esac

if [[ ! -x "$CXX" || ! -x "$READELF" || ! -f "$API_HEADER" ]]; then
    echo "Native extension toolchain or ABI header not found for $TARGET" >&2
    exit 1
fi

ABI_VERSION=$(sed -n 's/^#define NATIVE_EXTENSION_ABI_VERSION \([0-9][0-9]*\)u$/\1/p' "$API_HEADER")
[[ -n "$ABI_VERSION" ]] || { echo "Unable to read native extension ABI version" >&2; exit 1; }
mkdir -p "$(dirname "$OUTPUT")"

"$CXX" \
    -std=gnu++17 -Os -fPIC -ffreestanding -fno-exceptions -fno-rtti \
    "${TARGET_FLAGS[@]}" \
    "-DNATIVE_EXTENSION_TARGET_ABI=\"$TARGET_ABI\"" \
    -fdata-sections -ffunction-sections -nostdlib \
    -I"$PROJECT_DIR/src/app" \
    "${LINK_FLAGS[@]}" -Wl,--gc-sections \
    -Wl,--undefined=native_extension_create_instance \
    -Wl,--undefined=native_extension_destroy_instance \
    -Wl,--undefined=native_extension_shutdown \
    -Wl,--undefined=native_extension_descriptor \
    "$SOURCE" "$PROJECT_DIR/extensions/extension_runtime.cpp" \
    "${TARGET_LIBS[@]}" -o "$OUTPUT"

if ! "$READELF" -h "$OUTPUT" | grep -q "Machine:.*$EXPECTED_MACHINE"; then
    echo "Extension ELF has an unexpected machine type" >&2
    rm -f "$OUTPUT"
    exit 1
fi
if [[ "$TARGET" == "s3" ]]; then
    if "$READELF" -rW "$OUTPUT" | grep -E 'R_' | grep -vq 'R_XTENSA_RELATIVE'; then
        echo "S3 extension ELF contains unsupported relocations" >&2
        rm -f "$OUTPUT"
        exit 1
    fi
elif "$READELF" -r "$OUTPUT" | grep -q 'contains [1-9]\|R_'; then
    echo "Extension ELF contains unsupported relocations" >&2
    rm -f "$OUTPUT"
    exit 1
fi
if ! "$READELF" -sW "$OUTPUT" | grep -q '[[:space:]]native_extension_descriptor$' ||
   ! "$READELF" -sW "$OUTPUT" | grep -q '[[:space:]]native_extension_shutdown$'; then
    echo "Extension package is missing required ABI exports" >&2
    rm -f "$OUTPUT"
    exit 1
fi

python3 "$SCRIPT_DIR/verify_extension_descriptor.py" "$API_HEADER" "$OUTPUT" "$TARGET_ABI"

if [[ -n "${EXTENSION_SIGNING_KEY:-}" ]]; then
    bash "$SCRIPT_DIR/sign-p4-extension.sh" "$OUTPUT" "${OUTPUT%.elf}.ext"
fi

echo "Built $OUTPUT (ABI $ABI_VERSION, target $TARGET_ABI)"