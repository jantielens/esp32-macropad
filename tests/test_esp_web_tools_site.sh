#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."
PROJECT_DIR="$PWD"

TMP_DIR=$(mktemp -d)
trap 'rm -rf "$TMP_DIR"' EXIT

source config.sh

create_partitions_bin() {
    local destination="$1"
    python3 - "$destination" <<'PY'
import struct
import sys

entry = struct.pack(
    "<HBBII16sI",
    0x50AA, 0x00, 0x10, 0x10000, 0x100000, b"app0\0", 0,
)
with open(sys.argv[1], "wb") as output:
    output.write(entry)
    output.write(b"\xff" * 32)
PY
}

mkdir -p "$TMP_DIR/build/extensions"

for board_name in "${!FQBN_TARGETS[@]}"; do
    if [[ "$board_name" == *beta* ]] || [[ "$board_name" == *experimental* ]]; then
        continue
    fi

    board_dir="$TMP_DIR/build/$board_name"
    mkdir -p "$board_dir"
    : > "$board_dir/app.ino.bin"
    : > "$board_dir/app.ino.bootloader.bin"
    : > "$board_dir/boot_app0.bin"
    create_partitions_bin "$board_dir/app.ino.partitions.bin"
done

while IFS= read -r source; do
    package_name="$(python3 tools/extension_package_name.py "$source")"
    : > "$TMP_DIR/build/extensions/${package_name%.elf}.ext"
done < <(grep -rl --include='*.cpp' 'native_extension_descriptor' "$PROJECT_DIR/extensions"/*/ | sort)

GITHUB_SHA=smoketest \
RELEASE_TAG=v0.0.0 \
RELEASE_NOTES_PATH="$TMP_DIR/release-notes.md" \
BUILD_DIR="$TMP_DIR/build" \
./tools/build-esp-web-tools-site.sh "$TMP_DIR/site"

test -f "$TMP_DIR/site/manifests/esp32-p4-lcd4b-voice.json"
grep -q 'ESP32-MP Voice Assistant' "$TMP_DIR/site/index.html"
find "$TMP_DIR/site/extensions" -maxdepth 1 -name '*.ext' -print -quit | grep -q .

echo "ESP Web Tools site smoke test passed"