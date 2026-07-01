#!/bin/bash
# =============================================================================
# Asset bundler variant-matrix guard
# =============================================================================
# Validates the chunked-bundle variant generator in tools/minify-web-assets.sh
# (enumerate_bundle_variants) without building any board. The generator emits
# one #if-guarded PROGMEM blob per *reachable* combination of a bundle's chunk
# flags. Two correctness properties must hold for every board:
#
#   1. Exhaustiveness — every reachable flag assignment matches >= 1 branch
#      (otherwise the shared symbol is undefined -> link error).
#   2. Exclusivity    — every reachable flag assignment matches <= 1 branch
#      (otherwise the symbol is multiply-defined -> link error).
#
# "Reachable" encodes the device-class invariant: the IS_* flags are mutually
# exclusive (a board is exactly one device class), so at most one IS_* flag is
# ever true. Assignments with two-or-more IS_* flags true are impossible and
# must match ZERO branches (those combinations are intentionally not emitted).
#
# The test drives the REAL production function via the script's
# `--emit-variants` hook, brute-forces the full 2^N truth-table of flag
# assignments, evaluates each emitted #if expression against each assignment,
# and asserts the counts above. This proves the generated matrix is correct
# for every board, device-independently.

set -e
cd "$(dirname "$0")/.."

SCRIPT="tools/minify-web-assets.sh"

if [[ ! -x "$SCRIPT" && ! -f "$SCRIPT" ]]; then
    echo "FAIL: missing $SCRIPT" >&2
    exit 1
fi

PASS=0
FAIL=0

# Evaluate a single C-preprocessor #if expression of the form
#   "FLAG_A && !FLAG_B && FLAG_C"   (terms joined by &&, each optionally !-negated)
# against an associative array of flag truth values ($1 = expr, then assoc
# array name $2). Echoes 1 (true) or 0 (false).
eval_if_expr() {
    local expr="$1"
    local -n _vals_ref="$2"
    [[ "$expr" == "ALWAYS" ]] && { echo 1; return; }

    local term flag negate
    # Split on " && " into terms via a control-char delimiter.
    local delim=$'\x1f'
    expr="${expr// && /$delim}"
    local -a terms=()
    IFS="$delim" read -r -a terms <<< "$expr"

    for term in "${terms[@]}"; do
        negate=0
        flag="$term"
        if [[ "$flag" == "!"* ]]; then
            negate=1
            flag="${flag#!}"
        fi
        local v="${_vals_ref[$flag]:-0}"
        if [[ $negate -eq 1 ]]; then
            [[ "$v" == "1" ]] && { echo 0; return; }
        else
            [[ "$v" == "0" ]] && { echo 0; return; }
        fi
    done
    echo 1
}

# Run one scenario: given a list of flags, fetch the emitted variant matrix
# from the real script and verify exhaustiveness + exclusivity over the full
# truth table, plus the expected reachable variant count.
check_scenario() {
    local name="$1"; shift
    local -a flags=("$@")

    # Partition into independent vs device-class (IS_*) flags.
    local -a indep=() klass=()
    local f
    for f in "${flags[@]}"; do
        if [[ "$f" == IS_* ]]; then klass+=("$f"); else indep+=("$f"); fi
    done
    local k=${#indep[@]} c=${#klass[@]}
    local expected_variants=$(( (1 << k) * (c + 1) ))

    # Pull the emitted #if expressions (first tab-field) from the real function.
    local -a exprs=()
    local line expr rest
    while IFS=$'\t' read -r expr rest; do
        exprs+=("$expr")
    done < <(bash "$SCRIPT" --emit-variants "${flags[@]}")

    local got_variants=${#exprs[@]}
    if [[ $got_variants -ne $expected_variants ]]; then
        echo "FAIL [$name]: variant count $got_variants, expected $expected_variants" >&2
        FAIL=$((FAIL + 1))
        return
    fi

    # Brute-force the full 2^N assignment space (all flags independent here, so
    # we can also probe the "impossible" multi-class assignments).
    local n=${#flags[@]}
    local total=$((1 << n))
    local a i bit reachable_classes
    local ok=1
    for ((a = 0; a < total; a++)); do
        local -A assign=()
        local classes_on=0
        for ((i = 0; i < n; i++)); do
            bit=$(( (a >> i) & 1 ))
            assign["${flags[$i]}"]=$bit
            if [[ "${flags[$i]}" == IS_* && $bit -eq 1 ]]; then
                classes_on=$((classes_on + 1))
            fi
        done

        local matches=0 e
        for e in "${exprs[@]}"; do
            if [[ "$(eval_if_expr "$e" assign)" == "1" ]]; then
                matches=$((matches + 1))
            fi
        done

        if [[ $classes_on -le 1 ]]; then
            # Reachable assignment: exactly one branch must match.
            if [[ $matches -ne 1 ]]; then
                echo "FAIL [$name]: assignment $a (classes_on=$classes_on) matched $matches branches, expected 1" >&2
                ok=0
            fi
        else
            # Impossible assignment: no branch should match.
            if [[ $matches -ne 0 ]]; then
                echo "FAIL [$name]: impossible assignment $a (classes_on=$classes_on) matched $matches branches, expected 0" >&2
                ok=0
            fi
        fi
    done

    if [[ $ok -eq 1 ]]; then
        echo "PASS [$name]: $got_variants variants, exclusive + exhaustive over $total assignments"
        PASS=$((PASS + 1))
    else
        FAIL=$((FAIL + 1))
    fi
}

echo "=== Asset bundler variant-matrix guard ==="

# No flags -> single always-on variant.
check_scenario "no-flags"

# Independent flags only -> pure 2^k.
check_scenario "indep-only" HAS_DISPLAY HAS_MQTT

# Device-class group only -> (c + 1) branches.
check_scenario "classes-only" IS_SHUTTER_TESTER IS_COFFEE_SCALE IS_DARKROOM_TIMER

# The real portal.js set today: 2 independent + 3 device classes -> 16 variants
# (the case from issue #31; the old 2^N generator produced 32).
check_scenario "portal-js-today" HAS_DISPLAY HAS_EPAPER \
    IS_SHUTTER_TESTER IS_COFFEE_SCALE IS_DARKROOM_TIMER

# Forward-looking: a 4th device class would have tripped the old cap (6 flags
# -> 64 variants); the exclusive-group model keeps it at 2^2 * 5 = 20.
check_scenario "four-classes" HAS_DISPLAY HAS_EPAPER \
    IS_SHUTTER_TESTER IS_COFFEE_SCALE IS_DARKROOM_TIMER IS_FUTURE_CLASS

echo
if [[ $FAIL -ne 0 ]]; then
    echo "=== variant-matrix guard FAILED ($FAIL failure(s), $PASS passed) ===" >&2
    exit 1
fi
echo "=== variant-matrix guard passed ($PASS scenario(s)) ==="
