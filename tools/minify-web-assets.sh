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

# ---------------------------------------------------------------------------
# Chunked-bundle variant enumeration (pure, testable)
# ---------------------------------------------------------------------------
# The variant generator (emit_chunked_variants) must produce one #if-guarded
# blob per *reachable* combination of a bundle's chunk flags. Two flag kinds
# behave very differently:
#
#   * Independent flags (e.g. HAS_DISPLAY, HAS_EPAPER) can each be on/off
#     freely, so k of them yield 2^k combinations.
#   * Device-class flags (IS_*) are mutually exclusive — a board is exactly
#     one device class (see device_class_detect()'s #if ladder). c of them
#     therefore yield only (c + 1) reachable states: one per class, plus a
#     "none" state. The 2^c independent expansion would emit combinations
#     like `IS_SHUTTER_TESTER && IS_COFFEE_SCALE` that no board can ever
#     match, bloating web_assets.h for nothing.
#
# Total reachable variants = 2^k * (c + 1).
#
# A device-class flag is identified purely by its `IS_` prefix, matching the
# enforced naming convention for product variants.

# True (exit 0) when $1 is a device-class flag (part of the exclusive group).
is_device_class_flag() {
    [[ "$1" == IS_* ]]
}

# Enumerate the reachable variants for a set of unique chunk flags.
# Emits one line per variant to stdout, tab-separated:
#   <if_expr>\t<space-separated flags that are ON in this variant>
# The if_expr is a C preprocessor expression (e.g. "HAS_DISPLAY && !HAS_EPAPER
# && IS_COFFEE_SCALE && !IS_SHUTTER_TESTER && !IS_DARKROOM_TIMER"), or the
# literal "ALWAYS" when there are no flags. Independent flags expand 2^k;
# device-class flags form one (c + 1)-way exclusive group. Every positive
# device-class branch negates the other class flags, and the "none" branch
# negates them all, so the emitted branches are textually mutually exclusive
# and exhaustive over reachable boards. (A misconfigured board with two IS_*
# flags true would match zero branches -> loud link error, never a silent
# duplicate.)
enumerate_bundle_variants() {
    local -a flags=("$@")
    local -a indep=() klass=()
    local fl
    for fl in "${flags[@]}"; do
        if is_device_class_flag "$fl"; then
            klass+=("$fl")
        else
            indep+=("$fl")
        fi
    done

    local k=${#indep[@]} c=${#klass[@]}
    local n_indep=$((1 << k))
    local iv b cidx j
    for ((iv = 0; iv < n_indep; iv++)); do
        local indep_expr="" indep_active=""
        for ((b = 0; b < k; b++)); do
            if (( (iv >> b) & 1 )); then
                indep_expr+="${indep_expr:+ && }${indep[$b]}"
                indep_active+="${indep_active:+ }${indep[$b]}"
            else
                indep_expr+="${indep_expr:+ && }!${indep[$b]}"
            fi
        done

        # Device-class group: c positive branches (one class on, rest off)
        # plus one "none" branch (all off). When c == 0 this loop runs once
        # with an empty class expression, collapsing to the pure 2^k case.
        for ((cidx = 0; cidx <= c; cidx++)); do
            local class_expr="" class_active=""
            for ((j = 0; j < c; j++)); do
                if (( j == cidx )); then
                    class_expr+="${class_expr:+ && }${klass[$j]}"
                    class_active="${klass[$j]}"
                else
                    class_expr+="${class_expr:+ && }!${klass[$j]}"
                fi
            done

            local full_expr active
            if [[ -n "$indep_expr" && -n "$class_expr" ]]; then
                full_expr="$indep_expr && $class_expr"
            else
                full_expr="${indep_expr}${class_expr}"
            fi
            active="$indep_active"
            [[ -n "$class_active" ]] && active+="${active:+ }$class_active"

            printf '%s\t%s\n' "${full_expr:-ALWAYS}" "$active"
        done
    done
}

# Diagnostic / test hook: print the reachable variant matrix for a set of
# flags and exit, without running the full asset pipeline. Used by
# tests/test_asset_variant_matrix.sh to validate exclusivity/exhaustiveness
# of the generated #if branches against the real production logic.
if [[ "${1:-}" == "--emit-variants" ]]; then
    shift
    enumerate_bundle_variants "$@"
    exit 0
fi

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

# Device-class web assets: per-feature fragments and JS handlers live next to
# their owning C++ code under src/app/device_classes/<class>/web/. They are
# bundled and emitted with the same naming/feature-flag conventions as files
# in WEB_DIR (symbol names are derived from basename only, so a file in either
# location produces the same C symbol).
DEVICE_CLASSES_ROOT="$PROJECT_ROOT/src/app/device_classes"
if [[ -d "$DEVICE_CLASSES_ROOT" ]]; then
    FRAGMENT_FILES+=($(find "$DEVICE_CLASSES_ROOT" -path "*/web/*.fragment.html" -type f | sort))
    JS_FILES+=($(find "$DEVICE_CLASSES_ROOT" -path "*/web/*.js" -not -name "_*.js" -type f | sort))
    CSS_FILES+=($(find "$DEVICE_CLASSES_ROOT" -path "*/web/*.css" -not -name "_*.css" -type f | sort))
fi

# ---- Bundle support ----
# A *.bundle manifest lists fragment files that should be concatenated into the
# primary asset during minification.  Works for both JS (.js.bundle) and CSS
# (.css.bundle).  Fragment files (prefixed with _) are excluded from individual
# processing so they only appear in the bundled output.
#
# Concatenation order matters: JS bundles concatenate top-to-bottom, CSS bundles
# follow CSS cascade rules (later entries override earlier ones at equal
# specificity).

# resolve_bundle_path <relative-path>
#   Echoes the absolute filesystem path for a manifest entry. Looks under
#   WEB_DIR first, then falls back to src/app/device_classes/*/web/<path>.
#   Echoes nothing and returns 1 if the file is not found.
resolve_bundle_path() {
    local rel="$1"
    if [[ -f "$WEB_DIR/$rel" ]]; then
        echo "$WEB_DIR/$rel"
        return 0
    fi
    if [[ -d "$DEVICE_CLASSES_ROOT" ]]; then
        local match
        match=$(find "$DEVICE_CLASSES_ROOT" -path "*/web/$rel" -type f 2>/dev/null | head -1)
        if [[ -n "$match" ]]; then
            echo "$match"
            return 0
        fi
    fi
    return 1
}

# discover_bundle_manifests <extension> <skip_array_name>
#   Scans WEB_DIR for *.<ext>.bundle manifests, validates referenced files, and
#   populates the named associative array with fragment paths to skip.
discover_bundle_manifests() {    local ext="$1"
    local -n _skip_map="$2"
    for bundle_manifest in $(find "$WEB_DIR" -maxdepth 1 -name "*.$ext.bundle" -type f 2>/dev/null); do
        local primary_name
        primary_name=$(basename "$bundle_manifest" .bundle)  # e.g. portal.js
        local bundle_missing=0
        while IFS= read -r bline || [[ -n "$bline" ]]; do
            bline="${bline%%#*}"                     # strip comments
            bline="$(echo "$bline" | xargs)"         # trim whitespace
            [[ -z "$bline" ]] && continue
            # Validate that the listed file exists (WEB_DIR or device_classes/*/web/)
            local resolved
            resolved=$(resolve_bundle_path "$bline")
            if [[ -z "$resolved" ]]; then
                echo "  Error: $(basename "$bundle_manifest") references missing file: $bline"
                bundle_missing=$((bundle_missing + 1))
                continue
            fi
            [[ "$bline" = "$primary_name" ]] && continue  # don't skip the primary
            _skip_map["$resolved"]=1
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
        local resolved
        resolved=$(resolve_bundle_path "$bline")
        if [[ -z "$resolved" ]]; then
            echo "  Error: bundle manifest references missing file: $bline" >&2
            return 1
        fi
        cat "$resolved" >> "$_out_path"
        echo >> "$_out_path"
    done < "$manifest"
}

# concatenate_bundle_chunked <manifest> <chunks_array> <flags_array> <names_array>
#   Parses a .bundle manifest with `# [chunk:NAME]` or `# [chunk:NAME HAS_FLAG]`
#   markers and emits one temp file per chunk (in order). Populates:
#     chunks: list of temp file paths (caller must rm)
#     flags:  list of feature flag strings (empty for always-on chunks; HAS_* or IS_*)
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
        if [[ "$trimmed" =~ ^#[[:space:]]*\[chunk:([a-z0-9_-]+)(([[:space:]]+(HAS|IS)_[A-Z_0-9]+)?)\][[:space:]]*$ ]]; then
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
        local resolved
        resolved=$(resolve_bundle_path "$bline")
        if [[ -z "$resolved" ]]; then
            echo "  ✗ chunk references missing file: $bline" >&2
            return 1
        fi
        cat "$resolved" >> "$current_tmp"
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
    grep -qE '^[[:space:]]*#[[:space:]]*\[chunk:[a-z0-9_-]+([[:space:]]+(HAS|IS)_[A-Z_0-9]+)?\][[:space:]]*$' "$manifest"
}

declare -A JS_SKIP_FILES
declare -A CSS_SKIP_FILES
discover_bundle_manifests "js" JS_SKIP_FILES
discover_bundle_manifests "css" CSS_SKIP_FILES
filter_bundle_fragments JS_FILES JS_SKIP_FILES
filter_bundle_fragments CSS_FILES CSS_SKIP_FILES

# Template fragments under $WEB_DIR (_binding_help.html, _widget_*.html,
# _style_help.html, _health_widget.html, _reboot_overlay.html) are loaded
# by tools/_render_html_template.py at render time.
#
# Historical note: HEADER/NAV/FOOTER placeholders (_header.html / _nav.html
# / _footer.html) were also supported but never landed on disk for any
# board. Removed; resurrect from git history if a future board needs them.

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
    
    # Write content to temp file and gzip it.
    # `-n` suppresses the original filename + mtime in the gzip header so the
    # emitted byte stream is reproducible across runs and CI environments.
    # The ESP32 gzip decoder ignores FNAME/MTIME, so this is purely a
    # build-determinism improvement.
    echo -n "$content" > "$temp_file"
    gzip -9 -n -c "$temp_file" > "$temp_gz"
    
    # Convert to C byte array format
    xxd -i < "$temp_gz" | grep -v "unsigned" | sed 's/^  //'
    
    # Cleanup
    rm -f "$temp_file" "$temp_gz"
}

# ---------------------------------------------------------------------------
# Shared CSS/JS pipeline helpers
# ---------------------------------------------------------------------------
# Both CSS and JS go through the same shape: discover source files, optionally
# concatenate from a .bundle manifest (chunked or plain), minify, gzip, store
# into kind-specific associative arrays, then emit 2^N #if-guarded PROGMEM
# variants for chunked bundles. The only per-kind differences are the
# minifier (csscompressor vs rjsmin), the file extension, the symbol-suffix,
# and an optional `node --check` syntax pass on JS.

# Minify a source file with the kind-specific minifier.
minify_source() {
    local kind="$1"
    local src="$2"
    case "$kind" in
        css)
            python3 -c "
import csscompressor
with open('$src', 'r') as f:
    print(csscompressor.compress(f.read()), end='')
"
            ;;
        js)
            python3 -c "
import rjsmin
with open('$src', 'r') as f:
    print(rjsmin.jsmin(f.read()), end='')
"
            ;;
        *)
            echo "minify_source: unknown kind '$kind'" >&2
            return 1
            ;;
    esac
}

# Optional source-level syntax check (currently JS only, via node --check).
# Echoes errors prefixed with the supplied human label and returns non-zero
# on failure. Always succeeds for kinds without a checker.
syntax_check_source() {
    local kind="$1"
    local src="$2"
    local label="$3"
    [[ "$kind" != "js" ]] && return 0
    command -v node &> /dev/null || return 0
    if ! node --check "$src" 2>/dev/null; then
        echo "  ✗ $label has syntax errors:"
        node --check "$src" 2>&1 | sed 's/^/    /'
        return 1
    fi
    return 0
}

# Process every source file of one kind (css|js): minify, gzip, and store
# results into the kind-specific associative arrays. Handles chunked bundles,
# plain bundles, and standalone files identically across CSS and JS.
process_asset_kind() {
    local kind="$1"
    local -n files_arr="$2"
    local upper="${kind^^}"
    local -n contents_ref="${upper}_CONTENTS"
    local -n gzip_ref="${upper}_GZIP_CONTENTS"
    local -n chunk_flags_ref="${upper}_CHUNK_FLAGS"
    local -n chunk_names_ref="${upper}_CHUNK_NAMES"
    local -n bundle_chunks_ref="${upper}_BUNDLE_CHUNKS"

    local src_file raw_name filename bundle_manifest
    for src_file in "${files_arr[@]}"; do
        raw_name=$(basename "$src_file" ".${kind}")
        filename="${raw_name//[.-]/_}"
        bundle_manifest="${src_file}.bundle"

        # --- Chunked bundle path (manifest contains [chunk:NAME ...] markers) ---
        if bundle_has_chunk_markers "$bundle_manifest"; then
            echo "Bundling ${upper} (chunked): $raw_name.${kind} (from $(basename "$bundle_manifest"))..."

            local chunk_paths=() chunk_flags=() chunk_names=()
            if ! concatenate_bundle_chunked "$bundle_manifest" chunk_paths chunk_flags chunk_names; then
                echo "  ✗ Failed to parse bundle manifest $bundle_manifest"
                exit 1
            fi

            local chunk_keys=""
            local ci chunk_path chunk_flag chunk_name chunk_key
            local chunk_orig_size chunk_minified chunk_min_size chunk_gzipped chunk_gz_size
            for ci in "${!chunk_paths[@]}"; do
                chunk_path="${chunk_paths[$ci]}"
                chunk_flag="${chunk_flags[$ci]}"
                chunk_name="${chunk_names[$ci]}"
                chunk_key="${filename}_${chunk_name}"

                if ! syntax_check_source "$kind" "$chunk_path" "Bundle chunk [${chunk_name}] (${chunk_flag:-always})"; then
                    rm -f "${chunk_paths[@]}"
                    exit 1
                fi

                echo "  Minifying chunk [${chunk_name}] (${chunk_flag:-always})..."
                chunk_orig_size=$(wc -c <"$chunk_path")
                chunk_minified=$(minify_source "$kind" "$chunk_path")
                rm -f "$chunk_path"

                contents_ref["$chunk_key"]="$chunk_minified"
                chunk_flags_ref["$chunk_key"]="$chunk_flag"
                chunk_names_ref["$chunk_key"]="$chunk_name"
                chunk_min_size=$(echo -n "$chunk_minified" | wc -c)

                chunk_gzipped=$(gzip_to_c_array "$chunk_minified")
                gzip_ref["$chunk_key"]="$chunk_gzipped"
                chunk_gz_size=$(echo -n "$chunk_minified" | gzip -9 -c | wc -c)

                ORIGINAL_SIZES["${kind}_${chunk_key}"]=$chunk_orig_size
                PROCESSED_SIZES["${kind}_${chunk_key}"]=$chunk_min_size
                GZIPPED_SIZES["${kind}_${chunk_key}"]=$chunk_gz_size

                chunk_keys+="$chunk_key "
            done
            bundle_chunks_ref["$filename"]="${chunk_keys% }"
            continue
        fi

        # --- Plain bundle / single-file path ---
        local source="$src_file"
        if concatenate_bundle "$bundle_manifest" source; then
            echo "Bundling ${upper}: $raw_name.${kind} (from $(basename "$bundle_manifest"))..."
            if ! syntax_check_source "$kind" "$source" "Concatenated bundle $raw_name.${kind}"; then
                rm -f "$source"
                exit 1
            fi
        fi

        echo "Minifying ${upper}: $raw_name.${kind}..."
        local content original_size minified minified_size gzipped gzipped_size
        content=$(cat "$source")
        original_size=$(echo -n "$content" | wc -c)
        minified=$(minify_source "$kind" "$source")

        # Cleanup temp file if it was a bundle
        [[ "$source" != "$src_file" ]] && rm -f "$source"

        contents_ref["$filename"]="$minified"
        minified_size=$(echo -n "$minified" | wc -c)

        gzipped=$(gzip_to_c_array "$minified")
        gzip_ref["$filename"]="$gzipped"
        gzipped_size=$(echo -n "$minified" | gzip -9 -c | wc -c)

        ORIGINAL_SIZES["${kind}_${filename}"]=$original_size
        PROCESSED_SIZES["${kind}_${filename}"]=$minified_size
        GZIPPED_SIZES["${kind}_${filename}"]=$gzipped_size
    done
}

# Emit chunked-bundle variants for one kind (css|js) under a single PROGMEM
# symbol name with #if guards. Variant enumeration is delegated to
# enumerate_bundle_variants(), which treats IS_* device-class flags as one
# mutually-exclusive group (see its header), so the variant count is
# 2^k * (c + 1) rather than a pessimistic 2^N. Exactly one branch matches
# per board build, so device-class assets contribute nothing to non-matching
# boards.
emit_chunked_variants() {
    local kind="$1"
    local upper="${kind^^}"
    local -n bundle_chunks_ref="${upper}_BUNDLE_CHUNKS"
    local -n chunk_flags_ref="${upper}_CHUNK_FLAGS"
    local -n contents_ref="${upper}_CONTENTS"

    local bundle_name chunks ckey f n_flags n_indep n_classes n_variants
    local if_expr active_flags cflag include
    local variant_tmp variant_orig_size variant_gz_size variant_gz vnum
    for bundle_name in "${!bundle_chunks_ref[@]}"; do
        chunks="${bundle_chunks_ref[$bundle_name]}"

        local -a unique_flags=()
        local -A seen_flags=()
        for ckey in $chunks; do
            f="${chunk_flags_ref[$ckey]:-}"
            if [[ -n "$f" && -z "${seen_flags[$f]:-}" ]]; then
                unique_flags+=("$f")
                seen_flags["$f"]=1
            fi
        done
        n_flags=${#unique_flags[@]}

        # Reachable variant count: independent flags expand 2^k, the IS_*
        # device-class group expands (c + 1)-ways (one per class + "none").
        n_classes=0
        for f in "${unique_flags[@]}"; do
            is_device_class_flag "$f" && n_classes=$((n_classes + 1))
        done
        n_indep=$((n_flags - n_classes))
        n_variants=$(( (1 << n_indep) * (n_classes + 1) ))

        # Backstop against runaway header growth. With the exclusive-group
        # model only independent flags expand exponentially, so this cap is
        # far harder to hit than the old 2^N one; it exists to catch a bundle
        # that accidentally accumulates many independent flags.
        if [[ $n_variants -gt 32 ]]; then
            echo "  ✗ Bundle $bundle_name expands to $n_variants variants" \
                 "(${n_indep} independent flag(s), ${n_classes} device-class flag(s); max 32)." >&2
            echo "     Independent flags (HAS_*) each double the gzipped variants emitted into" >&2
            echo "     web_assets.h; device-class flags (IS_*) add only one variant each." >&2
            echo "     Consolidate independent chunk flags, or split the bundle." >&2
            unset unique_flags seen_flags
            exit 1
        fi

        echo "Building variants for ${bundle_name}_${kind}" \
             "(${n_indep} independent + ${n_classes} device-class flag(s), ${n_variants} variant(s))..."

        cat >> "$OUTPUT_FILE" << EOF

// ${upper} bundle variants for ${bundle_name}.${kind}
// Chunks: $(echo $chunks)
// Unique flags: ${unique_flags[*]:-<none>}
// IS_* device-class flags form one mutually-exclusive group.
// Each board build matches exactly one #if branch below.
EOF

        vnum=0
        while IFS=$'\t' read -r if_expr active_flags; do
            vnum=$((vnum + 1))

            variant_tmp=$(mktemp /tmp/bundle_variant_XXXXXX)
            for ckey in $chunks; do
                cflag="${chunk_flags_ref[$ckey]:-}"
                include=0
                if [[ -z "$cflag" ]]; then
                    include=1
                else
                    # Included iff this variant has the chunk's flag ON.
                    case " $active_flags " in
                        *" $cflag "*) include=1 ;;
                    esac
                fi
                if [[ $include -eq 1 ]]; then
                    printf '%s\n' "${contents_ref[$ckey]}" >> "$variant_tmp"
                fi
            done

            variant_orig_size=$(wc -c <"$variant_tmp")
            variant_gz_size=$(gzip -9 -c <"$variant_tmp" | wc -c)
            variant_gz=$(gzip_to_c_array "$(cat "$variant_tmp")")
            rm -f "$variant_tmp"

            if [[ "$if_expr" == "ALWAYS" ]]; then
                cat >> "$OUTPUT_FILE" << EOF
const uint8_t ${bundle_name}_${kind}_gz[] PROGMEM = {
${variant_gz}
};
const size_t ${bundle_name}_${kind}_gz_len = sizeof(${bundle_name}_${kind}_gz);

EOF
            else
                cat >> "$OUTPUT_FILE" << EOF
#if ${if_expr}
const uint8_t ${bundle_name}_${kind}_gz[] PROGMEM = {
${variant_gz}
};
const size_t ${bundle_name}_${kind}_gz_len = sizeof(${bundle_name}_${kind}_gz);
#endif // ${if_expr}

EOF
            fi
            printf "  variant %d/%d [%s]: %d -> %d bytes\n" "$vnum" "$n_variants" "${if_expr/ALWAYS/always}" "$variant_orig_size" "$variant_gz_size"
        done < <(enumerate_bundle_variants "${unique_flags[@]}")

        unset unique_flags seen_flags
    done
}

# Process HTML files (template substitution + minification)
for html_file in "${HTML_FILES[@]}"; do
    filename=$(basename "$html_file" .html)
    echo "Processing HTML: $filename.html..."
    content=$(cat "$html_file")
    original_size=$(echo -n "$content" | wc -c)

    # Template substitution and minification (see tools/_render_html_template.py).
    minified=$(python3 "$SCRIPT_DIR/_render_html_template.py" \
        --web-dir "$WEB_DIR" \
        --input "$html_file" \
        --project-name "$PROJECT_NAME" \
        --project-display-name "$PROJECT_DISPLAY_NAME")
    
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

    minified=$(python3 "$SCRIPT_DIR/_render_html_template.py" \
        --web-dir "$WEB_DIR" \
        --input "$fragment_file" \
        --project-name "$PROJECT_NAME" \
        --project-display-name "$PROJECT_DISPLAY_NAME")
    
    FRAGMENT_CONTENTS["$filename"]="$minified"
    minified_size=$(echo -n "$minified" | wc -c)
    
    gzipped=$(gzip_to_c_array "$minified")
    FRAGMENT_GZIP_CONTENTS["$filename"]="$gzipped"
    gzipped_size=$(echo -n "$minified" | gzip -9 -c | wc -c)
    
    ORIGINAL_SIZES["frag_$filename"]=$original_size
    PROCESSED_SIZES["frag_$filename"]=$minified_size
    GZIPPED_SIZES["frag_$filename"]=$gzipped_size
done

# Per-chunk feature-flag map for chunked CSS bundles. Keys are CSS_CONTENTS
# keys (e.g. "portal_all_core"); values are HAS_*/IS_* macro names (or empty
# for always-on). Mirrors the JS chunked path so device-class CSS can be
# gated out of non-matching boards without per-chunk HTTP fetches.
declare -A CSS_CHUNK_FLAGS
declare -A CSS_CHUNK_NAMES
declare -A CSS_BUNDLE_CHUNKS

# Per-chunk feature-flag map for chunked JS bundles. Keys are JS_CONTENTS keys
# (e.g. "portal_core"); values are HAS_* macro names (or empty for always-on).
declare -A JS_CHUNK_FLAGS
# Per-chunk human name (e.g. "core", "pad"); keyed by JS_CONTENTS key.
declare -A JS_CHUNK_NAMES
# Map from bundle primary key (e.g. "portal") to space-separated chunk keys
# in concatenation order. Used during header emission to write the chunks table.
declare -A JS_BUNDLE_CHUNKS

# Process CSS and JS sources. Both kinds use the same pipeline:
#   bundle (chunked or plain) -> minify -> gzip -> store in kind-specific maps.
process_asset_kind css CSS_FILES
process_asset_kind js JS_FILES


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

# Map an asset filename stem (fragment stem like "pad_editor_fragment" or
# JS stem like "epaper_init") to the compile-time feature flag that must
# be true for the asset to be included in the build. Echoes nothing for
# always-on assets. The fragment and JS stem namespaces are disjoint, so a
# single mapping table handles both.
#
# CSS chunking convention: chunked CSS files in portal-all.css.bundle use
# the marker `[chunk:<full_class_name> IS_<CLASS>]` (full class name, e.g.
# `coffee_scale`, never abbreviations like `scale`) so chunk names never
# collide as more device classes land. Same naming applies to any future
# chunked-JS bundle.
asset_feature_flag() {
    local stem="$1"
    # Strip trailing _fragment suffix (no-op for JS stems).
    stem="${stem%_fragment}"
    case "$stem" in
        pad_editor|swipe_actions|boot_actions|button_defaults|timers|brightness|screen_preview|screensaver)
            echo "HAS_DISPLAY" ;;
        mqtt|ha_discovery)
            echo "HAS_MQTT" ;;
        ble)
            echo "HAS_BLE_HID" ;;
        volume)
            echo "HAS_AUDIO" ;;
        sounds)
            echo "HAS_SOUND_PLAYER" ;;
        epaper_status|epaper_image|epaper_overlay|epaper_vcom|epaper_init)
            echo "HAS_EPAPER" ;;
        shutter|shutter_tests|shutter_sessions|shutter_session_actions)
            echo "IS_SHUTTER_TESTER" ;;
        scale|brews|brew_templates|portal_action_editor_scale|portal_brews|portal_brews_charts|portal_brews_init|portal_brews_templates)
            echo "IS_COFFEE_SCALE" ;;
        darkroom|prints|portal_action_editor_darkroom|portal_darkroom_init|portal_prints|portal_darkroom)
            echo "IS_DARKROOM_TIMER" ;;
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
    flag=$(asset_feature_flag "$filename")
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

# Generate CSS sections (gzipped). Chunked-bundle keys are emitted as
# per-variant blobs in a separate pass below (one combined PROGMEM array
# per build), so skip them here.
for filename in "${!CSS_CONTENTS[@]}"; do
    if [[ -n "${CSS_CHUNK_NAMES[$filename]:-}" ]]; then
        continue
    fi
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
    js_flag=$(asset_feature_flag "$filename")
    if [[ -n "$js_flag" ]]; then
        cat >> "$OUTPUT_FILE" << EOF
// JavaScript from src/app/web/${filename}.js (minified + gzipped)
#if $js_flag
const uint8_t ${filename}_js_gz[] PROGMEM = {
${JS_GZIP_CONTENTS[$filename]}
};
#endif // $js_flag

EOF
    else
        cat >> "$OUTPUT_FILE" << EOF
// JavaScript from src/app/web/${filename}.js (minified + gzipped)
const uint8_t ${filename}_js_gz[] PROGMEM = {
${JS_GZIP_CONTENTS[$filename]}
};

EOF
    fi
done

# Add size constants (gzipped sizes)
cat >> "$OUTPUT_FILE" << 'SIZE_CONSTANTS'

// Asset sizes (gzipped, calculated at compile time)
SIZE_CONSTANTS

for filename in "${!HTML_CONTENTS[@]}"; do
    echo "const size_t ${filename}_html_gz_len = sizeof(${filename}_html_gz);" >> "$OUTPUT_FILE"
done

for filename in "${!FRAGMENT_CONTENTS[@]}"; do
    flag=$(asset_feature_flag "$filename")
    if [[ -n "$flag" ]]; then
        echo "#if $flag" >> "$OUTPUT_FILE"
        echo "const size_t ${filename}_html_gz_len = sizeof(${filename}_html_gz);" >> "$OUTPUT_FILE"
        echo "#endif // $flag" >> "$OUTPUT_FILE"
    else
        echo "const size_t ${filename}_html_gz_len = sizeof(${filename}_html_gz);" >> "$OUTPUT_FILE"
    fi
done

for filename in "${!CSS_CONTENTS[@]}"; do
    if [[ -n "${CSS_CHUNK_NAMES[$filename]:-}" ]]; then
        continue
    fi
    echo "const size_t ${filename}_css_gz_len = sizeof(${filename}_css_gz);" >> "$OUTPUT_FILE"
done

for filename in "${!JS_CONTENTS[@]}"; do
    if [[ -n "${JS_CHUNK_NAMES[$filename]:-}" ]]; then
        continue
    fi
    js_flag=$(asset_feature_flag "$filename")
    if [[ -n "$js_flag" ]]; then
        echo "#if $js_flag" >> "$OUTPUT_FILE"
        echo "const size_t ${filename}_js_gz_len = sizeof(${filename}_js_gz);" >> "$OUTPUT_FILE"
        echo "#endif // $js_flag" >> "$OUTPUT_FILE"
    else
        echo "const size_t ${filename}_js_gz_len = sizeof(${filename}_js_gz);" >> "$OUTPUT_FILE"
    fi
done

# Emit chunked JS+CSS bundle variants. Each bundle expands to 2^k * (c + 1)
# #if-guarded PROGMEM arrays under one symbol (k independent flags, c mutually
# exclusive IS_* device-class flags); exactly one branch matches per build, so
# device-class assets contribute nothing to non-matching boards.
emit_chunked_variants js
emit_chunked_variants css

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
        flag=$(asset_feature_flag "$filename")
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
