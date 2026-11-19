#!/usr/bin/env bash
# Generate one portal asset set using the actual firmware compiler configuration.
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TERSER="$SCRIPT_DIR/node_modules/.bin/terser"
if [[ ! -x "$TERSER" ]]; then
    npm ci --prefix "$SCRIPT_DIR" --no-audit --no-fund
fi
if ! python3 -c 'import csscompressor' 2>/dev/null; then
    python3 -m pip install --user csscompressor
fi
exec python3 "$SCRIPT_DIR/portal_assets.py" "$@"
