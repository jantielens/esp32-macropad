#!/bin/bash
# Verifies versioned static asset URLs and their immutable response policy.
set -e
cd "$(dirname "$0")/.."

version_part() {
    sed -nE "s/^[[:space:]]*#define[[:space:]]+$1[[:space:]]+([0-9]+)[[:space:]]*$/\\1/p" \
        src/version.h | head -n 1
}

version="$(version_part VERSION_MAJOR).$(version_part VERSION_MINOR).$(version_part VERSION_PATCH)"
rendered_shell="$(mktemp)"
trap 'rm -f "$rendered_shell"' EXIT

python3 tools/_render_html_template.py \
    --web-dir src/app/web \
    --input src/app/web/shell.html \
    --project-name esp32-macropad \
    --project-display-name 'ESP32 Macropad' \
    --firmware-version "$version" > "$rendered_shell"

grep -Fq "href=\"/portal-all.css?v=$version\"" "$rendered_shell"
grep -Fq "src=\"/portal.js?v=$version\"" "$rendered_shell"
grep -Fq '"/portal-camera.js?v=" FIRMWARE_VERSION' src/app/components/camera_component.cpp
grep -Fq '"/portal-camera.css?v=" FIRMWARE_VERSION' src/app/components/camera_component.cpp
grep -Fq '"public, max-age=31536000, immutable"' src/app/web_portal_pages.cpp
test "$(grep -c 'kImmutableAssetCacheControl' src/app/web_portal_pages.cpp)" -eq 5

echo "PASS: portal asset cache policy"