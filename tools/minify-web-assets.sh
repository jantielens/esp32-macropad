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

# ---- Bundle support ----
# A *.bundle manifest lists fragment files that should be concatenated into the
# primary asset during minification.  Works for both JS (.js.bundle) and CSS
# (.css.bundle).  Fragment files (prefixed with _) are excluded from individual
# processing so they only appear in the bundled output.
#
# Concatenation order matters: JS bundles concatenate top-to-bottom, CSS bundles
# follow CSS cascade rules (later entries override earlier ones at equal
# specificity).

# discover_bundle_manifests <extension> <skip_array_name>
#   Scans WEB_DIR for *.<ext>.bundle manifests, validates referenced files, and
#   populates the named associative array with fragment paths to skip.
discover_bundle_manifests() {
    local ext="$1"
    local -n _skip_map="$2"
    for bundle_manifest in $(find "$WEB_DIR" -maxdepth 1 -name "*.$ext.bundle" -type f 2>/dev/null); do
        local primary_name
        primary_name=$(basename "$bundle_manifest" .bundle)  # e.g. portal.js
        local bundle_missing=0
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
            _skip_map["$WEB_DIR/$bline"]=1
        done < "$bundle_manifest"
        if [[ $bundle_missing -gt 0 ]]; then
            echo "Error: $bundle_missing missing file(s) in bundle manifest $(basename "$bundle_manifest")"
            exit 1
        fi
    done
}

# filter_bundle_fragments <file_array_name> <skip_array_name>
#   Removes entries present in the skip map from the file list (in-place).
filter_bundle_fragments() {
    local -n _files="$1"
    local -n _skip="$2"
    local filtered=()
    for f in "${_files[@]}"; do
        [[ -n "${_skip[$f]}" ]] && continue
        filtered+=("$f")
    done
    _files=("${filtered[@]}")
}

# concatenate_bundle <bundle_manifest> <output_var_name>
#   Reads a .bundle manifest and concatenates listed files into a temp file.
#   Sets the named variable to the temp file path (caller must clean up).
#   Returns 1 if the manifest does not exist (no bundle).
concatenate_bundle() {
    local manifest="$1"
    local -n _out_path="$2"
    [[ -f "$manifest" ]] || return 1
    _out_path=$(mktemp /tmp/bundle_XXXXXX)
    while IFS= read -r bline || [[ -n "$bline" ]]; do
        bline="${bline%%#*}"
        bline="$(echo "$bline" | xargs)"
        [[ -z "$bline" ]] && continue
        cat "$WEB_DIR/$bline" >> "$_out_path"
        echo >> "$_out_path"
    done < "$manifest"
}

# concatenate_bundle_chunked <manifest> <chunks_array> <flags_array> <names_array>
#   Parses a .bundle manifest with `# [chunk:NAME]` or `# [chunk:NAME HAS_FLAG]`
#   markers and emits one temp file per chunk (in order). Populates:
#     chunks: list of temp file paths (caller must rm)
#     flags:  list of feature flag strings (empty for always-on chunks)
#     names:  list of chunk names (e.g. "core", "pad")
#   Files before the first marker are an error. Empty chunks are an error.
#   Returns 1 if the manifest does not exist or parsing fails.
concatenate_bundle_chunked() {
    local manifest="$1"
    local -n _chunks="$2"
    local -n _flags="$3"
    local -n _names="$4"
    [[ -f "$manifest" ]] || return 1
    _chunks=()
    _flags=()
    _names=()

    local current_flag=""
    local current_name=""
    local current_tmp=""
    local has_content=0
    local seen_marker=0

    while IFS= read -r bline || [[ -n "$bline" ]]; do
        # Detect [chunk:NAME] or [chunk:NAME HAS_FLAG] markers BEFORE stripping.
        local trimmed
        trimmed=$(echo "$bline" | xargs)
        if [[ "$trimmed" =~ ^#[[:space:]]*\[chunk:([a-z0-9_-]+)(([[:space:]]+HAS_[A-Z_0-9]+)?)\][[:space:]]*$ ]]; then
            local new_name="${BASH_REMATCH[1]}"
            local new_flag_raw="${BASH_REMATCH[2]}"
            local new_flag
            new_flag=$(echo "$new_flag_raw" | xargs)
            # Flush current chunk
            if [[ $seen_marker -eq 1 ]]; then
                if [[ $has_content -eq 0 ]]; then
                    echo "  ✗ chunk [$current_name] is empty" >&2
                    rm -f "$current_tmp"
                    return 1
                fi
                _chunks+=("$current_tmp")
                _flags+=("$current_flag")
                _names+=("$current_name")
            fi
            current_name="$new_name"
            current_flag="$new_flag"
            current_tmp=$(mktemp /tmp/bundle_chunk_XXXXXX)
            has_content=0
            seen_marker=1
            continue
        fi
        bline="${bline%%#*}"
        bline="$(echo "$bline" | xargs)"
        [[ -z "$bline" ]] && continue
        if [[ $seen_marker -eq 0 ]]; then
            echo "  ✗ file '$bline' appears before first [chunk:NAME] marker" >&2
            return 1
        fi
        cat "$WEB_DIR/$bline" >> "$current_tmp"
        echo >> "$current_tmp"
        has_content=1
    done < "$manifest"

    # Flush trailing chunk
    if [[ $seen_marker -eq 1 ]]; then
        if [[ $has_content -eq 0 ]]; then
            echo "  ✗ chunk [$current_name] is empty" >&2
            rm -f "$current_tmp"
            return 1
        fi
        _chunks+=("$current_tmp")
        _flags+=("$current_flag")
        _names+=("$current_name")
    fi
}

# bundle_has_chunk_markers <manifest>
#   Returns 0 if the manifest contains any `# [chunk:NAME]` markers, 1 otherwise.
bundle_has_chunk_markers() {
    local manifest="$1"
    [[ -f "$manifest" ]] || return 1
    grep -qE '^[[:space:]]*#[[:space:]]*\[chunk:[a-z0-9_-]+([[:space:]]+HAS_[A-Z_0-9]+)?\][[:space:]]*$' "$manifest"
}

declare -A JS_SKIP_FILES
declare -A CSS_SKIP_FILES
discover_bundle_manifests "js" JS_SKIP_FILES
discover_bundle_manifests "css" CSS_SKIP_FILES
filter_bundle_fragments JS_FILES JS_SKIP_FILES
filter_bundle_fragments CSS_FILES CSS_SKIP_FILES

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
WIDGET_LIST_TEMPLATE=""
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

if [ -f "$WEB_DIR/_widget_list.html" ]; then
    WIDGET_LIST_TEMPLATE=$(cat "$WEB_DIR/_widget_list.html")
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
widget_list_template = '''$WIDGET_LIST_TEMPLATE'''
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
    html = html.replace('{{WIDGET_LIST}}', widget_list_template)
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
widget_list_template = '''$WIDGET_LIST_TEMPLATE'''
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
    html = html.replace('{{WIDGET_LIST}}', widget_list_template)
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

# Process CSS files (minify, with bundle support)
for css_file in "${CSS_FILES[@]}"; do
    raw_name=$(basename "$css_file" .css)
    filename="${raw_name//[.-]/_}"

    # Bundle support: if a .bundle manifest exists, concatenate fragments
    bundle_manifest="${css_file}.bundle"
    css_source="$css_file"
    if concatenate_bundle "$bundle_manifest" css_source; then
        echo "Bundling CSS: $raw_name.css (from $(basename "$bundle_manifest"))..."
    fi

    echo "Minifying CSS: $raw_name.css..."
    content=$(cat "$css_source")
    original_size=$(echo -n "$content" | wc -c)
    
    minified=$(python3 -c "
import csscompressor
with open('$css_source', 'r') as f:
    css = f.read()
    minified = csscompressor.compress(css)
    print(minified, end='')
")

    # Cleanup temp file if it was a bundle
    [[ "$css_source" != "$css_file" ]] && rm -f "$css_source"
    
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

# Per-chunk feature-flag map for chunked JS bundles. Keys are JS_CONTENTS keys
# (e.g. "portal_core"); values are HAS_* macro names (or empty for always-on).
declare -A JS_CHUNK_FLAGS
# Per-chunk human name (e.g. "core", "pad"); keyed by JS_CONTENTS key.
declare -A JS_CHUNK_NAMES
# Map from bundle primary key (e.g. "portal") to space-separated chunk keys
# in concatenation order. Used during header emission to write the chunks table.
declare -A JS_BUNDLE_CHUNKS

# Process JS files (minify, with bundle support)
for js_file in "${JS_FILES[@]}"; do
    raw_name=$(basename "$js_file" .js)
    filename="${raw_name//[.-]/_}"

    bundle_manifest="${js_file}.bundle"

    # --- Chunked bundle path (manifest contains [HAS_*] markers) ---
    if bundle_has_chunk_markers "$bundle_manifest"; then
        echo "Bundling JS (chunked): $raw_name.js (from $(basename "$bundle_manifest"))..."

        chunk_paths=()
        chunk_flags=()
        chunk_names=()
        if ! concatenate_bundle_chunked "$bundle_manifest" chunk_paths chunk_flags chunk_names; then
            echo "  ✗ Failed to parse bundle manifest $bundle_manifest"
            exit 1
        fi

        chunk_keys=""
        for ci in "${!chunk_paths[@]}"; do
            chunk_path="${chunk_paths[$ci]}"
            chunk_flag="${chunk_flags[$ci]}"
            chunk_name="${chunk_names[$ci]}"
            chunk_key="${filename}_${chunk_name}"

            # Syntax-check the chunk independently
            if command -v node &> /dev/null; then
                if ! node --check "$chunk_path" 2>/dev/null; then
                    echo "  ✗ Bundle chunk [${chunk_name}] (${chunk_flag:-always}) has syntax errors:"
                    node --check "$chunk_path" 2>&1 | sed 's/^/    /'
                    rm -f "${chunk_paths[@]}"
                    exit 1
                fi
            fi

            echo "  Minifying chunk [${chunk_name}] (${chunk_flag:-always})..."
            chunk_orig_size=$(wc -c <"$chunk_path")

            chunk_minified=$(python3 -c "
import rjsmin
with open('$chunk_path', 'r') as f:
    js = f.read()
    print(rjsmin.jsmin(js), end='')
")
            rm -f "$chunk_path"

            JS_CONTENTS["$chunk_key"]="$chunk_minified"
            JS_CHUNK_FLAGS["$chunk_key"]="$chunk_flag"
            JS_CHUNK_NAMES["$chunk_key"]="$chunk_name"
            chunk_min_size=$(echo -n "$chunk_minified" | wc -c)

            chunk_gzipped=$(gzip_to_c_array "$chunk_minified")
            JS_GZIP_CONTENTS["$chunk_key"]="$chunk_gzipped"
            chunk_gz_size=$(echo -n "$chunk_minified" | gzip -9 -c | wc -c)

            ORIGINAL_SIZES["js_$chunk_key"]=$chunk_orig_size
            PROCESSED_SIZES["js_$chunk_key"]=$chunk_min_size
            GZIPPED_SIZES["js_$chunk_key"]=$chunk_gz_size

            chunk_keys+="$chunk_key "
        done
        JS_BUNDLE_CHUNKS["$filename"]="${chunk_keys% }"
        continue
    fi

    # --- Plain bundle / single-file path ---
    js_source="$js_file"
    if concatenate_bundle "$bundle_manifest" js_source; then
        echo "Bundling JS: $raw_name.js (from $(basename "$bundle_manifest"))..."

        # Syntax-check the concatenated bundle
        if command -v node &> /dev/null; then
            if ! node --check "$js_source" 2>/dev/null; then
                echo "  ✗ Concatenated bundle $raw_name.js has syntax errors:"
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

// Board feature flags — gate display/audio/MQTT-only fragments away when
// the corresponding subsystem is disabled at compile time. Without this
// the linker pulls in every PROGMEM fragment array even though headless
// builds never call find_fragment_asset() for them.
#include "board_config.h"

// Project branding (from config.sh)
// Kept in a tiny header so non-web code can include branding without pulling
// in the large embedded asset arrays.
#include "project_branding.h"

HEADER_START

# Map a fragment filename stem (e.g. "pad_editor_fragment") to the
# compile-time feature flag that must be true for the fragment to be
# included in the build. Echoes nothing for always-on fragments.
fragment_feature_flag() {
    local stem="$1"
    # Strip trailing _fragment suffix
    stem="${stem%_fragment}"
    case "$stem" in
        pad_editor|swipe_actions|boot_actions|button_defaults|timers|brightness|screensaver)
            echo "HAS_DISPLAY" ;;
        mqtt|ha_discovery)
            echo "HAS_MQTT" ;;
        ble)
            echo "HAS_BLE_HID" ;;
        volume)
            echo "HAS_AUDIO" ;;
        sounds)
            echo "HAS_SOUND_PLAYER" ;;
        *)
            echo "" ;;
    esac
}

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
    flag=$(fragment_feature_flag "$filename")
    if [[ -n "$flag" ]]; then
        echo "#if $flag" >> "$OUTPUT_FILE"
    fi
    cat >> "$OUTPUT_FILE" << EOF
// Fragment from src/app/web/${filename%.fragment*}.fragment.html (gzipped)
const uint8_t ${filename}_html_gz[] PROGMEM = {
${FRAGMENT_GZIP_CONTENTS[$filename]}
};

EOF
    if [[ -n "$flag" ]]; then
        echo "#endif // $flag" >> "$OUTPUT_FILE"
        echo >> "$OUTPUT_FILE"
    fi
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

# Generate JS sections (gzipped). Chunked-bundle keys are emitted as
# per-variant blobs in a separate pass below (one combined PROGMEM array
# per build), so skip them here.
for filename in "${!JS_CONTENTS[@]}"; do
    if [[ -n "${JS_CHUNK_NAMES[$filename]:-}" ]]; then
        continue
    fi
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
    flag=$(fragment_feature_flag "$filename")
    if [[ -n "$flag" ]]; then
        echo "#if $flag" >> "$OUTPUT_FILE"
        echo "const size_t ${filename}_html_gz_len = sizeof(${filename}_html_gz);" >> "$OUTPUT_FILE"
        echo "#endif // $flag" >> "$OUTPUT_FILE"
    else
        echo "const size_t ${filename}_html_gz_len = sizeof(${filename}_html_gz);" >> "$OUTPUT_FILE"
    fi
done

for filename in "${!CSS_CONTENTS[@]}"; do
    echo "const size_t ${filename}_css_gz_len = sizeof(${filename}_css_gz);" >> "$OUTPUT_FILE"
done

for filename in "${!JS_CONTENTS[@]}"; do
    if [[ -n "${JS_CHUNK_NAMES[$filename]:-}" ]]; then
        continue
    fi
    echo "const size_t ${filename}_js_gz_len = sizeof(${filename}_js_gz);" >> "$OUTPUT_FILE"
done

# Generate JS bundle variants. For each chunked bundle (e.g. portal_js):
#   1. Collect unique HAS_* flags used across chunks (capped at 3 -> 8 variants).
#   2. For each 2^N combination, concatenate chunks whose flag is unset OR
#      matches the combination, then gzip ONCE.
#   3. Emit each variant as a #if-guarded PROGMEM array under the same
#      symbol name (${bundle_name}_js_gz). Exactly one #if branch matches
#      per build -> one combined blob, one HTTP request, one gzip member.
for bundle_name in "${!JS_BUNDLE_CHUNKS[@]}"; do
    chunks="${JS_BUNDLE_CHUNKS[$bundle_name]}"

    declare -a unique_flags=()
    declare -A seen_flags=()
    for ckey in $chunks; do
        f="${JS_CHUNK_FLAGS[$ckey]:-}"
        if [[ -n "$f" && -z "${seen_flags[$f]:-}" ]]; then
            unique_flags+=("$f")
            seen_flags["$f"]=1
        fi
    done
    n_flags=${#unique_flags[@]}
    if [[ $n_flags -gt 3 ]]; then
        echo "  ✗ Bundle $bundle_name has $n_flags unique chunk flags (max 3 supported)." >&2
        echo "     Each flag doubles flash cost across boards; consolidate chunks instead." >&2
        unset unique_flags seen_flags
        exit 1
    fi
    n_variants=$((1 << n_flags))
    echo "Building variants for ${bundle_name}_js (${n_flags} unique flags, ${n_variants} variant(s))..."

    cat >> "$OUTPUT_FILE" << EOF

// JS bundle variants for ${bundle_name}.js
// Chunks: $(echo $chunks)
// Unique flags: ${unique_flags[*]:-<none>}
// Each board build matches exactly one #if branch below.
EOF

    for ((v=0; v<n_variants; v++)); do
        if_expr=""
        for ((b=0; b<n_flags; b++)); do
            flag="${unique_flags[$b]}"
            if (( (v >> b) & 1 )); then
                cond="$flag"
            else
                cond="!$flag"
            fi
            if [[ -z "$if_expr" ]]; then
                if_expr="$cond"
            else
                if_expr="$if_expr && $cond"
            fi
        done

        variant_tmp=$(mktemp /tmp/bundle_variant_XXXXXX)
        for ckey in $chunks; do
            cflag="${JS_CHUNK_FLAGS[$ckey]:-}"
            include=0
            if [[ -z "$cflag" ]]; then
                include=1
            else
                for ((b=0; b<n_flags; b++)); do
                    if [[ "${unique_flags[$b]}" == "$cflag" ]]; then
                        if (( (v >> b) & 1 )); then include=1; fi
                        break
                    fi
                done
            fi
            if [[ $include -eq 1 ]]; then
                printf '%s\n' "${JS_CONTENTS[$ckey]}" >> "$variant_tmp"
            fi
        done

        variant_orig_size=$(wc -c <"$variant_tmp")
        variant_gz_size=$(gzip -9 -c <"$variant_tmp" | wc -c)
        variant_gz=$(gzip_to_c_array "$(cat "$variant_tmp")")
        rm -f "$variant_tmp"

        if [[ $n_flags -eq 0 ]]; then
            cat >> "$OUTPUT_FILE" << EOF
const uint8_t ${bundle_name}_js_gz[] PROGMEM = {
${variant_gz}
};
const size_t ${bundle_name}_js_gz_len = sizeof(${bundle_name}_js_gz);

EOF
        else
            cat >> "$OUTPUT_FILE" << EOF
#if ${if_expr}
const uint8_t ${bundle_name}_js_gz[] PROGMEM = {
${variant_gz}
};
const size_t ${bundle_name}_js_gz_len = sizeof(${bundle_name}_js_gz);
#endif // ${if_expr}

EOF
        fi
        printf "  variant %d/%d [%s]: %d -> %d bytes\n" "$((v+1))" "$n_variants" "${if_expr:-always}" "$variant_orig_size" "$variant_gz_size"
    done

    unset unique_flags seen_flags
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
        flag=$(fragment_feature_flag "$filename")
        if [[ -n "$flag" ]]; then
            echo "#if $flag" >> "$OUTPUT_FILE"
            echo "    {\"$frag_id\", ${filename}_html_gz, sizeof(${filename}_html_gz)}," >> "$OUTPUT_FILE"
            echo "#endif // $flag" >> "$OUTPUT_FILE"
        else
            echo "    {\"$frag_id\", ${filename}_html_gz, sizeof(${filename}_html_gz)}," >> "$OUTPUT_FILE"
        fi
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
