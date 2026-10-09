#!/usr/bin/env python3
"""Attribute retained GNU linker-map sections without rebuilding firmware."""

import argparse
import copy
from dataclasses import asdict, dataclass
from datetime import datetime, timezone
import fnmatch
import json
from pathlib import Path
import re
import shutil

from parse_esp32_partitions import _find_app_partition, _iter_entries


FEATURES = {
    "MCP (HAS_MCP)": (("mcp*.cpp.o", "web_mcp.cpp.o", "web_portal_mcp*.cpp.o", "*_mcp_adapter.cpp.o"), ("mcp_fragment_html_gz",)),
    "Alarms (ALARM_ENABLED)": (
        ("alarm*.cpp.o", "web_portal_alarm*.cpp.o"),
        ("alarm_schedule_fragment_html_gz", "alarm_behavior_fragment_html_gz", "portal_alarms_js_gz"),
    ),
    "MQTT (HAS_MQTT)": (
        ("mqtt*.cpp.o", "ha_discovery*.cpp.o", "web_portal_mqtt*.cpp.o"),
        ("mqtt_fragment_html_gz", "ha_discovery_fragment_html_gz"),
    ),
    "Audio (HAS_AUDIO)": (
        ("audio.cpp.o", "audio_output_drivers.cpp.o", "web_portal_audio*.cpp.o"),
        ("volume_fragment_html_gz",),
    ),
    "Audio input (HAS_AUDIO_INPUT)": (("audio_input*.cpp.o",), ()),
    "Sound player (HAS_SOUND_PLAYER)": (
        ("sound_player*.cpp.o", "sound_store.cpp.o", "mp3_metadata.cpp.o", "web_portal_sound*.cpp.o"),
        ("sounds_fragment_html_gz",),
    ),
    "Music playback / analysis": (
        ("music*.cpp.o",), ("music_fragment_html_gz",),
    ),
    "Camera (HAS_CAMERA)": (
        ("camera*.cpp.o", "web_portal_camera*.cpp.o"),
        ("camera_fragment_html_gz", "camera_snapshots_fragment_html_gz", "portal_camera_js_gz",
         "portal_camera_snapshots_js_gz", "portal_camera_css_gz"),
    ),
    "Remote log (HAS_REMOTE_LOG)": (
        ("remote_log*.cpp.o", "web_portal_log*.cpp.o"),
        ("logs_fragment_html_gz", "portal_logs_js_gz"),
    ),
    "Home Assistant": (("ha_*.cpp.o",), ()),
    "Display / touch (HAS_DISPLAY / HAS_TOUCH)": (
        ("display*.cpp.o", "touch*.cpp.o", "dma2d_arbiter.cpp.o"),
        ("brightness_fragment_html_gz", "rotation_fragment_html_gz", "screen_preview_fragment_html_gz"),
    ),
    "LVGL": (("lvgl*.cpp.o",), ()),
    "Fonts (HAS_CUSTOM_FONTS)": (("custom_fonts.cpp.o",), ()),
    "Image decoding / encoding": (("image_decoder.cpp.o", "jpeg_encoder_service.cpp.o"), ()),
    "Image fetch (HAS_IMAGE_FETCH)": (("image_fetch.cpp.o",), ()),
    "Image library (HAS_IMAGE_LIBRARY)": (
        ("image_library*.cpp.o", "local_image_loader.cpp.o"),
        ("image_library_fragment_html_gz", "portal_image_library_js_gz"),
    ),
    "Native extensions (HAS_NATIVE_EXTENSIONS)": (("native_extension*.cpp.o",), ()),
    "Bindings / expressions": (
        ("*binding*.cpp.o", "expr_eval.cpp.o", "data_stream.cpp.o", "list_provider*.cpp.o"), (),
    ),
    "Actions": (
        ("action*.cpp.o", "boot_actions.cpp.o", "swipe_actions.cpp.o", "key_sequence*.cpp.o"),
        ("boot_actions_fragment_html_gz", "swipe_actions_fragment_html_gz"),
    ),
    "Widgets (HAS_DISPLAY)": (("widgets.cpp.o",), ()),
    "Pads / screens (HAS_DISPLAY)": (
        ("pad*.cpp.o", "screens.cpp.o", "screen_saver*.cpp.o", "button*.cpp.o", "label_style.cpp.o",
         "visual_alert.cpp.o", "message_bubble.cpp.o", "swipe_config.cpp.o", "web_portal_pad.cpp.o"),
        ("pad_editor_fragment_html_gz", "button_defaults_fragment_html_gz", "screensaver_fragment_html_gz"),
    ),
    "BLE HID (HAS_BLE_HID)": (
        ("ble_hid.cpp.o", "web_portal_ble.cpp.o"), (),
    ),
    "USB HID (HAS_USB_HID)": (
        ("usb_hid.cpp.o", "mouse_hid.cpp.o", "gamepad_hid.cpp.o"), (),
    ),
    "Shared HID": (
        ("*hid.cpp.o",), ("hid_fragment_html_gz",),
    ),
    "Bluetooth stack / telemetry (HAS_BLE_HID / HAS_BLE)": (("ble_*.cpp.o",), ()),
    "Wi-Fi / networking (shared)": (
        ("wifi*.cpp.o", "net_activity.cpp.o", "web_portal_ap.cpp.o"), ("wifi_fragment_html_gz",),
    ),
    "TLS / cryptography (shared)": ((), ()),
    "Storage": (
        ("storage*.cpp.o", "sd_*.cpp.o", "fs_*.cpp.o", "icon_store.cpp.o", "web_portal_fs_store.cpp.o"),
        ("storage_fragment_html_gz",),
    ),
    "OTA / firmware updates": (
        ("ota*.cpp.o", "web_portal_ota*.cpp.o", "web_portal_firmware.cpp.o"),
        ("firmware_fragment_html_gz",),
    ),
    "Timers / time": (("timer*.cpp.o", "time_service.cpp.o"), ("timers_fragment_html_gz",)),
    "Sensors / power": (("sensors.cpp.o", "power*.cpp.o", "hw_button*.cpp.o", "duty_cycle.cpp.o"), ()),
    "Configuration": (("config*.cpp.o",), ()),
    "Device classes": (("device_class*.cpp.o", "class_branding.cpp.o"), ()),
    "Built-in image assets": (("png_assets.cpp.o",), ()),
    "Web portal (shared)": (("web_portal*.cpp.o", "portal*.cpp.o", "route_components.cpp.o"), ()),
    "Web portal assets (shared)": ((), ()),
    "Arduino / ESP-IDF core": ((), ()),
    "C / C++ runtime": ((), ()),
    "Linker-merged strings / constants (shared)": ((), ()),
}
LIBRARIES = {
    "LVGL": ("lvgl", "liblvgl.a"),
    "Display / touch (HAS_DISPLAY / HAS_TOUCH)": (
        "TFT_eSPI", "LovyanGFX", "Arduino_GFX*", "libesp_lcd.a", "libesp_driver_ppa.a",
    ),
    "Image decoding / encoding": ("PNGdec", "JPEGDEC", "TJpg_Decoder", "libesp_new_jpeg.a", "libesp_driver_jpeg.a"),
    "Wi-Fi / networking (shared)": (
        "WiFi", "Network", "HTTPClient", "DNSServer", "ESPmDNS", "AsyncUDP", "Async_TCP",
        "liblwip.a", "libesp_wifi.a", "libesp_netif.a", "libesp_eth.a", "libesp_http_client.a",
        "libtcp_transport.a", "libespressif__mdns.a", "libespressif__esp_hosted.a",
        "libespressif__esp_wifi_remote.a", "libespressif__esp_serial_slave_link.a",
    ),
    "TLS / cryptography (shared)": (
        "NetworkClientSecure", "Hash", "libmbed*.a", "libesp-tls.a", "libespressif__libsodium.a",
    ),
    "MQTT (HAS_MQTT)": ("PubSubClient", "libmqtt.a"),
    "Bluetooth stack / telemetry (HAS_BLE_HID / HAS_BLE)": ("BLE", "libbt.a"),
    "USB HID (HAS_USB_HID)": ("USB", "libarduino_tinyusb.a", "libusb.a"),
    "Shared HID": ("libesp_hid.a",),
    "Storage": (
        "FS", "LittleFS", "SD", "SD_MMC", "Preferences", "libfatfs.a", "libsdmmc.a", "libspiffs.a",
        "libjoltwallet__littlefs.a", "libnvs*.a", "libwear_levelling.a", "libesp_driver_sdmmc.a",
        "libesp_driver_sdspi.a", "libesp_partition.a", "libspi_flash.a",
    ),
    "OTA / firmware updates": ("Update", "libapp_update.a", "libesp_https_ota.a"),
    "Sound player (HAS_SOUND_PLAYER)": ("libchmorgan__esp-libhelix-mp3.a",),
    "Audio (HAS_AUDIO)": ("libesp_driver_i2s.a",),
    "Camera (HAS_CAMERA)": (
        "libesp_driver_cam.a", "libesp_driver_isp.a", "libespressif__esp_cam_sensor.a",
        "libespressif__esp_video.a", "libespressif__esp_sccb_intf.a",
    ),
    "Web portal (shared)": ("ESP_Async_WebServer", "libesp_http_server.a", "libhttp_parser.a"),
    "C / C++ runtime": ("libc.a", "libm.a", "libgcc.a", "libstdc++.a", "libnosys.a", "libcxx.a", "libnewlib.a"),
    "Arduino / ESP-IDF core": (
        "core.a", "SPI", "Wire", "libfreertos.a", "libpthread.a", "libheap.a", "libhal.a", "libsoc.a",
        "libriscv.a", "libdriver.a", "libesp_driver_*.a", "libesp_hw_support.a", "libesp_system.a",
        "libesp_common.a", "libesp_timer.a", "libesp_rom.a", "libesp_event.a", "libesp_mm.a",
        "libesp_psram.a", "libesp_pm.a", "libesp_ringbuf.a", "liblog.a", "libefuse.a", "libvfs.a",
        "libbootloader_support.a", "libesp_app_format.a", "libesp_bootloader_format.a",
    ),
}
FLAG_SUBSYSTEMS = {
    "HAS_BACKLIGHT": "Display / touch (HAS_DISPLAY / HAS_TOUCH)",
    "HAS_BUILTIN_LED": "Sensors / power",
    "HAS_BUTTON": "Sensors / power",
    "HAS_CONFIG_MODE_BUTTON": "Sensors / power",
    "HAS_EPAPER_FRAME_WAKE_BUTTON": "Sensors / power",
    "HAS_EPAPER_FRONTLIGHT": "Display / touch (HAS_DISPLAY / HAS_TOUCH)",
    "HAS_EPAPER_PANEL": "Display / touch (HAS_DISPLAY / HAS_TOUCH)",
    "HAS_EPAPER_VCOM": "Display / touch (HAS_DISPLAY / HAS_TOUCH)",
    "HAS_ES7210_MIC": "Audio input (HAS_AUDIO_INPUT)",
    "HAS_HA_HISTORY": "Home Assistant",
    "HAS_LVGL_EPAPER": "LVGL",
    "HAS_MUSIC_ANALYSIS": "Music playback / analysis",
    "HAS_SD_CARD": "Storage",
    "HAS_SENSOR_AHT10": "Sensors / power",
    "HAS_SENSOR_BATTERY_ADC": "Sensors / power",
    "HAS_SENSOR_BME280": "Sensors / power",
    "HAS_SENSOR_DUMMY": "Sensors / power",
    "HAS_SENSOR_HX711": "Sensors / power",
    "HAS_SENSOR_LD2410_OUT": "Sensors / power",
    "HAS_SENSOR_NAU7802": "Sensors / power",
    "HAS_SENSOR_TSL2591": "Sensors / power",
    "HAS_STORAGE_BROWSER": "Storage",
    "IS_COFFEE_SCALE": "Device classes",
    "IS_DARKROOM_TIMER": "Device classes",
    "IS_EPAPER_FRAME": "Device classes",
    "IS_SHUTTER_TESTER": "Device classes",
    "IS_VOICE_ASSISTANT": "Device classes",
}
EXCLUDED_FLAGS = {
    "HAS_PSRAM": "Memory capacity and allocation affect many subsystems; there is no distinct flash owner.",
}
FEATURE_FLAG_PATTERN = re.compile(r"\b(?:HAS_[A-Z0-9_]+|IS_[A-Z0-9_]+|ALARM_ENABLED)\b")
SHARED = "Shared/unattributed"
SECTION = re.compile(r"^\s+(\.[\w.$-]+)(?:\s+(0x[0-9a-fA-F]+)\s+(0x[0-9a-fA-F]+)\s+(.+))?\s*$")
CONTINUATION = re.compile(r"^\s+(0x[0-9a-fA-F]+)\s+(0x[0-9a-fA-F]+)\s+(.+)$")
OUTPUT = re.compile(r"^(\.[\w.$-]+)(?:\s|$)")
OBJECT = re.compile(r"(?:\(([^()]+\.(?:o|obj))\)|/([^/()]+\.(?:o|obj)))$")
MERGEABLE = re.compile(r"\.(?:str\d+\.\d+|cst\d+)$")


@dataclass
class Contribution:
    feature: str
    section: str
    object: str
    code: int = 0
    data: int = 0
    assets: int = 0
    static_ram: int = 0


def attribute(section, owner):
    if section.endswith(("_html_gz", "_js_gz", "_css_gz")):
        for feature, (_, assets) in FEATURES.items():
            if any(section.endswith(asset) for asset in assets):
                return feature, True
        return "Web portal assets (shared)", True
    if "/sketch/" in owner:
        match = OBJECT.search(owner)
        if match:
            module = match.group(1) or match.group(2)
            for feature, (modules, _) in FEATURES.items():
                if any(fnmatch.fnmatchcase(module, pattern) for pattern in modules):
                    return feature, False
        return SHARED, False
    library = re.search(r"/libraries/([^/]+)/", owner)
    archive = re.search(r"/([^/()]+\.a)\(", owner)
    name = library.group(1) if library else archive.group(1) if archive else ""
    for feature, patterns in LIBRARIES.items():
        if any(fnmatch.fnmatchcase(name, pattern) for pattern in patterns):
            return feature, False
    return SHARED, False


def parse_map(text):
    marker = "Linker script and memory map"
    if marker not in text:
        raise ValueError("Not a supported GNU linker map: missing retained-section marker")
    contributions = []
    pending = None
    output_section = ""
    lines = text.split(marker, 1)[1].splitlines()
    for line_index, line in enumerate(lines):
        if line.startswith("Cross Reference Table"):
            break
        output = OUTPUT.match(line)
        if output:
            output_section = output.group(1)
            pending = None
            continue
        match = SECTION.match(line)
        if match:
            section, address, size, owner = match.groups()
            pending = section if address is None else None
            if address is None:
                continue
        elif pending:
            match = CONTINUATION.match(line)
            if not match:
                pending = None
                continue
            section = pending
            address, size, owner = match.groups()
            pending = None
        else:
            continue
        if not OBJECT.search(owner):
            continue
        if output_section.startswith((".debug", ".comment", ".eh_frame")) or output_section.endswith("_noload"):
            continue
        size = int(size, 16)
        if not size or not int(address, 16):
            continue
        feature, is_asset = attribute(section, owner)
        if MERGEABLE.search(section):
            following = lines[line_index + 1] if line_index + 1 < len(lines) else ""
            if "(size before relaxing)" not in following:
                continue
            feature = "Linker-merged strings / constants (shared)"
        contribution = Contribution(feature, section, owner)
        if section.startswith((".text", ".literal", ".iram", ".flash.text", ".rtc.text", ".rtc.literal")):
            contribution.code = size
        elif section.startswith((".bss", ".sbss", ".noinit", ".rtc_noinit")) or output_section.endswith((".bss", ".sbss", ".noinit", "_noinit")):
            contribution.static_ram = size
        elif section.startswith((".rodata", ".srodata", ".data", ".sdata", ".dram", ".rtc")):
            if is_asset:
                contribution.assets = size
            else:
                contribution.data = size
            if output_section.endswith((".data", ".sdata", ".data1")):
                contribution.static_ram = size
        else:
            continue
        contributions.append(contribution)
    if not contributions:
        raise ValueError("No retained code/data sections found in linker map")
    return contributions


def summarize(contributions):
    totals = {feature: dict(code=0, data=0, assets=0, static_ram=0) for feature in (*FEATURES, SHARED)}
    for contribution in contributions:
        for category in totals[contribution.feature]:
            totals[contribution.feature][category] += getattr(contribution, category)
    return totals


def build_context(path):
    context = {
        "board_directory": path.parent.name,
        "map_timestamp_utc": datetime.fromtimestamp(path.stat().st_mtime, timezone.utc).isoformat(),
        "application_binary_bytes": None,
        "app_partitions": [],
        "partitions": [],
        "flash_size_bytes": None,
        "flash_size_source": None,
        "partition_extent_bytes": None,
        "warnings": [],
    }
    metadata = Path(__file__).resolve().parent.parent / "src" / "boards" / path.parent.name / "metadata.json"
    if metadata.is_file():
        try:
            flash_mb = json.loads(metadata.read_text()).get("flash_mb")
            if isinstance(flash_mb, (int, float)) and not isinstance(flash_mb, bool) and flash_mb > 0:
                context["flash_size_bytes"] = int(flash_mb * 1024 * 1024)
                context["flash_size_source"] = "board-metadata"
            else:
                context["warnings"].append("Board metadata does not declare a valid flash capacity.")
        except (OSError, ValueError, AttributeError, OverflowError) as error:
            context["warnings"].append(f"Flash capacity unavailable: {error}")
    binary = path.with_suffix(".bin")
    partitions = path.with_suffix(".partitions.bin")
    if binary.is_file():
        context["application_binary_bytes"] = binary.stat().st_size
        if abs(binary.stat().st_mtime - path.stat().st_mtime) > 60:
            context["warnings"].append("Binary and map timestamps differ by over 60 seconds; artifacts may be mismatched.")
    else:
        context["warnings"].append(f"Application binary unavailable: {binary.name}")
    if partitions.is_file():
        try:
            entries = list(_iter_entries(partitions.read_bytes()))
            context["partitions"] = [dict(label=entry.label, type=entry.type, subtype=entry.subtype,
                                          offset=entry.offset, size_bytes=entry.size)
                                     for entry in sorted(entries, key=lambda entry: entry.offset)]
            context["partition_extent_bytes"] = max((entry.offset + entry.size for entry in entries), default=0)
            if context["flash_size_bytes"] is not None and context["partition_extent_bytes"] > context["flash_size_bytes"]:
                context["warnings"].append("Partition table extends beyond the board's declared flash capacity.")
            selected = _find_app_partition(entries)
            for entry in entries:
                if entry.type != 0:
                    continue
                binary_size = context["application_binary_bytes"]
                context["app_partitions"].append({
                    "label": entry.label, "size_bytes": entry.size, "offset": entry.offset,
                    "selected": entry == selected,
                    "headroom_bytes": None if binary_size is None else entry.size - binary_size,
                    "usage_percent": None if binary_size is None or not entry.size else 100 * binary_size / entry.size,
                })
        except (OSError, ValueError) as error:
            context["warnings"].append(f"Partition context unavailable: {error}")
    else:
        context["warnings"].append(f"Partition table unavailable: {partitions.name}")
    return context


def flash_bytes(values):
    return values["code"] + values["data"] + values["assets"]


def subsystem_flags(name):
    return list(dict.fromkeys(FEATURE_FLAG_PATTERN.findall(name)
                             + [flag for flag, subsystem in FLAG_SUBSYSTEMS.items() if subsystem == name]))


def compact_snapshot(totals, context, demo=False):
    rows = []
    for name, values in totals.items():
        if not any(values.values()):
            continue
        flags = subsystem_flags(name)
        row = {"id": re.sub(r"[^a-z0-9]+", "-", name.lower()).strip("-"),
               "name": name.split(" (")[0], "flags": flags, **values,
               "flash": flash_bytes(values)}
        rows.append(row)
    rows.sort(key=lambda row: row["flash"], reverse=True)
    return {"schema_version": 1, "source": "demo" if demo else "local-build",
            "board": context["board_directory"], "context": context,
            "attributed_flash_bytes": sum(row["flash"] for row in rows), "subsystems": rows}


def demo_snapshots():
    totals = {
        "LVGL": dict(code=180000, data=250000, assets=0, static_ram=8000),
        "Fonts (HAS_CUSTOM_FONTS)": dict(code=128, data=230000, assets=0, static_ram=0),
        "MCP (HAS_MCP)": dict(code=47000, data=1500, assets=2000, static_ram=500),
        "Web portal assets (shared)": dict(code=0, data=0, assets=150000, static_ram=0),
        "Bindings / expressions with an exceptionally long subsystem name":
            dict(code=22000, data=900, assets=0, static_ram=2400),
        "Arduino / ESP-IDF core": dict(code=600000, data=900000, assets=0, static_ram=45000),
    }
    snapshots = []
    capacity = 3 * 1024 * 1024
    for scenario, binary in (("Near capacity", capacity - 32 * 1024),
                             ("Over capacity", capacity + 192 * 1024), ("Missing artifacts", None)):
        context = dict(board_directory=f"DEMO - {scenario}", map_timestamp_utc=None,
                       application_binary_bytes=binary, app_partitions=[], partitions=[],
                       flash_size_bytes=None, flash_size_source=None, partition_extent_bytes=None, warnings=[])
        if binary is None:
            context["warnings"] = ["Demo: application binary and partition table are unavailable."]
        else:
            context["app_partitions"] = [dict(label="app0", size_bytes=capacity, offset=65536, selected=True,
                                              headroom_bytes=capacity - binary, usage_percent=100 * binary / capacity)]
            context["flash_size_bytes"] = 8 * 1024 * 1024
            context["flash_size_source"] = "demo"
            context["partition_extent_bytes"] = context["flash_size_bytes"]
            context["partitions"] = [
                dict(label="app0", type=0, subtype=0x10, offset=65536, size_bytes=capacity),
                dict(label="app1", type=0, subtype=0x11, offset=65536 + capacity, size_bytes=capacity),
                dict(label="storage", type=1, subtype=0x82, offset=65536 + 2 * capacity,
                     size_bytes=context["flash_size_bytes"] - 65536 - 2 * capacity),
            ]
        snapshots.append(compact_snapshot(copy.deepcopy(totals), context, demo=True))
    return snapshots


def write_site(dataset, output):
    assets = Path(__file__).resolve().parent / "firmware-size-view"
    output.mkdir(parents=True, exist_ok=True)
    for name in ("index.html", "report.css", "report.js"):
        shutil.copyfile(assets / name, output / name)
    (output / "reports.json").write_text(json.dumps(dataset, indent=2) + "\n")
    return dataset


def aggregate_snapshots(directory, expected_matrix, provenance):
    if not directory.is_dir():
        raise ValueError(f"Snapshot directory does not exist: {directory}")
    expected = {board["name"] for board in expected_matrix["board"]}
    reports = {}
    for path in sorted(directory.rglob("*.json")):
        snapshot = json.loads(path.read_text())
        if not isinstance(snapshot, dict):
            raise ValueError(f"Invalid board snapshot: {path}")
        board = snapshot.get("board")
        if snapshot.get("schema_version") != 1 or snapshot.get("source") != "local-build" or board not in expected:
            raise ValueError(f"Unsupported or unexpected snapshot: {path}")
        if board in reports:
            raise ValueError(f"Duplicate board snapshot: {board}")
        if snapshot.get("provenance") != provenance:
            raise ValueError(f"Mismatched build provenance: {board}")
        if not isinstance(snapshot.get("context"), dict) or not isinstance(snapshot.get("subsystems"), list):
            raise ValueError(f"Invalid board snapshot: {board}")
        reports[board] = snapshot
    return {
        "schema_version": 1,
        "generated_utc": datetime.now(timezone.utc).isoformat(),
        "provenance": provenance,
        "reports": [reports[board] for board in sorted(reports)],
        "missing_boards": sorted(expected - reports.keys()),
    }


def markdown_summary(dataset):
    def escaped(value):
        return str(value).replace("&", "&amp;").replace("<", "&lt;").replace("|", "&#124;").replace("\n", " ")

    def size(value):
        return "Unavailable" if value is None else f"{value / 1024:,.2f} KiB"

    lines = ["## Firmware Footprint", "", "Attribution is not guaranteed feature-removal savings.", ""]
    for snapshot in dataset["reports"]:
        context = snapshot["context"]
        lines.extend([f"### {escaped(snapshot['board'])}", "",
                      f"Application binary: **{size(context['application_binary_bytes'])}**", ""])
        for partition in context["app_partitions"]:
            lines.append(f"- {escaped(partition['label'])}: {size(partition['size_bytes'])}; "
                         f"projected headroom {size(partition['headroom_bytes'])}")
        lines.extend(["", "| Subsystem | Owning flags | Flash | Static RAM |", "| --- | --- | ---: | ---: |"])
        for row in sorted(snapshot["subsystems"], key=lambda row: row["flash"], reverse=True):
            lines.append(f"| {escaped(row['name'])} | {escaped(', '.join(row['flags']))} | "
                         f"{size(row['flash'])} | {size(row['static_ram'])} |")
        lines.extend(["", *[f"- Warning: {escaped(warning)}" for warning in context["warnings"]], ""])
    if dataset.get("missing_boards"):
        lines.extend(["**Missing reports (failed, skipped, or collection unavailable):** "
                      + ", ".join(escaped(board) for board in dataset["missing_boards"]), ""])
    lines.extend(["Download the `firmware-footprint` artifact for this run's interactive report.", ""])
    return "\n".join(lines)


def export_site(paths, output, include_demos=False):
    reports = [compact_snapshot(summarize(parse_map(path.read_text())), build_context(path)) for path in paths]
    if include_demos:
        reports.extend(demo_snapshots())
    if not reports:
        raise ValueError("Supply at least one linker map or --demos")
    dataset = {"schema_version": 1, "generated_utc": datetime.now(timezone.utc).isoformat(), "reports": reports}
    return write_site(dataset, output)


def print_report(totals, path, context):
    total_flash = sum(flash_bytes(values) for values in totals.values())
    print(f"Firmware subsystem attribution: {path}")
    print(f"Board directory: {context['board_directory']} (inferred from path)")
    print(f"Map timestamp (UTC, not verified build time): {context['map_timestamp_utc']}")
    binary_size = context["application_binary_bytes"]
    if binary_size is not None:
        print(f"Application binary: {binary_size:,} bytes ({binary_size / 1024:.2f} KiB)")
    for partition in context["app_partitions"]:
        selection = " [preferred app partition]" if partition["selected"] else ""
        usage = ""
        if partition["headroom_bytes"] is not None:
            usage = f", {partition['usage_percent']:.1f}% used, {partition['headroom_bytes'] / 1024:.2f} KiB headroom"
        print(f"App partition {partition['label']}{selection}: {partition['size_bytes'] / 1024:.2f} KiB{usage}")
    for warning in context["warnings"]:
        print(f"Warning: {warning}")
    print(f"Attributed flash payload: {total_flash / 1024:.2f} KiB")
    if binary_size is not None:
        print(f"Binary minus attributed payload: {(binary_size - total_flash) / 1024:.2f} KiB (not reconciled)")
    print("All sizes in KiB (1024 bytes); attributed bytes, NOT feature removal savings.\n")
    width = max(len(name) for name in totals)
    print(f"{'Subsystem / owning flag':<{width}} {'Code':>10} {'Other data':>10} {'Web assets':>10} {'Flash*':>10} {'Static RAM':>10} {'Flash %':>8}")
    rows = sorted(totals.items(), key=lambda item: flash_bytes(item[1]), reverse=True)
    for feature, values in rows:
        if not any(values.values()):
            continue
        flash = flash_bytes(values)
        columns = [values["code"], values["data"], values["assets"], flash, values["static_ram"]]
        percent = 100 * flash / total_flash if total_flash else 0
        print(f"{feature:<{width}}" + "".join(f" {value / 1024:>10.2f}" for value in columns) + f" {percent:>7.2f}%")
    total_values = {key: sum(values[key] for values in totals.values()) for key in ("code", "data", "assets", "static_ram")}
    columns = [total_values["code"], total_values["data"], total_values["assets"], total_flash, total_values["static_ram"]]
    print(f"{'TOTAL':<{width}}" + "".join(f" {value / 1024:>10.2f}" for value in columns))
    print("\n* Flash payload excludes alignment, image headers, and unrecognized sections.")
    print("Mergeable strings/constants count only linker-relaxed backing pools; ambiguous source entries are excluded.")
    print("Flash % uses attributed payload as denominator, not the application binary.")
    print("Static RAM is variables only; excludes executable RAM, heap, and task stacks.")
    print("Shared dependencies and web bundles have their own rows, not removal savings.")
    print("Flags label ownership, not detected enabled/disabled state; rows with no attributed bytes are hidden.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("maps", nargs="*", type=Path, help="Existing app.ino.map files (one unless --site is used)")
    parser.add_argument("--json", action="store_true", help="Print exact bytes and contributing sections as JSON")
    parser.add_argument("--site", type=Path, help="Export a standalone visual report directory for the supplied maps")
    parser.add_argument("--demos", action="store_true", help="Include labeled demo scenarios in --site output")
    parser.add_argument("--snapshot", type=Path, help="Write a compact per-board JSON snapshot")
    parser.add_argument("--snapshots", type=Path, help="Aggregate a directory of per-board snapshots into --site")
    parser.add_argument("--expected-matrix", help="Expected board matrix as JSON (required with --snapshots)")
    parser.add_argument("--dataset", type=Path, help="Render combined JSON into --site with an installer backlink")
    parser.add_argument("--summary", type=Path, help="Append Markdown attribution to a GitHub job summary")
    parser.add_argument("--archive", type=Path, help="Write a ZIP of the exported --site (include .zip suffix)")
    parser.add_argument("--commit", default="", help="Source commit SHA")
    parser.add_argument("--ref", default="", help="Source ref or release tag")
    parser.add_argument("--run-url", default="", help="GitHub Actions run URL")
    parser.add_argument("--fqbn", default="", help="Board FQBN for the snapshot")
    args = parser.parse_args()
    provenance = {"commit": args.commit, "ref": args.ref, "run_url": args.run_url}
    if args.snapshots or args.dataset:
        if not args.site or args.maps or args.demos or args.json or args.snapshot or (args.snapshots and args.dataset):
            parser.error("Use --snapshots or --dataset with --site, without maps, demos, or other output modes")
        try:
            if args.snapshots:
                if not args.expected_matrix:
                    parser.error("--snapshots requires --expected-matrix")
                dataset = aggregate_snapshots(args.snapshots, json.loads(args.expected_matrix), provenance)
            else:
                dataset = json.loads(args.dataset.read_text())
                if dataset.get("schema_version") != 1 or not isinstance(dataset.get("reports"), list):
                    raise ValueError("Unsupported report dataset")
                dataset["installer_url"] = "../index.html"
            write_site(dataset, args.site)
            if args.archive:
                shutil.make_archive(str(args.archive.with_suffix("")), "zip", args.site)
            if args.summary:
                with args.summary.open("a") as summary:
                    summary.write(markdown_summary(dataset))
        except (OSError, ValueError, KeyError, TypeError) as error:
            parser.exit(1, f"error: {error}\n")
        return
    if args.archive or args.expected_matrix or (args.summary and not args.snapshot):
        parser.error("--archive/--expected-matrix require aggregation; --summary requires --snapshot or aggregation")
    if args.site:
        if args.json or args.snapshot:
            parser.error("--json, --snapshot and --site are mutually exclusive")
        try:
            dataset = export_site(args.maps, args.site, args.demos)
        except (OSError, ValueError) as error:
            parser.exit(1, f"error: {error}\n")
        print(f"Exported {len(dataset['reports'])} reports to {args.site}")
        return
    if len(args.maps) != 1 or args.demos:
        parser.error("Supply exactly one map, or use --site with maps and/or --demos")
    path = args.maps[0]
    try:
        contributions = parse_map(path.read_text())
    except (OSError, ValueError) as error:
        parser.exit(1, f"error: {error}\n")
    totals = summarize(contributions)
    context = build_context(path)
    if args.snapshot:
        if args.json:
            parser.error("--json and --snapshot are mutually exclusive")
        snapshot = compact_snapshot(totals, context)
        snapshot["provenance"] = provenance
        snapshot["fqbn"] = args.fqbn
        args.snapshot.parent.mkdir(parents=True, exist_ok=True)
        args.snapshot.write_text(json.dumps(snapshot, indent=2) + "\n")
        if args.summary:
            with args.summary.open("a") as summary:
                summary.write(markdown_summary({"reports": [snapshot]}))
    elif args.json:
        print(json.dumps({"map": str(path), "kind": "attribution", "unit": "bytes",
                          "context": context, "attributed_flash_bytes": sum(flash_bytes(values) for values in totals.values()),
                          "features": totals, "contributions": [asdict(item) for item in contributions]}, indent=2))
    else:
        print_report(totals, path, context)


if __name__ == "__main__":
    main()