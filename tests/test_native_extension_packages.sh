#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."
PROJECT_DIR=$PWD

TMP_DIR=$(mktemp -d)
trap 'rm -rf "$TMP_DIR"' EXIT
SIGNING_KEY="$TMP_DIR/extension-signing-private.pem"
openssl ecparam -name prime256v1 -genkey -noout -out "$SIGNING_KEY"
export EXTENSION_SIGNING_KEY="$SIGNING_KEY"
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
    [[ $(wc -c < "${output%.elf}.ext") -eq $(($(wc -c < "$output") + 64)) ]]
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

first_package=$(find "$TMP_DIR" -maxdepth 1 -type f -name '*.ext' | head -n 1)
[[ -n "$first_package" ]]
cp "$first_package" "$TMP_DIR/tampered.ext"
printf '\x00' | dd of="$TMP_DIR/tampered.ext" bs=1 seek=16 conv=notrunc status=none
if cmp -s "$first_package" "$TMP_DIR/tampered.ext"; then
    echo "Tampered extension test did not modify the package" >&2
    exit 1
fi
if openssl dgst -sha256 -verify <(openssl ec -in "$SIGNING_KEY" -pubout 2>/dev/null) \
    -signature <(python3 - "$first_package" <<'PY'
import sys

raw = open(sys.argv[1], "rb").read()[-64:]
if len(raw) != 64:
    raise SystemExit(1)
def integer(value):
    value = value.lstrip(b"\0") or b"\0"
    if value[0] & 0x80:
        value = b"\0" + value
    return b"\x02" + bytes([len(value)]) + value
body = integer(raw[:32]) + integer(raw[32:])
sys.stdout.buffer.write(b"\x30" + bytes([len(body)]) + body)
PY
) <(head -c -64 "$TMP_DIR/tampered.ext") >/dev/null 2>&1; then
    echo "Tampered extension unexpectedly verified" >&2
    exit 1
fi

echo "Native extension packages passed catalog metadata, ABI, target, descriptor, relocation, and signed-package checks"
