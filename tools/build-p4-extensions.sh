#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
PROJECT_DIR=$(cd "$SCRIPT_DIR/.." && pwd)
OUTPUT_DIR=${1:-"$PROJECT_DIR/build/extensions"}
DEFAULT_SIGNING_KEY="$PROJECT_DIR/.secrets/extension-signing-private.pem"

mkdir -p "$OUTPUT_DIR"

if [[ -z "${EXTENSION_SIGNING_KEY:-}" ]]; then
    export EXTENSION_SIGNING_KEY="$DEFAULT_SIGNING_KEY"
fi
if [[ ! -f "$EXTENSION_SIGNING_KEY" ]]; then
    echo "Extension signing key not found: $EXTENSION_SIGNING_KEY" >&2
    echo "Set EXTENSION_SIGNING_KEY or generate the default key with:" >&2
    echo "  bash tools/generate-extension-signing-key.sh $DEFAULT_SIGNING_KEY" >&2
    exit 1
fi

found=0
while IFS= read -r source; do
    found=1
    package_name=$(python3 "$SCRIPT_DIR/extension_package_name.py" "$source")
    bash "$SCRIPT_DIR/build-p4-extension.sh" "$source" "$OUTPUT_DIR/$package_name"
done < <(grep -rl --include='*.cpp' 'native_extension_descriptor' "$PROJECT_DIR/extensions"/*/)

if [[ $found -eq 0 ]]; then
    echo "No native extension package sources found" >&2
    exit 1
fi

echo "Built native extensions in $OUTPUT_DIR"
