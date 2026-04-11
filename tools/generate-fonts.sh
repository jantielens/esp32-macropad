#!/bin/bash
# ============================================================================
# Custom Font Generator for LVGL
# ============================================================================
# Downloads TTF fonts and converts them to LVGL C source files using lv_font_conv.
#
# Usage:
#   ./tools/generate-fonts.sh              # Generate all fonts
#   ./tools/generate-fonts.sh --download   # Download TTFs only
#   ./tools/generate-fonts.sh --convert    # Convert existing TTFs only
#   ./tools/generate-fonts.sh --clean      # Remove generated C files
#
# Prerequisites:
#   - Node.js (for npx lv_font_conv)
#
# Adding a new font:
#   1. Add an entry to the FONTS array below
#   2. Run ./tools/generate-fonts.sh
#   3. Add extern declaration to src/app/fonts/custom_fonts.h
#   4. Add entry to pad_custom_font_lookup() in src/app/pad_layout.h
#   5. Rebuild firmware

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
FONTS_SRC_DIR="$PROJECT_DIR/assets/fonts"
FONTS_OUT_DIR="$PROJECT_DIR/src/app/fonts"

# Character set for display fonts: digits, upper/lower, common symbols
SYMBOLS='0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz.,:;-+/%°  '

# BPP (bits per pixel) for antialiasing quality. 4 = good quality, reasonable size.
BPP=4

# ============================================================================
# FONT CONFIGURATION
# ============================================================================
# Each entry: "output_name|ttf_filename|download_url|sizes"
#   output_name  - C symbol prefix (e.g., font_dseg7 → font_dseg7_48)
#   ttf_filename - Name of the TTF file in assets/fonts/
#   download_url - URL to download the TTF from
#   sizes        - Comma-separated pixel sizes to generate
#
# DSEG7 Classic Bold — 7-segment LCD display font
# https://github.com/keshikan/DSEG (SIL OFL 1.1)
#
# Bebas Neue — Bold condensed display font
# https://fonts.google.com/specimen/Bebas+Neue (SIL OFL 1.1)
#
# Doto — Dot-matrix display font (ExtraBold 800 weight)
# https://fonts.google.com/specimen/Doto (SIL OFL 1.1)

# Format: "output_name|ttf_filename|sizes|extra_args"
#   output_name - C symbol prefix (e.g., font_dseg7 → font_dseg7_48)
#   ttf_filename - Name of the TTF file in assets/fonts/
#   sizes        - Comma-separated pixel sizes to generate
#   extra_args   - Optional extra lv_font_conv args (e.g., --font-index for variable fonts)
FONTS=(
    "font_dseg7|DSEG7Classic-Bold.ttf|12,14,18,24,32,36,48|"
    "font_bebas|BebasNeue-Regular.ttf|12,14,18,24,32,36,48|"
    "font_doto|Doto[ROND,wght].ttf|12,14,18,24,32,36,48|"
)

# ============================================================================
# FUNCTIONS
# ============================================================================

download_fonts() {
    echo "=== Downloading font TTFs ==="
    mkdir -p "$FONTS_SRC_DIR"

    # --- DSEG7 Classic Bold (from GitHub release zip) ---
    local dseg_ttf="$FONTS_SRC_DIR/DSEG7Classic-Bold.ttf"
    if [[ -f "$dseg_ttf" ]]; then
        echo "  [skip] DSEG7Classic-Bold.ttf (already exists)"
    else
        echo "  [download] DSEG7Classic-Bold.ttf (from release zip)"
        local tmp_zip
        tmp_zip=$(mktemp /tmp/dseg-XXXXXX.zip)
        curl -fsSL -o "$tmp_zip" "https://github.com/keshikan/DSEG/releases/download/v0.46/fonts-DSEG_v046.zip" || {
            echo "  [ERROR] Failed to download DSEG release"; rm -f "$tmp_zip"; return 1
        }
        unzip -jo "$tmp_zip" "fonts-DSEG_v046/DSEG7-Classic/DSEG7Classic-Bold.ttf" -d "$FONTS_SRC_DIR" >/dev/null || {
            echo "  [ERROR] Failed to extract DSEG7Classic-Bold.ttf"; rm -f "$tmp_zip"; return 1
        }
        rm -f "$tmp_zip"
    fi

    # --- Bebas Neue (direct TTF from Google Fonts repo) ---
    local bebas_ttf="$FONTS_SRC_DIR/BebasNeue-Regular.ttf"
    if [[ -f "$bebas_ttf" ]]; then
        echo "  [skip] BebasNeue-Regular.ttf (already exists)"
    else
        echo "  [download] BebasNeue-Regular.ttf"
        curl -fsSL -o "$bebas_ttf" "https://github.com/google/fonts/raw/main/ofl/bebasneue/BebasNeue-Regular.ttf" || {
            echo "  [ERROR] Failed to download BebasNeue-Regular.ttf"; return 1
        }
    fi

    # --- Doto (variable font from Google Fonts repo) ---
    local doto_ttf="$FONTS_SRC_DIR/Doto[ROND,wght].ttf"
    if [[ -f "$doto_ttf" ]]; then
        echo "  [skip] Doto[ROND,wght].ttf (already exists)"
    else
        echo "  [download] Doto[ROND,wght].ttf (variable font)"
        curl -fsSL -o "$doto_ttf" "https://github.com/google/fonts/raw/main/ofl/doto/Doto%5BROND%2Cwght%5D.ttf" || {
            echo "  [ERROR] Failed to download Doto variable font"; return 1
        }
    fi

    echo ""
}

convert_fonts() {
    echo "=== Converting fonts to LVGL C source ==="
    mkdir -p "$FONTS_OUT_DIR"

    # Check lv_font_conv availability
    if ! command -v npx &>/dev/null; then
        echo "ERROR: npx not found. Install Node.js first."
        return 1
    fi

    for entry in "${FONTS[@]}"; do
        IFS='|' read -r name ttf_file sizes extra_args <<< "$entry"
        local ttf_path="$FONTS_SRC_DIR/$ttf_file"

        if [[ ! -f "$ttf_path" ]]; then
            echo "  [ERROR] TTF not found: $ttf_path (run with --download first)"
            return 1
        fi

        IFS=',' read -ra size_arr <<< "$sizes"
        for sz in "${size_arr[@]}"; do
            local out_name="${name}_${sz}"
            local out_c="$FONTS_OUT_DIR/${out_name}.c"

            echo "  [convert] ${out_name} (${sz}px, ${BPP}bpp)"

            # Build command args
            local cmd_args=(
                --font "$ttf_path"
                --size "$sz"
                --bpp "$BPP"
                --format lvgl
                --symbols "$SYMBOLS"
                --output "$out_c"
                --lv-include "lvgl.h"
                --no-compress
            )

            # Append extra args if present (e.g., for variable fonts)
            if [[ -n "${extra_args:-}" ]]; then
                # shellcheck disable=SC2206
                cmd_args+=($extra_args)
            fi

            npx lv_font_conv "${cmd_args[@]}" 2>/dev/null || {
                echo "  [ERROR] Failed to convert ${out_name}"
                return 1
            }

            # Post-process: prefix internal symbols with the font name to avoid
            # collisions when multiple font .c files are included in one TU.
            # Only rename standalone identifiers — not LVGL type names, function
            # names, or struct member names (preceded by '.').
            local p="${out_name}_"  # e.g., "font_dseg7_48_"
            sed -i \
                -e "s/\([^._a-zA-Z0-9]\)glyph_bitmap/\1${p}glyph_bitmap/g" \
                -e "s/^glyph_bitmap/${p}glyph_bitmap/" \
                -e "s/\([^._a-zA-Z0-9]\)glyph_dsc\b/\1${p}glyph_dsc/g" \
                -e "s/^glyph_dsc/${p}glyph_dsc/" \
                -e "s/\([^._a-zA-Z0-9]\)glyph_id_ofs_list_/\1${p}glyph_id_ofs_list_/g" \
                -e "s/^glyph_id_ofs_list_/${p}glyph_id_ofs_list_/" \
                -e "s/\([^._a-zA-Z0-9]\)unicode_list_/\1${p}unicode_list_/g" \
                -e "s/^unicode_list_/${p}unicode_list_/" \
                -e "s/\([^._a-zA-Z0-9]\)cmaps\b/\1${p}cmaps/g" \
                -e "s/^cmaps/${p}cmaps/" \
                -e "s/\([^._a-zA-Z0-9]\)font_dsc\b/\1${p}font_dsc/g" \
                -e "s/^font_dsc/${p}font_dsc/" \
                -e "s/\([^._a-zA-Z0-9]\)kern_left_class_mapping/\1${p}kern_left_class_mapping/g" \
                -e "s/\([^._a-zA-Z0-9]\)kern_right_class_mapping/\1${p}kern_right_class_mapping/g" \
                -e "s/\([^._a-zA-Z0-9]\)kern_class_values/\1${p}kern_class_values/g" \
                -e "s/\([^._a-zA-Z0-9]\)kern_classes\b/\1${p}kern_classes/g" \
                -e "s/\([^._a-zA-Z0-9]\)kern_pair_glyph_ids/\1${p}kern_pair_glyph_ids/g" \
                -e "s/\([^._a-zA-Z0-9]\)kern_pair_values\b/\1${p}kern_pair_values/g" \
                -e "s/\([^._a-zA-Z0-9]\)kern_pairs\b/\1${p}kern_pairs/g" \
                "$out_c"

            # Report file size
            local fsize
            fsize=$(wc -c < "$out_c")
            echo "           → ${out_c##*/} ($(( fsize / 1024 )) KB)"
        done
    done
    echo ""
}

clean_fonts() {
    echo "=== Cleaning generated font files ==="
    rm -f "$FONTS_OUT_DIR"/font_*.c
    echo "  Done."
}

show_summary() {
    echo "=== Font Summary ==="
    local total=0
    for f in "$FONTS_OUT_DIR"/font_*.c; do
        [[ -f "$f" ]] || continue
        local sz
        sz=$(wc -c < "$f")
        total=$((total + sz))
        printf "  %-30s %6d bytes (%d KB)\n" "$(basename "$f")" "$sz" "$((sz / 1024))"
    done
    echo "  ────────────────────────────────────────"
    printf "  %-30s %6d bytes (%d KB)\n" "TOTAL" "$total" "$((total / 1024))"
    echo ""
}

# ============================================================================
# MAIN
# ============================================================================

case "${1:-all}" in
    --download)
        download_fonts
        ;;
    --convert)
        convert_fonts
        show_summary
        ;;
    --clean)
        clean_fonts
        ;;
    all|--all)
        download_fonts
        convert_fonts
        show_summary
        ;;
    *)
        echo "Usage: $0 [--download|--convert|--clean|--all]"
        exit 1
        ;;
esac
