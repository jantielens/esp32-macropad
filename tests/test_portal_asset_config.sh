#!/usr/bin/env bash
# Verify actual preprocessing, explicit opt-outs, and fail-closed asset selection.
set -euo pipefail
cd "$(dirname "$0")/.."
python3 - <<'PY'
import json
from pathlib import Path
import subprocess
import sys
import tempfile

sys.path.insert(0, "tools")
from portal_assets import compiler_from_properties, resolve_conditions, source_blocks

compiler = ["c++", "-Isrc/app", "-DCONFIG_IDF_TARGET_ESP32S3", "-DBOARD_HAS_PSRAM",
            "-DHAS_DISPLAY=1", "-DHAS_TOUCH=0", "-DHAS_USB_HID=0", "-DHAS_BLE_HID=1"]
expressions = ["HAS_DISPLAY", "HAS_TOUCH", "HAS_USB_HID || HAS_BLE_HID",
               "HAS_DISPLAY && HAS_TOUCH && (HAS_USB_HID || HAS_BLE_HID)"]
expressions.extend(f"HAS_DISPLAY && !HAS_TOUCH && ({' || '.join(['HAS_BLE_HID'] * count)})"
                   for count in range(1, 15))
resolved = resolve_conditions(compiler, expressions)
assert resolved["HAS_DISPLAY"] and not resolved["HAS_TOUCH"]
assert resolved["HAS_USB_HID || HAS_BLE_HID"]
assert not resolved[expressions[3]]
assert all(resolved[expression] for expression in expressions[4:])
assert resolve_conditions(compiler, ["HAS_PROGRESS_WIDGET"])["HAS_PROGRESS_WIDGET"]
assert source_blocks("shared\n// @portal-if HAS_TOUCH\ntouch\n// @portal-endif\n",
                     resolved.__getitem__) == "shared\n"
for text in ("// @portal-endif\n", "// @portal-if HAS_TOUCH\n", "// @portal-iff HAS_TOUCH\n"):
    try:
        source_blocks(text, resolved.__getitem__)
    except ValueError:
        pass
    else:
        raise AssertionError("Malformed feature block accepted")
try:
    resolve_conditions(compiler, ["HAS_MISSPELLED_FEATURE"])
except subprocess.CalledProcessError as error:
    assert "Unknown portal gate" in error.stderr
else:
    raise AssertionError("Unknown gate accepted")

with tempfile.TemporaryDirectory() as directory:
    properties = Path(directory) / "build.properties"
    properties.write_text('recipe.cpp.o.pattern=c++ -c -MMD -DBOARD_HAS_PSRAM '
                          '-DHAS_TOUCH=0 {includes} "@generated/build_opt.h" '
                          '"@generated/file_opts" "{source_file}" -o "{object_file}"\n'
                          'build.core.path=core with spaces\n'
                          'build.opt.path=generated/build_opt.h\n'
                          'file_opts.path=generated/file_opts\n')
    command = compiler_from_properties(properties)
    assert "-Icore with spaces" in command
    assert "-DHAS_TOUCH=0" in command and "-DBOARD_HAS_PSRAM" in command
    assert not any("generated/" in argument for argument in command)
    assert "-c" not in command and "-o" not in command
print("PASS: compiler-resolved portal asset configuration")
PY
