#!/usr/bin/env python3
import json
import pathlib
import subprocess
import tempfile


root = pathlib.Path(__file__).resolve().parents[1]
subprocess.run(
    ["c++", "-std=c++11", "-Wall", "-Wextra", "-Werror", "-fsyntax-only",
     "-x", "c++", "-", "-I", str(root),
     "-I", str(root / "src/boards/crowpanel-4.2-interactive"),
     "-DBOARD_HAS_OVERRIDE", "-DBOARD_HAS_PSRAM", "-DCONFIG_IDF_TARGET_ESP32S3=1"],
    input='''#include "src/app/board_config.h"
static_assert(HAS_DISPLAY && HAS_LVGL_EPAPER && HAS_EPAPER_PANEL, "Interactive profile required");
static_assert(!HAS_TOUCH && !IS_EPAPER_FRAME && !HAS_EPAPER_GRAYSCALE, "Monochrome non-touch profile required");
static_assert(NUM_HW_BUTTONS == 6 && MAX_HW_BUTTONS >= 6, "All six inputs required");
static_assert(HW_BUTTON_DEFS[0].pin == 2 && HW_BUTTON_DEFS[1].pin == 1, "Menu and Back pins");
static_assert(HW_BUTTON_DEFS[2].pin == 6 && HW_BUTTON_DEFS[3].pin == 4 && HW_BUTTON_DEFS[4].pin == 5, "Jog switch pins");
static_assert(HW_BUTTON_DEFS[5].pin == 0 && BUTTON_PIN == 2, "Boot strap and configuration pins");
''', text=True, check=True,
)
print("PASS: crowpanel-4.2-interactive")
with tempfile.TemporaryDirectory() as directory:
    executable = pathlib.Path(directory) / "button-defaults"
    subprocess.run(
        ["c++", "-std=c++11", "-Wall", "-Wextra", "-Werror", "-x", "c++", "-",
         "-I", str(root), "-I", str(root / "src/boards/crowpanel-4.2-interactive"),
         "-DBOARD_HAS_OVERRIDE", "-DBOARD_HAS_PSRAM", "-DCONFIG_IDF_TARGET_ESP32S3=1",
         "-o", str(executable)],
        input='''#include "src/app/board_config.h"
#include <cstdio>
int main() { std::puts(HW_BUTTON_DEFAULTS_JSON); }
''', text=True, check=True,
    )
    defaults = json.loads(subprocess.check_output([str(executable)], text=True))["buttons"]
    assert defaults == [
        {}, {},
        {"tap_actions": [{"type": "button_nav", "command": "previous"}]},
        {"tap_actions": [{"type": "button_nav", "command": "next"}]},
        {"tap_actions": [{"type": "button_nav", "command": "tap"}],
         "hold_actions": [{"type": "button_nav", "command": "tap_hold"}]},
        {"tap_actions": [{"type": "display_refresh", "mode": "full"}]},
    ]
print("PASS: CrowPanel generic hardware action defaults")
for grayscale in (0, 1):
    subprocess.run(
        ["c++", "-std=c++11", "-Wall", "-Wextra", "-Werror", "-fsyntax-only",
         "-x", "c++", "-", "-I", str(root), "-I", str(root / "tests"),
         "-DHAS_DISPLAY=1", "-DHAS_EPAPER_PANEL=1", "-DHAS_LVGL_EPAPER=1",
         f"-DHAS_EPAPER_GRAYSCALE={grayscale}"],
        input='''#include "src/app/config_manager.h"
static_assert(config_manager_epaper_mode_supported(EPAPER_RENDER_MODE_BW), "B/W required");
static_assert(config_manager_epaper_mode_supported(EPAPER_RENDER_MODE_GRAYSCALE) == HAS_EPAPER_GRAYSCALE, "Grayscale capability required");
static_assert(!config_manager_epaper_mode_supported(255), "Unknown mode rejected");
''', text=True, check=True,
    )
print("PASS: monochrome and grayscale panel mode capabilities")
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
static_assert(!HAS_FILESYSTEM, "OTA-only C3 has no filesystem partition");
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

with tempfile.TemporaryDirectory() as directory:
    pathlib.Path(directory, "sdkconfig.h").write_text("")
    for target in ("ESP32", "ESP32S3", "ESP32C3", "ESP32C6", "ESP32P4", None):
        for override in (None, 12288):
            expected = override or (8192 if target in ("ESP32", "ESP32S3") else 16384)
            flags = [f"-DCONFIG_IDF_TARGET_{target}=1"] if target else []
            if override:
                flags.append(f"-DCONFIG_ASYNC_TCP_STACK_SIZE={override}")
            subprocess.run(
                ["c++", "-std=c++11", "-Wall", "-Wextra", "-Werror", "-fsyntax-only",
                 "-x", "c++", "-", "-I", directory, *flags,
                 "-include", str(root / "src/app/async_tcp_config.h")],
                input=f'''#ifndef CONFIG_ASYNC_TCP_STACK_SIZE
#define CONFIG_ASYNC_TCP_STACK_SIZE 16384
#endif
static_assert(CONFIG_ASYNC_TCP_STACK_SIZE == {expected}, "Unexpected AsyncTCP stack size");
''', text=True, check=True,
            )
assert "-include $SCRIPT_DIR/src/app/async_tcp_config.h" in (root / "build.sh").read_text()
print("PASS: AsyncTCP target defaults and explicit overrides")

audio_cases = 0
for target in ("ESP32", "ESP32S3", "ESP32C3", "ESP32C6", "ESP32P4", None):
    for sound_player in (0, 1):
        for psram_scratch in (0, 1):
            for override in (None, 12288):
                expected = override or (
                    8192 if target in ("ESP32", "ESP32S3") and
                    (not sound_player or psram_scratch) else 24576
                )
                flags = [f"-DCONFIG_IDF_TARGET_{target}=1"] if target else []
                if override:
                    flags.append(f"-DAUDIO_TASK_STACK_SIZE={override}")
                subprocess.run(
                    ["c++", "-std=c++11", "-Wall", "-Wextra", "-Werror", "-fsyntax-only",
                     "-x", "c++", "-", "-I", str(root), "-DHAS_AUDIO=1",
                     f"-DHAS_SOUND_PLAYER={sound_player}",
                     f"-DAUDIO_MP3_SCRATCH_PSRAM={psram_scratch}", *flags],
                    input=f'''#include "src/app/board_config.h"
static_assert(AUDIO_TASK_STACK_SIZE == {expected}, "Unexpected audio stack size");
''', text=True, check=True,
                )
                audio_cases += 1
print(f"PASS: {audio_cases} audio target, MP3 scratch, and override combinations")

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