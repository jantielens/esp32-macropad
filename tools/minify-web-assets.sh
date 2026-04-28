#!/usr/bin/env bash
#
# Minify Web Assets and Generate web_assets.h
#
# This script dynamically discovers HTML, CSS, and JavaScript files in src/app/web/
# and generates a C header file with embedded assets for the ESP32 web server.
# CSS and JS files are minified; HTML files are processed with template substitution.
#
# Usage: ./tools/minify-web-assets.sh <PROJECT_NAME> <PROJECT_DISPLAY_NAME>
#

set -e  # Exit on error

# Resolve script directory for absolute paths
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

# Accept project name arguments
PROJECT_NAME="${1:-esp32-template}"
PROJECT_DISPLAY_NAME="${2:-ESP32 Template}"

# Escape strings for safe use in C string literals
escape_c_string() {
    local s="$1"
    s="${s//\\/\\\\}"
    s="${s//\"/\\\"}"
    printf '%s' "$s"
}

PROJECT_NAME_C="$(escape_c_string "$PROJECT_NAME")"
PROJECT_DISPLAY_NAME_C="$(escape_c_string "$PROJECT_DISPLAY_NAME")"

# Source and output paths
WEB_DIR="$PROJECT_ROOT/src/app/web"
OUTPUT_FILE="$PROJECT_ROOT/src/app/web_assets.h"
BRANDING_FILE="$PROJECT_ROOT/src/app/project_branding.h"
REPO_SLUG_FILE="$PROJECT_ROOT/src/app/repo_slug_config.h"

echo "=== Web Assets Minification ==="
echo "Project root:         $PROJECT_ROOT"
echo "Project name:         $PROJECT_NAME"
echo "Project display name: $PROJECT_DISPLAY_NAME"
echo "Web sources:          $WEB_DIR"
echo "Output:               $OUTPUT_FILE"
echo "Branding header:      $BRANDING_FILE"
echo "Repo slug header:     $REPO_SLUG_FILE"
echo

# ---------------------------------------------------------------------------
# Repository slug (auto-detect from git remote)
# ---------------------------------------------------------------------------
# This template repository needs to know which GitHub repository it was built
# from in order to construct GitHub Pages URLs at runtime.
GITHUB_OWNER=""
GITHUB_REPO=""

origin_url=""
if command -v git >/dev/null 2>&1; then
    origin_url=$(git -C "$PROJECT_ROOT" config --get remote.origin.url 2>/dev/null || true)
fi

if [[ -n "$origin_url" ]]; then
    # Normalize common GitHub remote URL shapes:
    # - https://github.com/<owner>/<repo>.git
    # - https://github.com/<owner>/<repo>
    # - git@github.com:<owner>/<repo>.git
    # - ssh://git@github.com/<owner>/<repo>.git
    if [[ "$origin_url" =~ github\.com[:/]+([^/]+)/([^/]+)$ ]]; then
        GITHUB_OWNER="${BASH_REMATCH[1]}"
        GITHUB_REPO="${BASH_REMATCH[2]}"
        GITHUB_REPO="${GITHUB_REPO%.git}"

    fi
fi

if [[ -n "$GITHUB_OWNER" && -n "$GITHUB_REPO" ]]; then
    echo "Repo slug:            $GITHUB_OWNER/$GITHUB_REPO"
else
    echo "Repo slug:            (not detected; GitHub Pages links may be unavailable)"
fi

# Check if web directory exists
if [ ! -d "$WEB_DIR" ]; then
    echo "Error: Web directory not found: $WEB_DIR"
    exit 1
fi

# Discover source files (exclude template fragments starting with _ and *.fragment.html)
HTML_FILES=($(find "$WEB_DIR" -maxdepth 1 -name "*.html" -not -name "_*.html" -not -name "*.fragment.html" -type f | sort))
FRAGMENT_FILES=($(find "$WEB_DIR" -maxdepth 1 -name "*.fragment.html" -type f | sort))
CSS_FILES=($(find "$WEB_DIR" -maxdepth 1 -name "*.css" -not -name "_*.css" -type f | sort))
JS_FILES=($(find "$WEB_DIR" -maxdepth 1 -name "*.js" -not -name "_*.js" -type f | sort))

# ---- Bundle support: detect JS bundle manifests ----
# A .js.bundle file lists fragment files that should be concatenated into the
# primary file (last entry) during minification.  Fragment files are excluded
# from individual processing so they only appear in the bundled output.
declare -A JS_SKIP_FILES  # fragment files to exclude from individual processing

for bundle_manifest in $(find "$WEB_DIR" -maxdepth 1 -name "*.js.bundle" -type f 2>/dev/null); do
    primary_name=$(basename "$bundle_manifest" .bundle)  # e.g. portal_pad_editor.js
    bundle_missing=0
    while IFS= read -r bline || [[ -n "$bline" ]]; do
        bline="${bline%%#*}"                     # strip comments
        bline="$(echo "$bline" | xargs)"         # trim whitespace
        [[ -z "$bline" ]] && continue
        # Validate that the listed file exists
        if [[ ! -f "$WEB_DIR/$bline" ]]; then
            echo "  Error: $(basename "$bundle_manifest") references missing file: $bline"
            bundle_missing=$((bundle_missing + 1))
        fi
        [[ "$bline" = "$primary_name" ]] && continue  # don't skip the primary
        JS_SKIP_FILES["$WEB_DIR/$bline"]=1
    done < "$bundle_manifest"
    if [[ $bundle_missing -gt 0 ]]; then
        echo "Error: $bundle_missing missing file(s) in bundle manifest $(basename "$bundle_manifest")"
        exit 1
    fi
done

# Filter out JS files that are fragments of a bundle
JS_FILES_FILTERED=()
for f in "${JS_FILES[@]}"; do
    [[ -n "${JS_SKIP_FILES[$f]}" ]] && continue
    JS_FILES_FILTERED+=("$f")
done
JS_FILES=("${JS_FILES_FILTERED[@]}")

# Read template fragments for HTML processing
HEADER_TEMPLATE=""
NAV_TEMPLATE=""
FOOTER_TEMPLATE=""
BINDING_HELP_TEMPLATE=""
WIDGET_BAR_CHART_TEMPLATE=""
WIDGET_GAUGE_TEMPLATE=""
WIDGET_SPARKLINE_TEMPLATE=""
WIDGET_TABLE_TEMPLATE=""
WIDGET_ROCKER_TEMPLATE=""
WIDGET_NUMERICROCKER_TEMPLATE=""
STYLE_HELP_TEMPLATE=""
HEALTH_WIDGET_TEMPLATE=""
REBOOT_OVERLAY_TEMPLATE=""

if [ -f "$WEB_DIR/_header.html" ]; then
    HEADER_TEMPLATE=$(cat "$WEB_DIR/_header.html")
fi

if [ -f "$WEB_DIR/_nav.html" ]; then
    NAV_TEMPLATE=$(cat "$WEB_DIR/_nav.html")
fi

if [ -f "$WEB_DIR/_footer.html" ]; then
    FOOTER_TEMPLATE=$(cat "$WEB_DIR/_footer.html")
fi

if [ -f "$WEB_DIR/_binding_help.html" ]; then
    BINDING_HELP_TEMPLATE=$(cat "$WEB_DIR/_binding_help.html")
fi

if [ -f "$WEB_DIR/_widget_bar_chart.html" ]; then
    WIDGET_BAR_CHART_TEMPLATE=$(cat "$WEB_DIR/_widget_bar_chart.html")
fi

if [ -f "$WEB_DIR/_widget_gauge.html" ]; then
    WIDGET_GAUGE_TEMPLATE=$(cat "$WEB_DIR/_widget_gauge.html")
fi

if [ -f "$WEB_DIR/_widget_sparkline.html" ]; then
    WIDGET_SPARKLINE_TEMPLATE=$(cat "$WEB_DIR/_widget_sparkline.html")
fi

if [ -f "$WEB_DIR/_widget_table.html" ]; then
    WIDGET_TABLE_TEMPLATE=$(cat "$WEB_DIR/_widget_table.html")
fi

if [ -f "$WEB_DIR/_widget_rocker.html" ]; then
    WIDGET_ROCKER_TEMPLATE=$(cat "$WEB_DIR/_widget_rocker.html")
fi

if [ -f "$WEB_DIR/_widget_numericrocker.html" ]; then
    WIDGET_NUMERICROCKER_TEMPLATE=$(cat "$WEB_DIR/_widget_numericrocker.html")
fi

if [ -f "$WEB_DIR/_style_help.html" ]; then
    STYLE_HELP_TEMPLATE=$(cat "$WEB_DIR/_style_help.html")
fi

if [ -f "$WEB_DIR/_health_widget.html" ]; then
    HEALTH_WIDGET_TEMPLATE=$(cat "$WEB_DIR/_health_widget.html")
fi

if [ -f "$WEB_DIR/_reboot_overlay.html" ]; then
    REBOOT_OVERLAY_TEMPLATE=$(cat "$WEB_DIR/_reboot_overlay.html")
fi

if [ ${#HTML_FILES[@]} -eq 0 ] && [ ${#FRAGMENT_FILES[@]} -eq 0 ] && [ ${#CSS_FILES[@]} -eq 0 ] && [ ${#JS_FILES[@]} -eq 0 ]; then
    echo "Error: No HTML, CSS, or JS files found in $WEB_DIR"
    exit 1
fi

echo "Found files:"
echo "  HTML:      ${#HTML_FILES[@]} file(s)"
echo "  Fragments: ${#FRAGMENT_FILES[@]} file(s)"
echo "  CSS:       ${#CSS_FILES[@]} file(s)"
echo "  JS:        ${#JS_FILES[@]} file(s)"
echo

# ---------------------------------------------------------------------------
# JavaScript syntax validation
# ---------------------------------------------------------------------------
# Check all JS source files for syntax errors using Node.js. This catches
# missing braces, unterminated strings, and other parse errors early, before
# minification where error messages are less helpful.

if command -v node &> /dev/null; then
    echo "Validating JavaScript syntax..."
    js_syntax_errors=0
    for js_check_file in $(find "$WEB_DIR" -maxdepth 1 -name "*.js" -type f | sort); do
        if ! node --check "$js_check_file" 2>/dev/null; then
            echo "  ✗ $(basename "$js_check_file"):"
            node --check "$js_check_file" 2>&1 | sed 's/^/    /'
            js_syntax_errors=$((js_syntax_errors + 1))
        fi
    done
    if [[ $js_syntax_errors -gt 0 ]]; then
        echo
        echo "Error: $js_syntax_errors JavaScript file(s) have syntax errors"
        exit 1
    fi
    echo "  ✓ All JavaScript files passed syntax check"
    echo
else
    echo "Note: node not found — skipping JavaScript syntax validation"
    echo
fi

# Check for Python 3
if ! command -v python3 &> /dev/null; then
    echo "Error: python3 is required but not installed"
    exit 1
fi

# Check/install required Python packages
if [ ${#HTML_FILES[@]} -gt 0 ] || [ ${#CSS_FILES[@]} -gt 0 ] || [ ${#JS_FILES[@]} -gt 0 ]; then
    echo "Checking Python dependencies..."
    
    if [ ${#CSS_FILES[@]} -gt 0 ] && ! python3 -c "import csscompressor" 2>/dev/null; then
        echo "Installing csscompressor..."
        python3 -m pip install --user csscompressor
    fi
    
    if [ ${#JS_FILES[@]} -gt 0 ] && ! python3 -c "import rjsmin" 2>/dev/null; then
        echo "Installing rjsmin..."
        python3 -m pip install --user rjsmin
    fi
    echo
fi

# Helper function to format bytes with percentage
format_size_savings() {
    local original=$1
    local processed=$2
    local type=$3
    
    if [ $original -eq $processed ]; then
        echo "  $type: $processed bytes (no compression)"
    else
        local saved=$((original - processed))
        local percent=$((saved * 100 / original))
        echo "  $type: $original → $processed bytes (saved $saved bytes, -${percent}%)"
    fi
}

# Arrays to store processed content and statistics
declare -A HTML_CONTENTS
declare -A HTML_GZIP_CONTENTS
declare -A FRAGMENT_CONTENTS
declare -A FRAGMENT_GZIP_CONTENTS
declare -A CSS_CONTENTS
declare -A CSS_GZIP_CONTENTS
declare -A JS_CONTENTS
declare -A JS_GZIP_CONTENTS
declare -A ORIGINAL_SIZES
declare -A PROCESSED_SIZES
declare -A GZIPPED_SIZES

# Helper function to gzip content and generate C byte array
gzip_to_c_array() {
    local content="$1"
    local temp_file=$(mktemp)
    local temp_gz=$(mktemp)
    
    # Write content to temp file and gzip it
    echo -n "$content" > "$temp_file"
    gzip -9 -c "$temp_file" > "$temp_gz"
    
    # Convert to C byte array format
    xxd -i < "$temp_gz" | grep -v "unsigned" | sed 's/^  //'
    
    # Cleanup
    rm -f "$temp_file" "$temp_gz"
}

# Process HTML files (template substitution + minification)
for html_file in "${HTML_FILES[@]}"; do
    filename=$(basename "$html_file" .html)
    echo "Processing HTML: $filename.html..."
    content=$(cat "$html_file")
    original_size=$(echo -n "$content" | wc -c)
    
    # Template substitution and minification
    minified=$(python3 -c "
import re
import sys

# Read template fragments from environment or files
header_template = '''$HEADER_TEMPLATE'''
nav_template = '''$NAV_TEMPLATE'''
footer_template = '''$FOOTER_TEMPLATE'''
binding_help_template = '''$BINDING_HELP_TEMPLATE'''
widget_bar_chart_template = '''$WIDGET_BAR_CHART_TEMPLATE'''
widget_gauge_template = '''$WIDGET_GAUGE_TEMPLATE'''
widget_sparkline_template = '''$WIDGET_SPARKLINE_TEMPLATE'''
widget_table_template = '''$WIDGET_TABLE_TEMPLATE'''
widget_rocker_template = '''$WIDGET_ROCKER_TEMPLATE'''
widget_numericrocker_template = '''$WIDGET_NUMERICROCKER_TEMPLATE'''
style_help_template = '''$STYLE_HELP_TEMPLATE'''
health_widget_template = '''$HEALTH_WIDGET_TEMPLATE'''
reboot_overlay_template = '''$REBOOT_OVERLAY_TEMPLATE'''

with open('$html_file', 'r') as f:
    html = f.read()
    
    # Replace template placeholders with actual content
    html = html.replace('{{HEADER}}', header_template)
    html = html.replace('{{NAV}}', nav_template)
    html = html.replace('{{FOOTER}}', footer_template)
    html = html.replace('{{BINDING_HELP}}', binding_help_template)
    html = html.replace('{{WIDGET_BAR_CHART}}', widget_bar_chart_template)
    html = html.replace('{{WIDGET_GAUGE}}', widget_gauge_template)
    html = html.replace('{{WIDGET_SPARKLINE}}', widget_sparkline_template)
    html = html.replace('{{WIDGET_TABLE}}', widget_table_template)
    html = html.replace('{{WIDGET_ROCKER}}', widget_rocker_template)
    html = html.replace('{{WIDGET_NUMERICROCKER}}', widget_numericrocker_template)
    html = html.replace('{{STYLE_HELP}}', style_help_template)
    html = html.replace('{{HEALTH_WIDGET}}', health_widget_template)
    html = html.replace('{{REBOOT_OVERLAY}}', reboot_overlay_template)
    
    # Project name substitution
    html = html.replace('{{PROJECT_NAME}}', '$PROJECT_NAME')
    html = html.replace('{{PROJECT_DISPLAY_NAME}}', '$PROJECT_DISPLAY_NAME')
    
    # Remove HTML comments
    html = re.sub(r'<!--.*?-->', '', html, flags=re.DOTALL)
    # Collapse multiple spaces/newlines to single space
    html = re.sub(r'\s+', ' ', html)
    # Remove spaces around tags
    html = re.sub(r'>\s+<', '><', html)
    # Trim
    html = html.strip()
    print(html, end='')
")
    
    HTML_CONTENTS["$filename"]="$minified"
    minified_size=$(echo -n "$minified" | wc -c)
    
    # Gzip compress
    gzipped=$(gzip_to_c_array "$minified")
    HTML_GZIP_CONTENTS["$filename"]="$gzipped"
    gzipped_size=$(echo -n "$minified" | gzip -9 -c | wc -c)
    
    ORIGINAL_SIZES["html_$filename"]=$original_size
    PROCESSED_SIZES["html_$filename"]=$minified_size
    GZIPPED_SIZES["html_$filename"]=$gzipped_size
done

# Process fragment HTML files (subset of template substitution + minification)
# Fragments get: BINDING_HELP, WIDGET_*, STYLE_HELP, HEALTH_WIDGET, REBOOT_OVERLAY,
# PROJECT_NAME, PROJECT_DISPLAY_NAME. They do NOT get HEADER, NAV, or FOOTER.
for fragment_file in "${FRAGMENT_FILES[@]}"; do
    stem=$(basename "$fragment_file" .fragment.html)
    # C symbols: replace hyphens with underscores
    c_stem="${stem//-/_}"
    filename="${c_stem}_fragment"
    echo "Processing fragment: $stem.fragment.html..."
    content=$(cat "$fragment_file")
    original_size=$(echo -n "$content" | wc -c)
    
    minified=$(python3 -c "
import re

binding_help_template = '''$BINDING_HELP_TEMPLATE'''
widget_bar_chart_template = '''$WIDGET_BAR_CHART_TEMPLATE'''
widget_gauge_template = '''$WIDGET_GAUGE_TEMPLATE'''
widget_sparkline_template = '''$WIDGET_SPARKLINE_TEMPLATE'''
widget_table_template = '''$WIDGET_TABLE_TEMPLATE'''
widget_rocker_template = '''$WIDGET_ROCKER_TEMPLATE'''
widget_numericrocker_template = '''$WIDGET_NUMERICROCKER_TEMPLATE'''
style_help_template = '''$STYLE_HELP_TEMPLATE'''
health_widget_template = '''$HEALTH_WIDGET_TEMPLATE'''
reboot_overlay_template = '''$REBOOT_OVERLAY_TEMPLATE'''

with open('$fragment_file', 'r') as f:
    html = f.read()
    
    html = html.replace('{{BINDING_HELP}}', binding_help_template)
    html = html.replace('{{WIDGET_BAR_CHART}}', widget_bar_chart_template)
    html = html.replace('{{WIDGET_GAUGE}}', widget_gauge_template)
    html = html.replace('{{WIDGET_SPARKLINE}}', widget_sparkline_template)
    html = html.replace('{{WIDGET_TABLE}}', widget_table_template)
    html = html.replace('{{WIDGET_ROCKER}}', widget_rocker_template)
    html = html.replace('{{WIDGET_NUMERICROCKER}}', widget_numericrocker_template)
    html = html.replace('{{STYLE_HELP}}', style_help_template)
    html = html.replace('{{HEALTH_WIDGET}}', health_widget_template)
    html = html.replace('{{REBOOT_OVERLAY}}', reboot_overlay_template)
    
    html = html.replace('{{PROJECT_NAME}}', '$PROJECT_NAME')
    html = html.replace('{{PROJECT_DISPLAY_NAME}}', '$PROJECT_DISPLAY_NAME')
    
    html = re.sub(r'<!--.*?-->', '', html, flags=re.DOTALL)
    html = re.sub(r'\s+', ' ', html)
    html = re.sub(r'>\s+<', '><', html)
    html = html.strip()
    print(html, end='')
")
    
    FRAGMENT_CONTENTS["$filename"]="$minified"
    minified_size=$(echo -n "$minified" | wc -c)
    
    gzipped=$(gzip_to_c_array "$minified")
    FRAGMENT_GZIP_CONTENTS["$filename"]="$gzipped"
    gzipped_size=$(echo -n "$minified" | gzip -9 -c | wc -c)
    
    ORIGINAL_SIZES["frag_$filename"]=$original_size
    PROCESSED_SIZES["frag_$filename"]=$minified_size
    GZIPPED_SIZES["frag_$filename"]=$gzipped_size
done

# Process CSS files (minify)
for css_file in "${CSS_FILES[@]}"; do
    raw_name=$(basename "$css_file" .css)
    filename="${raw_name//[.-]/_}"
    echo "Minifying CSS: $raw_name.css..."
    content=$(cat "$css_file")
    original_size=$(echo -n "$content" | wc -c)
    
    minified=$(python3 -c "
import csscompressor
with open('$css_file', 'r') as f:
    css = f.read()
    minified = csscompressor.compress(css)
    print(minified, end='')
")
    
    CSS_CONTENTS["$filename"]="$minified"
    minified_size=$(echo -n "$minified" | wc -c)
    
    # Gzip compress
    gzipped=$(gzip_to_c_array "$minified")
    CSS_GZIP_CONTENTS["$filename"]="$gzipped"
    gzipped_size=$(echo -n "$minified" | gzip -9 -c | wc -c)
    
    ORIGINAL_SIZES["css_$filename"]=$original_size
    PROCESSED_SIZES["css_$filename"]=$minified_size
    GZIPPED_SIZES["css_$filename"]=$gzipped_size
done

# Process JS files (minify, with bundle support)
for js_file in "${JS_FILES[@]}"; do
    raw_name=$(basename "$js_file" .js)
    filename="${raw_name//[.-]/_}"

    # Bundle support: if a .bundle manifest exists, concatenate fragments
    bundle_manifest="${js_file}.bundle"
    js_source="$js_file"
    if [[ -f "$bundle_manifest" ]]; then
        echo "Bundling JS: $filename.js (from $(basename "$bundle_manifest"))..."
        js_source=$(mktemp /tmp/bundle_XXXXXX.js)
        while IFS= read -r bline || [[ -n "$bline" ]]; do
            bline="${bline%%#*}"
            bline="$(echo "$bline" | xargs)"
            [[ -z "$bline" ]] && continue
            cat "$WEB_DIR/$bline" >> "$js_source"
            echo >> "$js_source"
        done < "$bundle_manifest"

        # Syntax-check the concatenated bundle
        if command -v node &> /dev/null; then
            if ! node --check "$js_source" 2>/dev/null; then
                echo "  ✗ Concatenated bundle $filename.js has syntax errors:"
                node --check "$js_source" 2>&1 | sed 's/^/    /'
                rm -f "$js_source"
                exit 1
            fi
        fi
    fi

    echo "Minifying JS: $raw_name.js..."
    content=$(cat "$js_source")
    original_size=$(echo -n "$content" | wc -c)
    
    minified=$(python3 -c "
import rjsmin
with open('$js_source', 'r') as f:
    js = f.read()
    minified = rjsmin.jsmin(js)
    print(minified, end='')
")

    # Cleanup temp file if it was a bundle
    [[ "$js_source" != "$js_file" ]] && rm -f "$js_source"
    
    JS_CONTENTS["$filename"]="$minified"
    minified_size=$(echo -n "$minified" | wc -c)
    
    # Gzip compress
    gzipped=$(gzip_to_c_array "$minified")
    JS_GZIP_CONTENTS["$filename"]="$gzipped"
    gzipped_size=$(echo -n "$minified" | gzip -9 -c | wc -c)
    
    ORIGINAL_SIZES["js_$filename"]=$original_size
    PROCESSED_SIZES["js_$filename"]=$minified_size
    GZIPPED_SIZES["js_$filename"]=$gzipped_size
done

echo

# Generate the header file
echo "Generating $OUTPUT_FILE..."

# Generate project branding header (tiny, safe to include from anywhere)
cat > "$BRANDING_FILE" << 'BRANDING_HEADER_START'
/*
 * Project Branding
 *
 * *** AUTO-GENERATED FILE - DO NOT EDIT MANUALLY ***
 *
 * Generated by tools/minify-web-assets.sh from config.sh values.
 */

#ifndef PROJECT_BRANDING_H
#define PROJECT_BRANDING_H

// If these were already defined (e.g. board_config.h defaults), override them.
#ifdef PROJECT_NAME
#undef PROJECT_NAME
#endif

#ifdef PROJECT_DISPLAY_NAME
#undef PROJECT_DISPLAY_NAME
#endif

BRANDING_HEADER_START

cat >> "$BRANDING_FILE" << EOF
#define PROJECT_NAME "$PROJECT_NAME_C"
#define PROJECT_DISPLAY_NAME "$PROJECT_DISPLAY_NAME_C"

EOF

cat >> "$BRANDING_FILE" << 'BRANDING_HEADER_END'

#endif // PROJECT_BRANDING_H
BRANDING_HEADER_END

# Generate repo slug header (tiny, safe to include from anywhere)
cat > "$REPO_SLUG_FILE" << 'REPO_SLUG_HEADER_START'
/*
 * Repository Slug Configuration
 *
 * *** AUTO-GENERATED FILE - DO NOT EDIT MANUALLY ***
 *
 * Generated by tools/minify-web-assets.sh by inspecting the local git remote.
 */

#ifndef REPO_SLUG_CONFIG_H
#define REPO_SLUG_CONFIG_H

// Repository owner/name used to construct GitHub Pages URLs.

REPO_SLUG_HEADER_START

REPO_OWNER_C="$(escape_c_string "$GITHUB_OWNER")"
REPO_REPO_C="$(escape_c_string "$GITHUB_REPO")"
cat >> "$REPO_SLUG_FILE" << EOF
#define REPO_OWNER "$REPO_OWNER_C"
#define REPO_NAME "$REPO_REPO_C"

EOF

cat >> "$REPO_SLUG_FILE" << 'REPO_SLUG_HEADER_END'

#endif // REPO_SLUG_CONFIG_H
REPO_SLUG_HEADER_END

# Start header file
cat > "$OUTPUT_FILE" << 'HEADER_START'
/*
 * Web Assets - Embedded HTML/CSS/JS for ESP32 Web Server
 * 
 * *** AUTO-GENERATED FILE - DO NOT EDIT MANUALLY ***
 * 
 * This file is automatically generated by tools/minify-web-assets.sh
 * Source files are dynamically discovered in src/app/web/ directory.
 * 
 * Processing:
 *   - HTML files: template substitution + basic minification + gzip compression
 *   - CSS files:  minified using csscompressor + gzip compression
 *   - JS files:   minified using rjsmin + gzip compression
 * 
 * All assets are stored in gzipped format with Content-Encoding: gzip headers.
 * This reduces flash storage and bandwidth by 60-80%.
 * 
 * To modify web assets:
 *   1. Edit source files in src/app/web/
 *   2. Run ./build.sh (automatically runs minification and gzip compression)
 *   3. Upload new firmware to device
 */

#ifndef WEB_ASSETS_H
#define WEB_ASSETS_H

#include <Arduino.h>

// Project branding (from config.sh)
// Kept in a tiny header so non-web code can include branding without pulling
// in the large embedded asset arrays.
#include "project_branding.h"

HEADER_START

# Generate HTML sections (gzipped)
for filename in "${!HTML_CONTENTS[@]}"; do
    cat >> "$OUTPUT_FILE" << EOF
// HTML content from src/app/web/${filename}.html (gzipped)
const uint8_t ${filename}_html_gz[] PROGMEM = {
${HTML_GZIP_CONTENTS[$filename]}
};

EOF
done

# Generate fragment HTML sections (gzipped)
for filename in "${!FRAGMENT_CONTENTS[@]}"; do
    cat >> "$OUTPUT_FILE" << EOF
// Fragment from src/app/web/${filename%.fragment*}.fragment.html (gzipped)
const uint8_t ${filename}_html_gz[] PROGMEM = {
${FRAGMENT_GZIP_CONTENTS[$filename]}
};

EOF
done

# Generate CSS sections (gzipped)
for filename in "${!CSS_CONTENTS[@]}"; do
    cat >> "$OUTPUT_FILE" << EOF
// CSS styles from src/app/web/${filename}.css (minified + gzipped)
const uint8_t ${filename}_css_gz[] PROGMEM = {
${CSS_GZIP_CONTENTS[$filename]}
};

EOF
done

# Generate JS sections (gzipped)
for filename in "${!JS_CONTENTS[@]}"; do
    cat >> "$OUTPUT_FILE" << EOF
// JavaScript from src/app/web/${filename}.js (minified + gzipped)
const uint8_t ${filename}_js_gz[] PROGMEM = {
${JS_GZIP_CONTENTS[$filename]}
};

EOF
done

# Add size constants (gzipped sizes)
cat >> "$OUTPUT_FILE" << 'SIZE_CONSTANTS'

// Asset sizes (gzipped, calculated at compile time)
SIZE_CONSTANTS

for filename in "${!HTML_CONTENTS[@]}"; do
    echo "const size_t ${filename}_html_gz_len = sizeof(${filename}_html_gz);" >> "$OUTPUT_FILE"
done

for filename in "${!FRAGMENT_CONTENTS[@]}"; do
    echo "const size_t ${filename}_html_gz_len = sizeof(${filename}_html_gz);" >> "$OUTPUT_FILE"
done

for filename in "${!CSS_CONTENTS[@]}"; do
    echo "const size_t ${filename}_css_gz_len = sizeof(${filename}_css_gz);" >> "$OUTPUT_FILE"
done

for filename in "${!JS_CONTENTS[@]}"; do
    echo "const size_t ${filename}_js_gz_len = sizeof(${filename}_js_gz);" >> "$OUTPUT_FILE"
done

# Generate fragment lookup table for runtime dispatch
if [ ${#FRAGMENT_CONTENTS[@]} -gt 0 ]; then
    cat >> "$OUTPUT_FILE" << 'FRAG_TABLE_START'

// Fragment lookup table for /api/section/{id} dispatch
struct FragmentAsset {
    const char* id;       // fragment_id from ComponentDef (hyphenated, e.g. "wifi")
    const uint8_t* data;  // gzipped PROGMEM data
    size_t len;           // gzipped data length
};

static const FragmentAsset fragment_assets[] = {
FRAG_TABLE_START

    for filename in "${!FRAGMENT_CONTENTS[@]}"; do
        # Convert symbol name back to fragment_id: wifi_fragment -> wifi, pad_editor_fragment -> pad-editor
        frag_id="${filename%_fragment}"
        frag_id="${frag_id//_/-}"
        echo "    {\"$frag_id\", ${filename}_html_gz, sizeof(${filename}_html_gz)}," >> "$OUTPUT_FILE"
    done

    cat >> "$OUTPUT_FILE" << 'FRAG_TABLE_END'
};

static constexpr size_t fragment_assets_count = sizeof(fragment_assets) / sizeof(fragment_assets[0]);

// Lookup a fragment by its id. Returns nullptr if not found.
static inline const FragmentAsset* find_fragment_asset(const char* id) {
    for (size_t i = 0; i < fragment_assets_count; i++) {
        if (strcmp(fragment_assets[i].id, id) == 0) return &fragment_assets[i];
    }
    return nullptr;
}

FRAG_TABLE_END
fi

# Close header file
cat >> "$OUTPUT_FILE" << 'HEADER_END'

#endif // WEB_ASSETS_H
HEADER_END

# Display summary with statistics
echo "✓ Successfully generated web_assets.h"
echo
echo "Asset Summary (Original → Minified → Gzipped):"

# Calculate totals
total_original=0
total_processed=0
total_gzipped=0

for filename in "${!HTML_CONTENTS[@]}"; do
    key="html_$filename"
    orig=${ORIGINAL_SIZES[$key]}
    proc=${PROCESSED_SIZES[$key]}
    gzip=${GZIPPED_SIZES[$key]}
    percent=$((100 - (gzip * 100 / orig)))
    echo "  HTML ${filename}.html: $orig → $proc → $gzip bytes (-${percent}% total)"
    total_original=$((total_original + orig))
    total_processed=$((total_processed + proc))
    total_gzipped=$((total_gzipped + gzip))
done

for filename in "${!FRAGMENT_CONTENTS[@]}"; do
    key="frag_$filename"
    orig=${ORIGINAL_SIZES[$key]}
    proc=${PROCESSED_SIZES[$key]}
    gzip=${GZIPPED_SIZES[$key]}
    percent=$((100 - (gzip * 100 / orig)))
    echo "  FRAG ${filename}: $orig → $proc → $gzip bytes (-${percent}% total)"
    total_original=$((total_original + orig))
    total_processed=$((total_processed + proc))
    total_gzipped=$((total_gzipped + gzip))
done

for filename in "${!CSS_CONTENTS[@]}"; do
    key="css_$filename"
    orig=${ORIGINAL_SIZES[$key]}
    proc=${PROCESSED_SIZES[$key]}
    gzip=${GZIPPED_SIZES[$key]}
    percent=$((100 - (gzip * 100 / orig)))
    echo "  CSS  ${filename}.css:  $orig → $proc → $gzip bytes (-${percent}% total)"
    total_original=$((total_original + orig))
    total_processed=$((total_processed + proc))
    total_gzipped=$((total_gzipped + gzip))
done

for filename in "${!JS_CONTENTS[@]}"; do
    key="js_$filename"
    orig=${ORIGINAL_SIZES[$key]}
    proc=${PROCESSED_SIZES[$key]}
    gzip=${GZIPPED_SIZES[$key]}
    percent=$((100 - (gzip * 100 / orig)))
    echo "  JS   ${filename}.js:   $orig → $proc → $gzip bytes (-${percent}% total)"
    total_original=$((total_original + orig))
    total_processed=$((total_processed + proc))
    total_gzipped=$((total_gzipped + gzip))
done

echo "  ─────────────────────────────────────────────────────────────"
total_percent=$((100 - (total_gzipped * 100 / total_original)))
echo "  TOTAL: $total_original → $total_processed → $total_gzipped bytes (-${total_percent}% total)"
echo
