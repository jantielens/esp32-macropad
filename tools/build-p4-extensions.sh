#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
PROJECT_DIR=$(cd "$SCRIPT_DIR/.." && pwd)
OUTPUT_DIR=${1:-"$PROJECT_DIR/build/extensions"}

mkdir -p "$OUTPUT_DIR"

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
