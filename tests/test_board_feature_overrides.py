#!/usr/bin/env python3
import csv
import json
import pathlib
import subprocess
import tempfile


root = pathlib.Path(__file__).resolve().parents[1]
arduinojson = pathlib.Path.home() / "Arduino/libraries/ArduinoJson/src"
fixture = json.loads((root / "tools/mock-data/pad-editor.json").read_text())
appearance_macros = {
    "default_pad_bg_color": "DEFAULT_PAD_BG_COLOR",
    "bg_color": "DEFAULT_BUTTON_BG_COLOR", "fg_color": "DEFAULT_BUTTON_FG_COLOR",
    "border_color": "DEFAULT_BUTTON_BORDER_COLOR", "border_width": "DEFAULT_BUTTON_BORDER_WIDTH",
    "corner_radius": "DEFAULT_BUTTON_CORNER_RADIUS", "content_pad": "DEFAULT_BUTTON_CONTENT_PAD",
    "pixel_shift_distance_px": "DEFAULT_PIXEL_SHIFT_DISTANCE",
}
for board, target in (("m5stack-stopwatch", "ESP32S3"), ("crowpanel-4.2-interactive", "ESP32S3"),
                      ("waveshare-7.8-interactive", "ESP32S3"),
                      ("inkplate6flick-interactive", "ESP32"), ("reterminal-e1003-interactive", "ESP32S3"),
                      ("reterminal-sticky-interactive", "ESP32S3"), ("reterminal-sticky-interactive-sd", "ESP32S3")):
    expected = dict(fixture["firmware_defaults"])
    # Opacity is a runtime default, covered by test_font_routes, not a board macro.
    expected.pop("bg_opacity")
    if board != "m5stack-stopwatch":
        expected.update(fixture["epaper_firmware_defaults"])
    output = []
    for field in expected:
        macro = appearance_macros.get(field, "DEFAULT_" + field.upper())
        if field == "icon_position":
            macro = '(DEFAULT_ICON_POSITION == ICON_POS_LEFT ? "left" : "non-left")'
        elif field == "icon_badge_padding":
            macro = "int(DEFAULT_ICON_BADGE_PADDING)"
        elif field == "button_shadow_type":
            macro = f'({macro} == BUTTON_SHADOW_TYPE_DROP ? "drop" : "opaque")'
        elif field == "button_shadow_color_mode":
            macro = f'({macro} == BUTTON_SHADOW_COLOR_DARKEN_BACKGROUND ? "darken_background" : "fixed")'
        output.append(f'std::cout << {macro} << "\\n";')
    source = '''#include <iostream>
#include "src/app/pad_config.h"
#include "src/app/dialog_appearance.h"
#include "src/app/pad_block.cpp"
static_assert(HAS_FACE_WIDGET, "LCD and interactive e-paper profiles include Face");
static_assert(!HAS_LVGL_EPAPER || DISPLAY_DISABLE_ANIMATIONS, "Other e-paper animations remain disabled");
int main() {
    DialogAppearance dialog;
    if (dialog.text_color != (HAS_LVGL_EPAPER ? 0x000000u : 0xFFFFFFu) ||
        dialog.background_color != (HAS_LVGL_EPAPER ? 0xFFFFFFu : 0x333333u) ||
        dialog.background_pattern_color != (HAS_LVGL_EPAPER ? 0x000000u : 0x111111u) ||
        dialog.border_color != (HAS_LVGL_EPAPER ? 0x000000u : 0x808080u) ||
        dialog.approve_background != (HAS_LVGL_EPAPER ? 0x000000u : 0x2563EBu) ||
        dialog.approve_color != 0xFFFFFFu ||
        dialog.cancel_background != (HAS_LVGL_EPAPER ? 0xFFFFFFu : 0x5F6368u) ||
        dialog.cancel_color != (HAS_LVGL_EPAPER ? 0x000000u : 0xFFFFFFu)) return 1;
    if (BOOT_APPEARANCE_DEFAULT_BACKGROUND_RGB != (HAS_LVGL_EPAPER ? 0xFFFFFFu : 0x000000u) ||
        BOOT_APPEARANCE_DEFAULT_TEXT_RGB != (HAS_LVGL_EPAPER ? 0x000000u : 0x646464u) ||
        DEFAULT_VISUAL_ALERT_RGB != (HAS_LVGL_EPAPER ? 0x000000u : 0xFF0000u)) return 1;
''' + "\n".join(output) + '''
    pad_block_init();
    std::cout << pad_block_catalog()[0]->buttons[1].json << "\\n";
}'''
    with tempfile.TemporaryDirectory() as directory:
        executable = pathlib.Path(directory) / "appearance"
        subprocess.run(["c++", "-std=c++11", "-Wall", "-Wextra", "-Werror", "-x", "c++", "-",
                        "-I", str(root), "-I", str(arduinojson), "-I", str(root / "src/boards" / board),
                        "-DBOARD_HAS_OVERRIDE", "-DBOARD_HAS_PSRAM", f"-DCONFIG_IDF_TARGET_{target}=1",
                        "-o", str(executable)], input=source, text=True, check=True)
        actual = subprocess.check_output([str(executable)], text=True).splitlines()
    system_info = json.loads(actual.pop())
    expected_colors = (["#000000", "#000000", "#000000", "#FFFFFF", "#000000"]
                       if board != "m5stack-stopwatch"
                       else ["#4CAF50", "#2196F3", "#FF5722", "#1A1A1A", "#808080"])
    assert [system_info[key] for key in ("widget_arc_color", "widget_arc_color_2", "widget_arc_color_3",
                                          "widget_gauge_track_color", "widget_gauge_tick_color")] == expected_colors
    for (field, value), actual_value in zip(expected.items(), actual):
        assert actual_value == str(int(value) if isinstance(value, bool) else value), (board, field, actual_value, value)
    assert len(actual) == len(expected)
print("PASS: LCD and e-paper board appearance defaults match portal fixtures")

# A board override must win without changing the preset's independent colors.
override_source = '''#include <iostream>
#include "src/app/dialog_appearance.h"
#include "src/app/pad_block.cpp"
int main() {
    DialogAppearance dialog;
    if (dialog.text_color != 0x123456u || DEFAULT_WIDGET_TRACK_OUTLINE_WIDTH != 0 ||
        std::string(DEFAULT_WIDGET_TRACK_OUTLINE_COLOR) != "#654321" ||
        std::string(DEFAULT_WIDGET_OPTION_SELECTED_FG_COLOR) != "#123456") return 1;
    std::cout << DEFAULT_WIDGET_SERIES_COLOR << "\\n";
    pad_block_init();
    std::cout << pad_block_catalog()[0]->buttons[1].json << "\\n";
}'''
with tempfile.TemporaryDirectory() as directory:
    executable = pathlib.Path(directory) / "overrides"
    subprocess.run(
        ["c++", "-std=c++11", "-Wall", "-Wextra", "-Werror", "-x", "c++", "-",
         "-I", str(root), "-I", str(arduinojson),
         "-I", str(root / "src/boards/reterminal-sticky-interactive"),
         "-DBOARD_HAS_OVERRIDE", "-DBOARD_HAS_PSRAM", "-DCONFIG_IDF_TARGET_ESP32S3=1",
         '-DDEFAULT_WIDGET_SERIES_COLOR="#123456"', "-DDEFAULT_DIALOG_TEXT_RGB=0x123456",
         "-DDEFAULT_WIDGET_TRACK_OUTLINE_WIDTH=0",
         '-DDEFAULT_WIDGET_TRACK_OUTLINE_COLOR="#654321"',
         '-DDEFAULT_WIDGET_OPTION_SELECTED_FG_COLOR="#123456"',
         '-DDEFAULT_SYSTEM_INFO_SERIES_COLOR_3="#654321"', "-o", str(executable)],
        input=override_source, text=True, check=True,
    )
    overridden = subprocess.check_output([str(executable)], text=True).splitlines()
    assert overridden[0] == "#123456"
    preset = json.loads(overridden[1])
    assert preset["widget_arc_color"] == "#000000"
    assert preset["widget_arc_color_3"] == "#654321"
print("PASS: board overrides take precedence without changing independent preset colors")

for profile in ("APPEARANCE_PROFILE_LCD", "APPEARANCE_PROFILE_MONOCHROME", "APPEARANCE_PROFILE_MONOCROME", "0"):
    result = subprocess.run(
        ["c++", "-std=c++11", "-fsyntax-only", "-x", "c++", "-", "-I", str(root),
         "-DAPPEARANCE_PROFILE=" + profile],
        input='#include "src/app/appearance_profile.h"\n', text=True, capture_output=True,
    )
    if profile in ("APPEARANCE_PROFILE_LCD", "APPEARANCE_PROFILE_MONOCHROME"):
        assert result.returncode == 0, result.stderr
    else:
        assert result.returncode != 0 and "Unknown APPEARANCE_PROFILE" in result.stderr, result.stderr
print("PASS: unknown appearance profiles are rejected")

for rtc, frame in ((1, 0), (0, 1), (1, 1)):
    result = subprocess.run(
        ["c++", "-std=c++11", "-Wall", "-Wextra", "-Werror", "-fsyntax-only",
         "-x", "c++", "-", "-I", str(root),
         f"-DHAS_RTC={rtc}", "-DRTC_DRIVER=" + ("RTC_DRIVER_PCF8563" if rtc else "RTC_DRIVER_NONE"),
         f"-DIS_EPAPER_FRAME={frame}", "-DHAS_EPAPER_PANEL=1", f"-DHAS_DISPLAY={1 - frame}"],
        input='#include "src/app/board_config.h"\n', text=True, capture_output=True,
    )
    if rtc and frame:
        assert result.returncode != 0, "Sleep-first RTC support must be rejected until writes are flushed"
        assert "HAS_RTC is not supported by IS_EPAPER_FRAME" in result.stderr, result.stderr
    else:
        assert result.returncode == 0, result.stderr
print("PASS: RTC supports interactive firmware and rejects the unintegrated sleep-first lifecycle")

for overrides, expected_error in (
    ({}, None),
    ({"HAS_VOICE_NOTES": 1}, None),
    ({"HAS_VOICE_NOTES": 1, "IS_VOICE_ASSISTANT": 1}, "cannot be enabled together"),
    ({"HAS_VOICE_NOTES": 1, "USE_SD_STORAGE": 0}, "requires SD-primary storage"),
    ({"HAS_VOICE_NOTES": 1, "HAS_AUDIO_INPUT": 0}, "requires audio input"),
    ({"HAS_VOICE_NOTES": 1, "HAS_FILESYSTEM": 0}, "requires audio input"),
    ({"HAS_VOICE_NOTES": 1, "HAS_DISPLAY": 0, "HAS_BUTTON": 0}, "requires audio input"),
):
    flags = {
        "IS_VOICE_ASSISTANT": 0, "HAS_AUDIO": 1,
        "HAS_AUDIO_INPUT": 1, "HAS_FILESYSTEM": 1, "HAS_DISPLAY": 1,
        "HAS_BUTTON": 0, "HAS_PSRAM": 1, "HAS_MQTT": 1,
        "HAS_SD_CARD": 1, "USE_SD_STORAGE": 1,
    }
    flags.update(overrides)
    source = '#include "src/app/board_config.h"\n'
    if "HAS_VOICE_NOTES" not in overrides:
        source += 'static_assert(!HAS_VOICE_NOTES, "Voice clips must be disabled by default");\n'
    result = subprocess.run(
        ["c++", "-std=c++11", "-Wall", "-Wextra", "-Werror", "-fsyntax-only",
         "-x", "c++", "-", "-I", str(root)]
        + [f"-D{name}={value}" for name, value in flags.items()],
        input=source, text=True, capture_output=True,
    )
    if expected_error:
        assert result.returncode != 0 and expected_error in result.stderr, result.stderr
    else:
        assert result.returncode == 0, result.stderr
print("PASS: Voice clips feature prerequisites and Voice Assistant exclusion")

for board, expected, sd in (("esp32-p4-lcd4b", 0, 0), ("esp32-p4-lcd4b-sd", 1, 1),
                            ("esp32-p4-lcd4b-voice", 0, 0)):
    subprocess.run(
        ["c++", "-std=c++11", "-Wall", "-Wextra", "-Werror", "-fsyntax-only",
         "-x", "c++", "-", "-I", str(root), "-I", str(root / "src/boards" / board),
         "-DBOARD_HAS_OVERRIDE", "-DBOARD_HAS_PSRAM", "-DCONFIG_IDF_TARGET_ESP32P4=1"],
        input=f'''#include "src/app/board_config.h"
static_assert(HAS_VOICE_NOTES == {expected}, "Voice Notes profile");
static_assert(HAS_SD_CARD == {sd} && USE_SD_STORAGE == {sd}, "LCD4B storage selection");
static_assert(MALLOC_PSRAM_THRESHOLD_BYTES == 512, "LCD4B variants preserve internal DMA headroom");
static_assert(MBEDTLS_PSRAM_THRESHOLD_BYTES == {4096 if sd else 0}, "TLS experiment is SD-only");
''' + ('''static_assert(SDMMC_BUS_WIDTH == 4 && SDMMC_POWER_PIN == 45 &&
              SDMMC_POWER_ACTIVE_LOW && SDMMC_LDO_CHANNEL == 4, "LCD4B SD wiring");
''' if sd else ""),
        text=True, check=True,
    )
print("PASS: LCD4B Voice Notes and Voice Assistant profiles")

for defines, expected_error in (
    (["-DHAS_AUDIO_INPUT=1", "-DHAS_AUDIO=1"], None),
    (["-DHAS_AUDIO_INPUT=1", "-DHAS_AUDIO=0"], "ES8311 audio input requires"),
    (["-DHAS_AUDIO_INPUT=1", "-DHAS_AUDIO=1",
      "-DAUDIO_OUTPUT_DRIVER=AUDIO_OUTPUT_DRIVER_STICKY_PWM"], "ES8311 audio input requires"),
    (["-DHAS_AUDIO_INPUT=1", "-DHAS_AUDIO=0",
      "-DAUDIO_INPUT_DRIVER=AUDIO_INPUT_DRIVER_STICKY_PDM",
      "-DCONFIG_IDF_TARGET_ESP32S3=1"], None),
    (["-DHAS_AUDIO_INPUT=1", "-DAUDIO_INPUT_DRIVER=99"], "Unknown audio input driver"),
    (["-DHAS_AUDIO_INPUT=1", "-DAUDIO_INPUT_DRIVER=AUDIO_INPUT_DRIVER_STICKY_PDM"],
     "Sticky PDM audio input requires ESP32-S3"),
):
    result = subprocess.run(
        ["c++", "-std=c++11", "-Wall", "-Wextra", "-Werror", "-fsyntax-only",
         "-x", "c++", "-", "-I", str(root)] + defines,
        input='#include "src/app/board_config.h"\n', text=True, capture_output=True,
    )
    if expected_error:
        assert result.returncode != 0 and expected_error in result.stderr, result.stderr
    else:
        assert result.returncode == 0, result.stderr
print("PASS: audio input selection and prerequisites")

with tempfile.TemporaryDirectory() as directory:
    for input_enabled in (0, 1):
        obj = pathlib.Path(directory) / "audio_input_drivers.o"
        subprocess.run(
            ["c++", "-std=c++11", "-Wall", "-Wextra", "-Werror",
             f"-DHAS_AUDIO_INPUT={input_enabled}", "-DHAS_AUDIO=1",
             "-c", str(root / "src/app/audio_input_drivers.cpp"), "-o", str(obj)],
            check=True,
        )
        symbols = subprocess.check_output(["nm", "-C", "--defined-only", str(obj)], text=True)
        assert "StickyPdmInput" not in symbols and "audio_input_driver_create" not in symbols, symbols
print("PASS: Sticky input driver excluded from disabled and ES8311 profiles")

with tempfile.TemporaryDirectory() as directory:
    for assistant, clips in ((0, 0), (1, 0), (0, 1)):
        obj = pathlib.Path(directory) / "voice_wav.o"
        subprocess.run(
            ["c++", "-std=c++11", "-Wall", "-Wextra", "-Werror",
             "-include", str(root / "tests/board_config.h"),
             f"-DIS_VOICE_ASSISTANT={assistant}", f"-DHAS_VOICE_NOTES={clips}",
             "-c", str(root / "src/app/voice_wav.cpp"), "-o", str(obj)],
            check=True,
        )
        symbols = subprocess.check_output(["nm", "-C", "--defined-only", str(obj)], text=True)
        assert ("voice_wav_build_header" in symbols) == bool(assistant or clips), symbols
        assert ("voice_wav_read_header" in symbols) == bool(clips), symbols
        if not (assistant or clips):
            assert not symbols.strip(), symbols
print("PASS: WAV code is excluded from unsupported profiles")

subprocess.run(
    ["c++", "-std=c++11", "-Wall", "-Wextra", "-Werror", "-fsyntax-only",
     "-x", "c++", "-", "-I", str(root),
     "-I", str(root / "src/boards/crowpanel-4.2-interactive"),
     "-DBOARD_HAS_OVERRIDE", "-DBOARD_HAS_PSRAM", "-DCONFIG_IDF_TARGET_ESP32S3=1"],
    input='''#include "src/app/board_config.h"
#include "src/app/lv_conf.h"
static_assert(HAS_DISPLAY && HAS_LVGL_EPAPER && HAS_EPAPER_PANEL, "Interactive profile required");
static_assert(!LV_USE_CANVAS, "Non-touch profile without extensions does not need canvas");
static_assert(!HAS_TOUCH && !IS_EPAPER_FRAME && !HAS_EPAPER_GRAYSCALE, "Monochrome non-touch profile required");
static_assert(NUM_HW_BUTTONS == 6 && MAX_HW_BUTTONS >= 6, "All six inputs required");
static_assert(HW_BUTTON_DEFS[0].pin == 2 && HW_BUTTON_DEFS[1].pin == 1, "Menu and Back pins");
static_assert(HW_BUTTON_DEFS[2].pin == 6 && HW_BUTTON_DEFS[3].pin == 4 && HW_BUTTON_DEFS[4].pin == 5, "Jog switch pins");
static_assert(HW_BUTTON_DEFS[5].pin == 0 && BUTTON_PIN == 2, "Boot strap and configuration pins");
static_assert(EPAPER_SPI_SCK_PIN == 12 && EPAPER_SPI_MOSI_PIN == 11, "Panel SPI pins");
static_assert(EPAPER_CS_PIN == 45 && EPAPER_DC_PIN == 46, "Panel control pins");
static_assert(EPAPER_RESET_PIN == 47 && EPAPER_BUSY_PIN == 48 && EPAPER_POWER_PIN == 7, "Panel lifecycle pins");
''', text=True, check=True,
)
print("PASS: crowpanel-4.2-interactive")
subprocess.run(
    ["c++", "-std=c++11", "-Wall", "-Wextra", "-Werror", "-fsyntax-only",
     "-x", "c++", "-", "-I", str(root),
     "-I", str(root / "tests"),
     "-I", str(root / "src/boards/waveshare-7.8-interactive"),
     "-DBOARD_HAS_OVERRIDE", "-DBOARD_HAS_PSRAM", "-DCONFIG_IDF_TARGET_ESP32S3=1"],
    input='''#include "src/app/board_config.h"
#include "src/app/native_extension.h"
#include "src/app/lv_conf.h"
static_assert(HAS_DISPLAY && HAS_LVGL_EPAPER && HAS_EPAPER_PANEL && HAS_EPAPER_GRAYSCALE, "Interactive grayscale profile");
static_assert(!HAS_TOUCH && !IS_EPAPER_FRAME && !HAS_SD_CARD, "Non-touch profile without SD");
static_assert(!HAS_USB_HID && !HAS_BLE_HID, "No unadvertised HID transports");
static_assert(HAS_NATIVE_EXTENSIONS && NATIVE_EXTENSION_SLOT_COUNT == 3, "Three native extension slots required");
static_assert(LV_USE_CANVAS, "Extensions require canvas even without touch");
static_assert(NUM_HW_BUTTONS == 1 && HW_BUTTON_DEFS[0].pin == 14 && BUTTON_PIN == 14, "Single GPIO14 input");
static_assert(DISPLAY_WIDTH == 1872 && DISPLAY_HEIGHT == 1404 && DISPLAY_ROTATION == 2, "Landscape mounting");
static_assert(EPAPER_SPI_SCK_PIN == 36 && EPAPER_SPI_MOSI_PIN == 35 && EPAPER_SPI_MISO_PIN == 37, "Proven SPI wiring");
static_assert(EPAPER_CS_PIN == 10 && EPAPER_RESET_PIN == 38 && EPAPER_BUSY_PIN == 5, "Proven control wiring");
static_assert(EPAPER_DEFAULT_VCOM_MV == 1530 && !IT8951_E1003_PROTOCOL, "Panel-specific standard HAT protocol");
static_assert(IT8951_PANEL_POWER_PIN == -1 && IT8951_CONTROLLER_POWER_PIN == -1, "Externally powered HAT");
#ifdef HW_BUTTON_DEFAULTS_JSON
#error Waveshare HAT button must have no default actions
#endif
''', text=True, check=True,
)
print("PASS: waveshare-7.8-interactive")
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
    defaults = json.loads(subprocess.check_output([str(executable)], text=True))
    assert defaults["action_format_version"] == 1
    assert defaults["buttons"] == [
        {}, {},
        {"tap_actions": [{"op": "button.navigate", "args": {"command": "previous"}}]},
        {"tap_actions": [{"op": "button.navigate", "args": {"command": "next"}}]},
        {"tap_actions": [{"op": "button.navigate", "args": {"command": "tap"}}],
         "hold_actions": [{"op": "button.navigate", "args": {"command": "tap_hold"}}]},
        {"tap_actions": [{"op": "display.refresh_full", "args": {}}]},
    ]
print("PASS: CrowPanel generic hardware action defaults")

fqbn_line = next(line for line in (root / "config.sh").read_text().splitlines()
                 if '["crowpanel-4.2-interactive"]=' in line)
scheme = fqbn_line.split("PartitionScheme=", 1)[1].split(",", 1)[0]
with (root / "partitions" / f"partitions_{scheme}.csv").open() as partition_file:
    partitions = list(csv.reader(line for line in partition_file
                                 if line.strip() and not line.startswith("#")))
partition_end = 0x9000
partition_by_name = {}
for name, kind, subtype, offset, size, _ in partitions:
    offset, size = int(offset.strip(), 0), int(size.strip(), 0)
    assert offset == partition_end and offset % 0x1000 == 0 and size % 0x1000 == 0
    if kind.strip() == "app":
        assert offset % 0x10000 == 0 and size == 0x380000
    partition_by_name[name.strip()] = (subtype.strip(), offset, size)
    partition_end = offset + size
assert partition_end == 8 * 1024 * 1024
assert partition_by_name["app0"] == ("ota_0", 0x10000, 0x380000)
assert partition_by_name["app1"] == ("ota_1", 0x390000, 0x380000)
assert partition_by_name["storage"] == ("spiffs", 0x710000, 896 * 1024)
assert partition_by_name["nvs"] == ("nvs", 0x9000, 0x5000)
assert partition_by_name["otadata"] == ("ota", 0xE000, 0x2000)
assert partition_by_name["coredump"] == ("coredump", 0x7F0000, 0x10000)
print("PASS: CrowPanel 3.5 MiB OTA slots fit 8 MiB flash with 896 KiB storage")

with tempfile.TemporaryDirectory() as directory:
    for header in ("action_registry.h", "action_storage.h"):
        copied_header = pathlib.Path(directory) / header
        copied_header.write_text((root / "src/app" / header).read_text())
    subprocess.run(
        ["c++", "-std=c++11", "-Wall", "-Wextra", "-Werror", "-fsyntax-only",
         "-x", "c++", "-", "-I", str(root), "-I", str(root / "src/app"),
         "-I", str(arduinojson), "-DHAS_DISPLAY=1"],
        input=f'''#include "src/app/action_registry.h"
#include "src/app/action_storage.h"
#include "{directory}/action_registry.h"
#include "{directory}/action_storage.h"
''', text=True, check=True,
    )
print("PASS: action headers tolerate Arduino staging duplicate copies")

for grayscale in (0, 1):
    subprocess.run(
        ["c++", "-std=c++11", "-Wall", "-Wextra", "-Werror", "-fsyntax-only",
         "-x", "c++", "-", "-I", str(root), "-I", str(root / "tests"),
         "-I", str(arduinojson),
         "-DHAS_DISPLAY=1", "-DHAS_EPAPER_PANEL=1", "-DHAS_LVGL_EPAPER=1",
         f"-DHAS_EPAPER_GRAYSCALE={grayscale}"],
        input='''#include "src/app/config_manager.h"
static_assert(config_manager_epaper_mode_supported(EPAPER_RENDER_MODE_BW), "B/W required");
static_assert(config_manager_epaper_mode_supported(EPAPER_RENDER_MODE_GRAYSCALE) == HAS_EPAPER_GRAYSCALE, "Grayscale capability required");
static_assert(!config_manager_epaper_mode_supported(255), "Unknown mode rejected");
''', text=True, check=True,
    )
print("PASS: monochrome and grayscale panel mode capabilities")
for board, target in (("esp32c3-withsensors", "ESP32C3"), ("esp32-p4-lcd4b", "ESP32P4"),
                      ("reterminal-e1003-frame", "ESP32S3")):
    for header in ("config_manager.h", "drivers/monochrome_render.h"):
        source = subprocess.check_output(
            ["c++", "-E", "-P", "-x", "c++", str(root / "src/app" / header),
             "-I", str(root / "tests"), "-I", str(root / "src/boards" / board),
             "-I", str(arduinojson),
             "-DBOARD_HAS_OVERRIDE", "-DBOARD_HAS_PSRAM", f"-DCONFIG_IDF_TARGET_{target}=1"],
            text=True, stderr=subprocess.PIPE,
        )
        for token in ("epaper_bw_conversion", "epaper_bw_threshold", "monochrome_is_white", "blueNoise"):
            assert token not in source, (board, header, token)
print("PASS: B/W conversion and settings excluded from non-interactive profiles")
source = '''#include "src/app/board_config.h"
static_assert(HAS_USB_HID, "USB HID must remain enabled");
static_assert(!ALARM_ENABLED, "Alarms must be disabled");
static_assert(HAS_NATIVE_EXTENSIONS, "Extensions must remain enabled");
static_assert(MALLOC_PSRAM_THRESHOLD_BYTES == 512, "Malloc threshold must be 512 bytes");
'''

for board in ("jc3248w535", "jc3636w518"):
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

for suffix, usb, ble in (("", 0, 0), ("-usb", 1, 0), ("-ble", 0, 1)):
    board = "jc3636w518-sd" + suffix
    subprocess.run(
        ["c++", "-std=c++11", "-Wall", "-Wextra", "-Werror", "-fsyntax-only",
         "-x", "c++", "-", "-I", str(root), "-I", str(root / "src/boards" / board),
         "-DCONFIG_IDF_TARGET_ESP32S3=1", "-DBOARD_HAS_PSRAM", "-DBOARD_HAS_OVERRIDE"],
        input=f'''#include "src/app/board_config.h"
#include "src/app/keyboard_transport.h"
static_assert(HAS_USB_HID == {usb} && HAS_BLE_HID == {ble}, "Selected HID transports");
static_assert(!HAS_BLE && BLE_BACKEND == BLE_BACKEND_ARDUINO_CORE, "Core HID backend; no telemetry");
static_assert(USE_SD_STORAGE && HAS_SD_CARD && HAS_NATIVE_EXTENSIONS, "SD storage and extensions");
static_assert(!ALARM_ENABLED && !HAS_MCP && !HAS_AUDIO && !HAS_IMAGE_FETCH, "Preserve constrained features");
static_assert(MALLOC_PSRAM_THRESHOLD_BYTES == 128, "SD profiles prefer PSRAM above 128 bytes");
static_assert(keyboard_transport_default(HAS_BLE_HID, HAS_USB_HID) == KeyboardTransport::None, "HID defaults off");
''', text=True, check=True,
    )
    metadata = json.loads((root / "src/boards" / board / "metadata.json").read_text())
    assert ("usb_hid" in metadata["capabilities"]) == bool(usb)
    assert ("ble_hid" in metadata["capabilities"]) == bool(ble)
    assert {"extensions", "sd_card"} <= set(metadata["capabilities"])
    print(f"PASS: {board} transport selection and metadata")

for board in sorted((root / "src/boards").iterdir()):
    if not (board / "board_overrides.h").exists():
        continue
    metadata = json.loads((board / "metadata.json").read_text())
    target = metadata["chip_family"].replace("-", "")
    result = subprocess.check_output(
        ["c++", "-E", "-P", "-x", "c++", "-", "-I", str(root / "src/app"),
         "-I", str(board), "-DBOARD_HAS_OVERRIDE", f"-DCONFIG_IDF_TARGET_{target}=1"]
        + (["-DBOARD_HAS_PSRAM"] if metadata["psram_mb"] else []),
        input='#include "board_config.h"\nHID_CAPS HAS_USB_HID HAS_BLE_HID\n', text=True,
    )
    values = next(line.split()[1:] for line in result.splitlines() if line.startswith("HID_CAPS "))
    for capability, value in zip(("usb_hid", "ble_hid"), values):
        assert value in ("true", "false", "0", "1"), (board.name, capability, value)
        assert (capability in metadata.get("capabilities", [])) == (value in ("true", "1")), (board.name, capability)
print("PASS: all board HID capabilities match preprocessed metadata")

for board in ("m5stack-tab5", "m5stack-tab5-sd"):
    subprocess.run(
        ["c++", "-std=c++11", "-Wall", "-Wextra", "-Werror", "-fsyntax-only",
         "-x", "c++", "-", "-I", str(root), "-I", str(root / "src/boards" / board),
         "-DCONFIG_IDF_TARGET_ESP32P4=1", "-DBOARD_HAS_PSRAM", "-DBOARD_HAS_OVERRIDE"],
        input='''#include "src/app/board_config.h"
static_assert(!HAS_USB_HID && HAS_NATIVE_EXTENSIONS, "USB HID disabled; extensions required");
static_assert(HAS_BLE_HID && !HAS_BLE, "BLE HID enabled; BLE telemetry disabled");
''', text=True, check=True,
    )
    metadata = json.loads((root / "src/boards" / board / "metadata.json").read_text())
    assert "usb_hid" not in metadata["capabilities"]
    assert "extensions" in metadata["capabilities"]
    assert "ble_hid" in metadata["capabilities"]
    assert metadata["wireless"]["ble"] is True
    fqbn_line = next(line for line in (root / "config.sh").read_text().splitlines()
                     if f'["{board}"]=' in line)
    assert "USBMode=hwcdc,CDCOnBoot=cdc" in fqbn_line
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
static_assert(!HAS_FACE_WIDGET, "Headless profiles exclude Face");
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
static_assert(MBEDTLS_PSRAM_THRESHOLD_BYTES == 0, "TLS experiment must be opt-in");
'''
policy_cases = []
for target in ("ESP32", "ESP32S3", "ESP32P4", "ESP32C3", "ESP32C6", None):
    for has_psram in (False, True):
        flags = [f"-DCONFIG_IDF_TARGET_{target}=1"] if target else []
        if has_psram:
            flags.append("-DBOARD_HAS_PSRAM")
        policy_cases.append((flags, 512))
policy_cases.extend([
    (["-DCONFIG_IDF_TARGET_ESP32S3=1", "-DBOARD_HAS_PSRAM", "-DHAS_PSRAM=0"], 512),
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

for flags, expected_error in (
    (["-DHAS_PSRAM=1", "-DMBEDTLS_PSRAM_THRESHOLD_BYTES=4096"], None),
    (["-DHAS_PSRAM=0", "-DMBEDTLS_PSRAM_THRESHOLD_BYTES=4096"], "requires HAS_PSRAM"),
    (["-DHAS_PSRAM=1", "-DMBEDTLS_PSRAM_THRESHOLD_BYTES=-1"], "must be nonnegative"),
    (["-DBOARD_HAS_OVERRIDE", "-DBOARD_HAS_PSRAM", "-DCONFIG_IDF_TARGET_ESP32P4=1",
      "-I", str(root / "src/boards/esp32-p4-lcd4b-sd"),
      "-DMBEDTLS_PSRAM_THRESHOLD_BYTES=0"], None),
):
    result = subprocess.run(
        ["c++", "-std=c++11", "-Wall", "-Wextra", "-Werror", "-fsyntax-only",
         "-x", "c++", "-", "-I", str(root), *flags],
        input='''#include "src/app/tls_allocator.cpp"
#if MBEDTLS_PSRAM_THRESHOLD_BYTES == 0
void check_disabled_allocator() { tls_allocator_init(); }
#endif
''' if "-DMBEDTLS_PSRAM_THRESHOLD_BYTES=0" in flags else
        '#include "src/app/board_config.h"\n',
        text=True, capture_output=True,
    )
    if expected_error:
        assert result.returncode != 0 and expected_error in result.stderr, result.stderr
    else:
        assert result.returncode == 0, result.stderr
print("PASS: TLS allocator opt-in, prerequisites, and SDK-free rollback")

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
void reboot_service_boot_init() {}
void tls_allocator_init() {}
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

for board, sd in (("reterminal-sticky-interactive", 0), ("reterminal-sticky-interactive-sd", 1)):
    subprocess.run(
        ["c++", "-std=c++11", "-Wall", "-Wextra", "-Werror", "-fsyntax-only",
         "-x", "c++", "-", "-I", str(root), "-I", str(root / "src/boards" / board),
         "-DBOARD_HAS_OVERRIDE", "-DBOARD_HAS_PSRAM", "-DCONFIG_IDF_TARGET_ESP32S3=1"],
        input=f'''#include "src/app/board_config.h"
static_assert(HAS_SD_CARD == {sd} && USE_SD_STORAGE == {sd}, "Sticky storage selection");
static_assert(HAS_VOICE_NOTES == {sd}, "Sticky Voice Notes selection");
static_assert(HAS_DISPLAY && HAS_TOUCH && HAS_AUDIO_INPUT && HAS_RTC && HAS_BLE, "Sticky capabilities");
static_assert(BLE_BACKEND == BLE_BACKEND_NIMBLE_ARDUINO, "Sticky BLE backend");
''' + ('''static_assert(SD_STORAGE_SPI && SD_SPI_CS_PIN == 8 && SD_SPI_SCK_PIN == 13 &&
    SD_SPI_MISO_PIN == 12 && SD_SPI_MOSI_PIN == 14 && SD_SPI_FREQUENCY_HZ == 1000000, "Sticky SD wiring");
''' if sd else ""), text=True, check=True,
    )
    metadata = json.loads((root / "src/boards" / board / "metadata.json").read_text())
    assert ("sd_card" in metadata["capabilities"]) == bool(sd)
print("PASS: Sticky SD and flash variants")

battery_boards = {
    "firebeetle2-esp32c6-aht10", "inkplate5v2-frame", "inkplate6flick-frame",
    "m5stack-stopwatch", "m5stack-tab5", "m5stack-tab5-sd",
    "reterminal-e1003-frame", "reterminal-sticky-interactive", "reterminal-sticky-interactive-sd",
}
for override in sorted((root / "src/boards").glob("*/board_overrides.h")):
    board = override.parent.name
    subprocess.run(
        ["c++", "-std=c++11", "-Wall", "-Wextra", "-Werror", "-fsyntax-only",
         "-x", "c++", "-", "-I", str(root), "-I", str(root / "src/app"),
         "-I", str(override.parent),
         "-DBOARD_HAS_OVERRIDE", "-DBOARD_HAS_PSRAM"]
        + (["-DCONFIG_IDF_TARGET_ESP32S3=1"] if board.startswith("reterminal-sticky-interactive") else []),
        input=f'''#include "src/app/board_config.h"
static_assert(HAS_BATTERY_INFO == {int(board in battery_boards)}, "Battery provider gate mismatch");
static_assert(!HAS_VOICE_NOTES || USE_SD_STORAGE, "Voice Notes requires SD-primary storage");
''', text=True, check=True,
    )
print("PASS: battery info enabled only on existing provider boards and inherited variants")

configured_boards = []
for line in (root / "config.sh").read_text().splitlines():
    line = line.strip()
    if line.startswith('["') and '"]=' in line:
        configured_boards.append(line.split('"', 2)[1])

s3_cache_rows = {
    "esp32-4848S040": 20, "jc3248w535": 80, "jc3636w518": 16,
    "jc3636w518-sd": 16, "jc3636w518-sd-usb": 16, "jc3636w518-sd-ble": 16,
    "m5stack-stopwatch": 16, "jc4827w543c": 32,
}
for board in configured_boards:
    metadata = json.loads((root / "src/boards" / board / "metadata.json").read_text())
    target = metadata["chip_family"].replace("-", "")
    flags = ["-DBOARD_HAS_OVERRIDE", f"-DCONFIG_IDF_TARGET_{target}=1"]
    if metadata["psram_mb"]:
        flags.append("-DBOARD_HAS_PSRAM")
    expected_buffer = ""
    if board.startswith("esp32-p4-lcd4b") or board.startswith("jc4880p433"):
        expected_buffer = 'static_assert(LVGL_BUFFER_SIZE == DISPLAY_WIDTH * 80 && LVGL_DRAW_BUF_COUNT == 2, "P4 strip profile");'
    elif board.startswith("m5stack-tab5"):
        expected_buffer = 'static_assert(LVGL_BUFFER_SIZE == DISPLAY_WIDTH * 160 && LVGL_DRAW_BUF_COUNT == 2 && LVGL_BUFFER_PSRAM_ONLY, "Tab5 strip profile");'
    elif board.startswith("jc1060p470c"):
        expected_buffer = 'static_assert(DISPLAY_ROTATION == 1 && LVGL_BUFFER_SIZE == DISPLAY_HEIGHT * 80 && LVGL_DRAW_BUF_COUNT == 2, "JC1060 portrait profile");'
    elif board in s3_cache_rows:
        expected_buffer = f'static_assert(LVGL_BUFFER_SIZE == DISPLAY_WIDTH * {s3_cache_rows[board]} && LVGL_DRAW_BUF_COUNT == 1, "S3 strip profile");'
    subprocess.run(
        ["c++", "-std=c++11", "-Wall", "-Wextra", "-Werror", "-fsyntax-only",
         "-x", "c++", "-", "-I", str(root), "-I", str(root / "src/app"),
         "-I", str(root / "src/boards" / board), *flags],
        input='#include "src/app/board_config.h"\n'
              '#include "src/app/lv_conf.h"\n'
              'static_assert(!HAS_DISPLAY_DIAGNOSTICS, "Detailed display diagnostics must default off");\n'
              f'static_assert(LVGL_DSI_FRAME_SWAP == {int(target == "ESP32P4")}, "P4 swap profile");\n'
              f'static_assert(LV_USE_PPA == {int(target == "ESP32P4")}, "P4 drawing backend selection");\n'
              'static_assert(LV_DRAW_SW_DRAW_UNIT_CNT == 1, "One software draw unit");\n'
              f'static_assert(LVGL_CACHE_PATTERNS == {int(target == "ESP32P4" or board in s3_cache_rows)}, "Pattern cache profile");\n'
              + ('static_assert(LVGL_PATTERN_CACHE_BYTES == 2097152 && LVGL_PATTERN_CACHE_RESERVE == 8388608, "P4 cache budget");\n'
                 if target == "ESP32P4" else
                 'static_assert(LVGL_PATTERN_CACHE_BYTES == 262144 && LVGL_PATTERN_CACHE_RESERVE == 2097152, "S3 cache budget");\n'
                 if board in s3_cache_rows else '')
              + expected_buffer + "\n",
        text=True, check=True,
    )
print(f"PASS: diagnostics, drawing, cache and buffer profiles for {len(configured_boards)} active targets")

for board, target in (("esp32-p4-lcd4b-sd", "ESP32P4"), ("m5stack-tab5", "ESP32P4"), ("jc3636w518-sd", "ESP32S3")):
    subprocess.run(
        ["c++", "-std=c++11", "-Wall", "-Wextra", "-Werror", "-fsyntax-only",
         "-x", "c++", "-", "-I", str(root), "-I", str(root / "src/app"),
         "-I", str(root / "src/boards" / board), "-DBOARD_HAS_OVERRIDE", "-DBOARD_HAS_PSRAM",
         f"-DCONFIG_IDF_TARGET_{target}=1", "-DLVGL_USE_PPA=0", "-DLVGL_CACHE_PATTERNS=0",
         "-DLVGL_DSI_FRAME_SWAP=0"],
        input='#include "src/app/board_config.h"\n'
              'static_assert(!LVGL_USE_PPA && !LVGL_CACHE_PATTERNS && !LVGL_DSI_FRAME_SWAP, "Independent optimization rollback");\n',
        text=True, check=True,
    )

resolved = subprocess.check_output(
    ["bash", "-c", '''source "$1"
printf 'revision %s\\n' "$P4_LVGL_REVISION"
for board in "${!FQBN_TARGETS[@]}"; do
    printf '%s %s\\n' "$board" "$(get_chip_type_from_fqbn "${FQBN_TARGETS[$board]}")"
done''', "board-profiles", str(root / "config.sh")], text=True,
)
lines = resolved.splitlines()
assert lines[0] == "revision cf22a42f1dc552160a2e48fde738cc8f31821827"
chips = dict(line.split() for line in lines[1:])
assert set(chips) == set(configured_boards)
for board, chip in chips.items():
    metadata = json.loads((root / "src/boards" / board / "metadata.json").read_text())
    assert (chip == "esp32p4") == (metadata["chip_family"] == "ESP32-P4"), board
print("PASS: independent rollback and resolved P4 dependency selection")