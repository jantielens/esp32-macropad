#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."
PROJECT_DIR=$PWD

TMP_DIR=$(mktemp -d)
trap 'rm -rf "$TMP_DIR"' EXIT
ABI_VERSION=$(sed -n 's/^#define NATIVE_EXTENSION_ABI_VERSION \([0-9][0-9]*\)u$/\1/p' src/app/native_extension_api.h)
TARGET_ABI=$(sed -n 's/^#define NATIVE_EXTENSION_TARGET_ABI "\([^"]*\)"$/\1/p' src/app/native_extension_api.h)

build_package() {
    local source=$1
    local output=$2
    local log
    log=$(bash tools/build-p4-extension.sh "$source" "$output")
    grep -q "ABI $ABI_VERSION, target $TARGET_ABI" <<<"$log"
    "$HOME/.arduino15/packages/esp32/tools/esp-rv32/2511/bin/riscv32-esp-elf-readelf" -sW "$output" |
        grep -q '[[:space:]]native_extension_descriptor$'
    "$HOME/.arduino15/packages/esp32/tools/esp-rv32/2511/bin/riscv32-esp-elf-readelf" -sW "$output" |
        grep -q '[[:space:]]native_extension_shutdown$'
    ! "$HOME/.arduino15/packages/esp32/tools/esp-rv32/2511/bin/riscv32-esp-elf-readelf" -r "$output" |
        grep -q 'contains [1-9]'
}

while IFS= read -r source; do
    package_name=$(python3 tools/extension_package_name.py "$source")
    metadata="$(dirname "$source")/metadata.json"
    if [[ ! -f "$metadata" ]]; then
        echo "Missing catalog metadata: $metadata" >&2
        exit 1
    fi
    jq -e 'type == "object" and ((keys | sort) == ["summary", "usage"]) and (.summary | type == "string" and length > 0) and (.usage | type == "string" and length > 0)' "$metadata" >/dev/null
    build_package "$source" "$TMP_DIR/$package_name"
done < <(grep -rl --include='*.cpp' 'native_extension_descriptor' "$PROJECT_DIR/extensions"/*/)

echo "Native extension packages passed catalog metadata, ABI, target, descriptor, and relocation checks"
