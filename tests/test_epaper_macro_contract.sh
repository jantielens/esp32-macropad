#!/bin/bash
# =============================================================================
# E-paper taxonomy macro contract guard
# =============================================================================

set -euo pipefail
cd "$(dirname "$0")/.."

fail_if_present() {
    local pattern=$1
    local description=$2
    if rg -n --glob '!build/**' --glob '!src/app/web_assets.h' "$pattern" src config.sh *.sh; then
        echo "FAIL: retired e-paper macro name found: $description" >&2
        exit 1
    fi
}

fail_if_present '\bHAS_EPAPER\b' 'HAS_EPAPER'
fail_if_present '\bIS_EPAPER\b' 'IS_EPAPER'
fail_if_present '\bHAS_EPAPER_WAKE_BUTTON\b' 'HAS_EPAPER_WAKE_BUTTON'
fail_if_present '\bBOARD_RETERMINAL_E1003\b' 'BOARD_RETERMINAL_E1003'
fail_if_present '\bEPAPER_(BATTERY|SD|BUTTON|VCOM|FAST_REFRESH|PIN)_[A-Z0-9_]*\b' 'unscoped E-Paper Frame hardware macro'

# The interactive LVGL panel keeps its established presentation macros. Every
# other E-paper macro belongs to the E-Paper Frame contract and must be scoped.
unexpected="$(rg -o --glob '!build/**' --glob '!src/app/web_assets.h' '\bEPAPER_[A-Z][A-Z0-9_]*\b' src config.sh *.sh \
    | sed 's/.*://' \
    | sort -u \
    | rg -v '^(EPAPER_FRAME($|_)|EPAPER_(BINDING|BW|DEFAULT|MIN_REFRESH|REFRESH|RENDER)_)' || true)"
if [[ -n "$unexpected" ]]; then
    echo "FAIL: unexpected unscoped E-Paper macro(s):" >&2
    echo "$unexpected" >&2
    exit 1
fi

echo "PASS: E-paper macro taxonomy is canonical."