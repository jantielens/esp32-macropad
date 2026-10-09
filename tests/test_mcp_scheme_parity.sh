#!/bin/bash
# =============================================================================
# Shared binding-schema completeness guard
# =============================================================================
# Every production [scheme:...] binding registered via binding_template_register()
# must provide registration-time BindingSchemeSpec metadata. The shared schema
# emitter powers both GET /api/bindings and the MCP capability manifest.
#
# This is a textual guard (no compilation): it scans src/app for register and
# legacy hooks. The build verifies each call has the required fourth argument.

set -e
cd "$(dirname "$0")/.."

SRC_DIR="src/app"

extract_schemes() {
    # $1 = function name to match. Prints sorted unique scheme names.
    grep -rhoE "${1}\\(\"[a-z_]+\"" "$SRC_DIR" 2>/dev/null \
        | sed -E 's/.*"([a-z_]+)".*/\1/' | sort -u
}

registered="$(extract_schemes binding_template_register)"

if [ -z "$registered" ]; then
    echo "FAIL: found no binding_template_register() calls under $SRC_DIR" >&2
    exit 1
fi

if rg -n "binding_template_(set_scheme|describe_scheme)" "$SRC_DIR" --glob '*.{cpp,h}' >/dev/null; then
    echo "FAIL: retired binding metadata hooks remain in production code" >&2
    exit 1
fi

# Every registration passes a BindingSchemeDoc so the portal binding reference
# and MCP manifest document it. Host profiles check the doc contents.
undocumented="$(python3 - "$SRC_DIR" <<'PY'
import pathlib, re, sys
for path in pathlib.Path(sys.argv[1]).rglob("*.cpp"):
    if "build" in path.parts:
        continue
    for match in re.finditer(r'binding_template_register\("([a-z_]+)".*?\);', path.read_text(), re.S):
        if not re.search(r'&k\w+Doc\}', match.group(0)):
            print(f"{path}: {match.group(1)}")
PY
)"
if [ -n "$undocumented" ]; then
    echo "FAIL: binding schemes registered without a BindingSchemeDoc:" >&2
    echo "$undocumented" >&2
    exit 1
fi

count="$(echo "$registered" | wc -w | tr -d ' ')"
echo "PASS: $count registered binding scheme(s) use shared registry metadata and docs."
