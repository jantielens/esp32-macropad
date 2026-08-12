#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

TMP_DIR=$(mktemp -d)
trap 'rm -rf "$TMP_DIR"' EXIT

build_package() {
    local source=$1
    local output=$2
    local log
    log=$(bash tools/build-p4-extension.sh "$source" "$output")
    grep -q "ABI 7, target rv32imafc-ilp32f" <<<"$log"
    "$HOME/.arduino15/packages/esp32/tools/esp-rv32/2511/bin/riscv32-esp-elf-readelf" -sW "$output" |
        grep -q '[[:space:]]native_extension_descriptor$'
    ! "$HOME/.arduino15/packages/esp32/tools/esp-rv32/2511/bin/riscv32-esp-elf-readelf" -r "$output" |
        grep -q 'contains [1-9]'
}

build_package extensions/hello-world/hello_world.cpp "$TMP_DIR/hello-world@1.0.0.elf"
build_package extensions/advanced-sample/advanced_sample.cpp "$TMP_DIR/advanced-sample@1.0.0.elf"
build_package extensions/flight-radar/flight_radar.cpp "$TMP_DIR/flight-radar@1.1.0.elf"

echo "Native extension packages passed ABI, target, descriptor, and relocation checks"
