#!/bin/bash
# =============================================================================
# MCP binding-manifest parity guard
# =============================================================================
# Every production [scheme:...] binding registered via binding_template_register()
# should also expose an MCP describe hook via binding_template_set_scheme_describe().
# The MCP pad-authoring manifest (mcp_tools_pads.cpp :: get_capabilities) enumerates
# schemes from the live registry and calls each scheme's describe hook, so an LLM
# client can discover and use the binding when authoring pads. A scheme registered
# WITHOUT a describe hook is silently invisible to AI pad authoring — exactly the
# "we forgot to update the MCP server" mistake this guard exists to catch.
#
# This is a textual guard (no compilation): it scans src/app for register and
# describe calls and diffs the scheme-name sets.
#
# KNOWN DEBT: a small allowlist covers schemes that predate the describe hook.
# Shrink this list over time; do not grow it without a review justification.
# The guard also fails if an allowlisted scheme is removed or since gained a
# describe hook, so the debt list stays honest and self-cleaning.

set -e
cd "$(dirname "$0")/.."

SRC_DIR="src/app"

# Schemes intentionally without an MCP describe hook (device-class schemes that
# predate the mechanism). Space-separated. SHRINK, don't grow.
ALLOWLIST=""

extract_schemes() {
    # $1 = function name to match. Prints sorted unique scheme names.
    grep -rhoE "${1}\\(\"[a-z_]+\"" "$SRC_DIR" 2>/dev/null \
        | sed -E 's/.*"([a-z_]+)".*/\1/' | sort -u
}

registered="$(extract_schemes binding_template_register)"
described="$(extract_schemes binding_template_set_scheme_describe)"

if [ -z "$registered" ]; then
    echo "FAIL: found no binding_template_register() calls under $SRC_DIR" >&2
    exit 1
fi

fail=0

# 1. Every registered scheme must be described or explicitly allowlisted.
missing=""
for s in $registered; do
    if echo "$described" | grep -qx "$s"; then continue; fi
    if echo " $ALLOWLIST " | grep -q " $s "; then continue; fi
    missing="$missing $s"
done
if [ -n "$missing" ]; then
    fail=1
    echo "FAIL: binding scheme(s) registered but missing an MCP describe hook:" >&2
    for s in $missing; do
        echo "  - $s  → add binding_template_set_scheme_describe(\"$s\", ...) in its" >&2
        echo "         *_binding.cpp init (guard with #if HAS_MCP), so MCP pad" >&2
        echo "         authoring clients can discover it." >&2
    done
    echo "" >&2
    echo "If a scheme is intentionally hidden from MCP, add it to ALLOWLIST in" >&2
    echo "$0 with a justification." >&2
fi

# 2. Allowlist hygiene: no stale entries.
for s in $ALLOWLIST; do
    if ! echo "$registered" | grep -qx "$s"; then
        fail=1
        echo "FAIL: ALLOWLIST entry '$s' is not a registered scheme — remove it." >&2
    elif echo "$described" | grep -qx "$s"; then
        fail=1
        echo "FAIL: ALLOWLIST entry '$s' now HAS a describe hook — remove it from ALLOWLIST." >&2
    fi
done

if [ "$fail" -ne 0 ]; then
    exit 1
fi

count="$(echo "$registered" | wc -w | tr -d ' ')"
echo "PASS: $count registered binding scheme(s) are MCP-discoverable (or allowlisted)."
