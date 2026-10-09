#!/usr/bin/env python3
import json
import pathlib
import subprocess
import tempfile


root = pathlib.Path(__file__).resolve().parents[1]
source = '''#include "src/app/board_config.h"
static_assert(HAS_USB_HID, "USB HID must remain enabled");
static_assert(!ALARM_ENABLED, "Alarms must be disabled");
static_assert(HAS_NATIVE_EXTENSIONS, "Extensions must remain enabled");
static_assert(MALLOC_PSRAM_THRESHOLD_BYTES == 512, "Malloc threshold must be 512 bytes");
'''

for board in ("jc3248w535", "jc3636w518", "jc3636w518-sd"):
    command = [
        "c++", "-std=c++11", "-Wall", "-Wextra", "-Werror",
        "-fsyntax-only", "-x", "c++", "-", "-I", str(root),
        "-I", str(root / "src/boards" / board),
        "-DCONFIG_IDF_TARGET_ESP32S3=1", "-DBOARD_HAS_PSRAM", "-DBOARD_HAS_OVERRIDE",
    ]
    subprocess.run(command, input=source, text=True, check=True)
    metadata = json.loads((root / "src/boards" / board / "metadata.json").read_text())
    assert "usb_hid" in metadata["capabilities"]
    assert "extensions" in metadata["capabilities"]
    print(f"PASS: {board}")

board = "esp32c3-withsensors"
subprocess.run(
    ["c++", "-std=c++11", "-Wall", "-Wextra", "-Werror",
     "-fsyntax-only", "-x", "c++", "-", "-I", str(root),
     "-I", str(root / "src/boards" / board),
     "-DCONFIG_IDF_TARGET_ESP32C3=1", "-DBOARD_HAS_OVERRIDE"],
    input='''#include "src/app/board_config.h"
static_assert(!HAS_MCP, "MCP must be disabled to preserve OTA flash space");
static_assert(HAS_BLE, "BLE telemetry must remain enabled");
static_assert(!HAS_MQTT, "MQTT must be disabled to preserve OTA flash space");
static_assert(HAS_SENSOR_DUMMY, "Sample sensor must remain enabled");
''',
    text=True, check=True,
)
metadata = json.loads((root / "src/boards" / board / "metadata.json").read_text())
assert "mcp" not in metadata["capabilities"]
assert "bthome" in metadata["capabilities"]
assert "mqtt" not in metadata["capabilities"]
print(f"PASS: {board}")

policy_source = '''#include "src/app/board_config.h"
static_assert(MALLOC_PSRAM_THRESHOLD_BYTES == EXPECTED_THRESHOLD, "Unexpected malloc policy");
'''
policy_cases = []
for target in ("ESP32", "ESP32S3", "ESP32P4", "ESP32C3", "ESP32C6", None):
    for has_psram in (False, True):
        flags = [f"-DCONFIG_IDF_TARGET_{target}=1"] if target else []
        if has_psram:
            flags.append("-DBOARD_HAS_PSRAM")
        expected = 512 if has_psram and target in ("ESP32", "ESP32S3") else 0
        policy_cases.append((flags, expected))
policy_cases.extend([
    (["-DCONFIG_IDF_TARGET_ESP32S3=1", "-DBOARD_HAS_PSRAM", "-DHAS_PSRAM=0"], 0),
    (["-DCONFIG_IDF_TARGET_ESP32=1", "-DHAS_PSRAM=1"], 512),
    (["-DCONFIG_IDF_TARGET_ESP32S3=1", "-DBOARD_HAS_PSRAM", "-DMALLOC_PSRAM_THRESHOLD_BYTES=0"], 0),
    (["-DCONFIG_IDF_TARGET_ESP32=1", "-DBOARD_HAS_PSRAM", "-DMALLOC_PSRAM_THRESHOLD_BYTES=1024"], 1024),
])
for flags, expected in policy_cases:
    subprocess.run(
        ["c++", "-std=c++11", "-Wall", "-Wextra", "-Werror", "-fsyntax-only", "-x", "c++", "-",
         "-I", str(root), f"-DEXPECTED_THRESHOLD={expected}", *flags],
        input=policy_source, text=True, check=True,
    )
print(f"PASS: {len(policy_cases)} target, PSRAM, and override combinations")

setup_prefix = (root / "src/app/app.ino").read_text().split("void setup()", 1)[1].split("dma2d_arbiter_init();", 1)[0]
runtime_source = '''#include <cassert>
#include <cstddef>
static bool psram_present = false;
static unsigned policy_calls = 0;
bool psramFound() { return psram_present; }
void heap_caps_malloc_extmem_enable(size_t threshold) {
    assert(threshold == 512);
    ++policy_calls;
}
void setup()
''' + setup_prefix + '''}
int main() {
    setup();
    assert(policy_calls == 0);
    psram_present = true;
    setup();
    assert(policy_calls == EXPECTED_CALLS);
}
'''
with tempfile.TemporaryDirectory() as directory:
    executable = str(pathlib.Path(directory) / "malloc-policy")
    for has_psram, threshold, calls in ((1, 512, 1), (0, 512, 0), (1, 0, 0)):
        subprocess.run(
            ["c++", "-std=c++11", "-Wall", "-Wextra", "-Werror", "-x", "c++", "-",
             f"-DHAS_PSRAM={has_psram}", f"-DMALLOC_PSRAM_THRESHOLD_BYTES={threshold}",
             f"-DEXPECTED_CALLS={calls}", "-o", executable],
            input=runtime_source, text=True, check=True,
        )
        subprocess.run([executable], check=True)
print("PASS: malloc policy startup guards")