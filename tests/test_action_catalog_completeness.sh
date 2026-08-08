#!/bin/bash
# =============================================================================
# Action catalog completeness guard
# =============================================================================
# action_catalog.cpp (src/app/action_catalog.h) is the single source of action
# presentation metadata shared by the web portal (GET /api/info?catalog=1) and
# the MCP capability manifest (get_capabilities). Because there is only one
# table, there is nothing for a second copy to drift from — the failure mode
# this guard exists to catch is a forgotten entry: a built-in type handled in
# the action_parse.cpp / action_dispatch.cpp strcmp ladder, or a device-class
# type registered via REGISTER_ACTION_TYPE, that has no catalog metadata and
# is therefore invisible to both the portal picker and MCP pad authoring.
#
# This is a textual guard (no compilation): it scans src/app for type ladders,
# registrations, and catalog entries, and diffs the resulting sets.

set -e
cd "$(dirname "$0")/.."

SRC_DIR="src/app"
CATALOG="$SRC_DIR/action_catalog.cpp"
PARSE="$SRC_DIR/action_parse.cpp"
DISPATCH="$SRC_DIR/action_dispatch.cpp"

fail=0

# ---------------------------------------------------------------------------
# 1. Built-in types: every ACTION_TYPE_* used in the parse/dispatch ladders
#    must have a matching add_action(actions, "value", ...) call in the
#    catalog. ACTION_TYPE_NONE ("") is the empty-action sentinel, not a real
#    type, and is excluded.
# ---------------------------------------------------------------------------

# Macro name -> string value, from every "#define ACTION_TYPE_X "value"" in src/app.
declare -A MACRO_VALUE
while IFS=' ' read -r macro value; do
    [ -z "$macro" ] && continue
    MACRO_VALUE["$macro"]="$value"
done < <(grep -rhoE '#define ACTION_TYPE_[A-Z_]+ *"[a-z_]*"' "$SRC_DIR" 2>/dev/null \
    | sed -E 's/#define +(ACTION_TYPE_[A-Z_]+) *"([a-z_]*)"/\1 \2/')

if [ "${#MACRO_VALUE[@]}" -eq 0 ]; then
    echo "FAIL: found no '#define ACTION_TYPE_*' declarations under $SRC_DIR" >&2
    exit 1
fi

ladder_macros="$(grep -ohE 'ACTION_TYPE_[A-Z_]+' "$PARSE" "$DISPATCH" 2>/dev/null | sort -u)"
if [ -z "$ladder_macros" ]; then
    echo "FAIL: found no ACTION_TYPE_* references in $PARSE / $DISPATCH" >&2
    exit 1
fi

catalog_types="$(grep -ohE 'add_action\(actions, *"[a-z_]+"' "$CATALOG" 2>/dev/null \
    | sed -E 's/.*"([a-z_]+)"/\1/' | sort -u)"
if [ -z "$catalog_types" ]; then
    echo "FAIL: found no add_action(...) calls in $CATALOG" >&2
    exit 1
fi

missing_builtins=""
for macro in $ladder_macros; do
    [ "$macro" = "ACTION_TYPE_NONE" ] && continue
    value="${MACRO_VALUE[$macro]:-}"
    if [ -z "$value" ]; then
        fail=1
        echo "FAIL: $macro is used in the action ladder but has no '#define $macro \"...\"'" >&2
        continue
    fi
    if ! echo "$catalog_types" | grep -qx "$value"; then
        missing_builtins="$missing_builtins $value"
    fi
done
if [ -n "$missing_builtins" ]; then
    fail=1
    echo "FAIL: built-in action type(s) in the parse/dispatch ladder have no catalog entry:" >&2
    for t in $missing_builtins; do
        echo "  - $t  -> add add_action(actions, \"$t\", <group>, <label>) in $CATALOG" >&2
    done
fi

# ---------------------------------------------------------------------------
# 2. Device-class types: every REGISTER_ACTION_TYPE(...) must sit in a file
#    that also assigns a non-null describe hook, so action_catalog_emit's
#    registry loop can present it.
# ---------------------------------------------------------------------------

files_with_registration="$(grep -rl 'REGISTER_ACTION_TYPE(' "$SRC_DIR" 2>/dev/null \
    | grep -v '/action_registry\.h$' || true)"
if [ -z "$files_with_registration" ]; then
    echo "FAIL: found no REGISTER_ACTION_TYPE(...) calls under $SRC_DIR" >&2
    exit 1
fi

missing_describe=""
for f in $files_with_registration; do
    if ! grep -qE '/\*[[:space:]]*describe[[:space:]]*\*/[[:space:]]*[A-Za-z_][A-Za-z0-9_]*,' "$f"; then
        missing_describe="$missing_describe $f"
    fi
done
if [ -n "$missing_describe" ]; then
    fail=1
    echo "FAIL: device-class action type(s) registered without a describe hook:" >&2
    for f in $missing_describe; do
        echo "  - $f  -> add a '<type>_describe(JsonObject&)' function and wire it into" >&2
        echo "           the ActionTypeDef's '/* describe    */' field" >&2
    done
fi

if [ "$fail" -ne 0 ]; then
    exit 1
fi

# ---------------------------------------------------------------------------
# 3. Every built-in entry documents at least one flat field for MCP clients,
#    or is explicitly listed below as genuinely fieldless (no JSON keys beyond
#    {type}). Catches a type added without any add_field(...) call by mistake.
# ---------------------------------------------------------------------------

FIELDLESS_BUILTINS="back ble_pair"

missing_fields=""
current_type=""
has_field=0
flush_current() {
    if [ -n "$current_type" ] && [ "$has_field" -eq 0 ]; then
        if ! echo " $FIELDLESS_BUILTINS " | grep -q " $current_type "; then
            missing_fields="$missing_fields $current_type"
        fi
    fi
}
while IFS= read -r line; do
    if echo "$line" | grep -qE 'add_action\(actions, *"[a-z_]+"'; then
        flush_current
        current_type="$(echo "$line" | grep -oE '"[a-z_]+"' | head -1 | tr -d '"')"
        has_field=0
    elif echo "$line" | grep -qE 'add_field\('; then
        has_field=1
    fi
done < "$CATALOG"
flush_current

if [ -n "$missing_fields" ]; then
    fail=1
    echo "FAIL: built-in action type(s) have no documented fields and are not in FIELDLESS_BUILTINS:" >&2
    for t in $missing_fields; do
        echo "  - $t  -> add an add_field(a, include_field_docs, \"name\", \"description\") call," >&2
        echo "         or add \"$t\" to FIELDLESS_BUILTINS in $0 if it truly has none" >&2
    done
fi

if [ "$fail" -ne 0 ]; then
    exit 1
fi

builtin_count="$(echo "$catalog_types" | wc -w | tr -d ' ')"
class_count="$(echo "$files_with_registration" | wc -l | tr -d ' ')"
echo "PASS: $builtin_count built-in and $class_count device-class action type(s) have catalog metadata."
