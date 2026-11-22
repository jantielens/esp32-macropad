#!/bin/bash
# Verifies build-specific asset URLs and their immutable response policy.
set -euo pipefail
cd "$(dirname "$0")/.."

version_part() {
    sed -nE "s/^[[:space:]]*#define[[:space:]]+$1[[:space:]]+([0-9]+)[[:space:]]*$/\\1/p" \
        src/version.h | head -n 1
}

version="$(version_part VERSION_MAJOR).$(version_part VERSION_MINOR).$(version_part VERSION_PATCH)"
build_id="1795298400123456789"
rendered_shell="$(mktemp)"
trap 'rm -f "$rendered_shell"' EXIT

python3 tools/_render_html_template.py \
    --web-dir src/app/web \
    --input src/app/web/shell.html \
    --project-name esp32-macropad \
    --project-display-name 'ESP32 Macropad' \
    --firmware-version "$version" \
    --asset-build-id "$build_id" \
    --with-auto-shutdown --with-microphone-streaming > "$rendered_shell"

grep -Fq "href=\"/portal-all.css?v=$build_id\"" "$rendered_shell"
grep -Fq "src=\"/portal.js?v=$build_id\"" "$rendered_shell"
grep -Fq "src=\"/portal_power_savings.js?v=$build_id\"" "$rendered_shell"
grep -Fq "src=\"/portal-live-microphone.js?v=$build_id\"" "$rendered_shell"
grep -Fq '"public, max-age=31536000, immutable"' src/app/web_portal_pages.cpp

python3 - <<'PY'
import gzip
import json
from pathlib import Path
import re
import sys
import tempfile

sys.path.insert(0, "tools")
from portal_assets import generate

compiler = ["c++", "-Isrc/app", "-Itests", "-Isrc/boards/esp32c3-withsensors",
            "-DBOARD_HAS_OVERRIDE", "-DCONFIG_IDF_TARGET_ESP32C3"]
with tempfile.TemporaryDirectory() as directory:
    output = Path(directory)
    ids = []
    for _ in range(2):
        generate(compiler, output, "esp32-macropad", "ESP32 Macropad")
        build_id = json.loads((output / "portal-assets.json").read_text())["build_id"]
        ids.append(build_id)
        assert re.fullmatch(r"[0-9]+", build_id)
        assert f'#define PORTAL_ASSET_BUILD_ID "{build_id}"' in (
            output / "portal_asset_version.h").read_text()
        header = (output / "web_assets.h").read_text()
        array = re.search(r"const uint8_t shell_html_gz\[\] PROGMEM = \{(.*?)\};",
                          header, re.DOTALL)[1]
        shell = gzip.decompress(bytes(int(value, 16) for value in
                                      re.findall(r"0x([0-9a-f]{2})", array))).decode()
        assert f'/portal.js?v={build_id}' in shell
        assert f'/portal-all.css?v={build_id}' in shell
        assert "{{" not in shell
    assert ids[0] != ids[1], "Unchanged rebuilds must invalidate old asset URLs"
PY

echo "PASS: portal asset cache policy"