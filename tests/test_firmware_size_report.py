#!/usr/bin/env python3
"""Focused tests for retained linker-map attribution."""

import importlib.util
from contextlib import redirect_stdout
import io
import json
import os
from pathlib import Path
import struct
import sys
import tempfile
import unittest
import zipfile
from unittest.mock import patch


sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from compile_flags_report import parse_all_defines, parse_board_config_defaults

SPEC = importlib.util.spec_from_file_location(
    "firmware_size_report", Path(__file__).resolve().parents[1] / "tools/firmware_size_report.py"
)
report = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = report
SPEC.loader.exec_module(report)


class CiReportTest(unittest.TestCase):
    def test_aggregation_requires_matching_unique_expected_boards(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            provenance = {"commit": "abc", "ref": "v1.0.0", "run_url": "https://github.com/example/repo/actions/runs/1"}
            matrix = {"board": [{"name": "board-a"}, {"name": "board-b"}]}
            snapshot = report.demo_snapshots()[0]
            snapshot.update(board="board-a", source="local-build", provenance=provenance)
            path = root / "snapshot.json"
            path.write_text(json.dumps(snapshot))
            dataset = report.aggregate_snapshots(root, matrix, provenance)
            self.assertEqual([row["board"] for row in dataset["reports"]], ["board-a"])
            self.assertEqual(dataset["missing_boards"], ["board-b"])
            self.assertIn("board-b", report.markdown_summary(dataset))
            duplicate = root / "duplicate.json"
            duplicate.write_text(json.dumps(snapshot))
            with self.assertRaisesRegex(ValueError, "Duplicate"):
                report.aggregate_snapshots(root, matrix, provenance)
            duplicate.unlink()
            with self.assertRaisesRegex(ValueError, "provenance"):
                report.aggregate_snapshots(root, matrix, {})
            with self.assertRaisesRegex(ValueError, "unexpected"):
                report.aggregate_snapshots(root, {"board": []}, provenance)
            snapshot["schema_version"] = 99
            path.write_text(json.dumps(snapshot))
            with self.assertRaisesRegex(ValueError, "Unsupported"):
                report.aggregate_snapshots(root, matrix, provenance)

    def test_no_snapshots_is_an_explicit_missing_run(self):
        with tempfile.TemporaryDirectory() as directory:
            dataset = report.aggregate_snapshots(Path(directory), {"board": [{"name": "board-a"}]}, {})
            self.assertEqual(dataset["reports"], [])
            self.assertEqual(dataset["missing_boards"], ["board-a"])
            report.write_site(dataset, Path(directory) / "site")
            self.assertTrue((Path(directory) / "site/index.html").is_file())

    def test_aggregate_cli_archive_and_installer_render(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            inputs = root / "inputs"
            inputs.mkdir()
            provenance = {"commit": "abc", "ref": "v1.0.0", "run_url": ""}
            for board in ("board-b", "board-a"):
                snapshot = report.demo_snapshots()[0]
                snapshot.update(board=board, source="local-build", provenance=provenance)
                (inputs / f"{board}.json").write_text(json.dumps(snapshot))
            site = root / "site"
            archive = root / "firmware-footprint.zip"
            matrix = {"board": [{"name": board} for board in ("board-b", "board-a", "board-c")]}
            args = ["report", "--snapshots", str(inputs), "--expected-matrix", json.dumps(matrix),
                    "--site", str(site), "--archive", str(archive), "--commit", "abc", "--ref", "v1.0.0"]
            with patch.object(sys, "argv", args):
                report.main()
            with zipfile.ZipFile(archive) as bundle:
                self.assertEqual(set(bundle.namelist()), {"index.html", "report.css", "report.js", "reports.json"})
                dataset = json.loads(bundle.read("reports.json"))
            self.assertEqual([row["board"] for row in dataset["reports"]], ["board-a", "board-b"])
            self.assertEqual(dataset["missing_boards"], ["board-c"])
            self.assertNotIn("installer_url", dataset)
            with patch.object(sys, "argv", ["report", "--dataset", str(site / "reports.json"),
                                             "--site", str(root / "installer/firmware-footprint")]):
                report.main()
            installed = json.loads((root / "installer/firmware-footprint/reports.json").read_text())
            self.assertEqual(installed["installer_url"], "../index.html")
            (inputs / "bad.json").write_text("[]")
            with self.assertRaisesRegex(ValueError, "Invalid"):
                report.aggregate_snapshots(inputs, matrix, provenance)

    def test_snapshot_cli_and_markdown_escape(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "board-a/app.ino.map"
            source.parent.mkdir()
            source.write_text("Linker script and memory map\n.flash.text 0x40000000 0x20\n .text.test 0x40000000 0x20 /build/sketch/mcp_tools.cpp.o\n")
            snapshot_path = root / "reports/board-a.json"
            summary_path = root / "summary.md"
            args = ["report", str(source), "--snapshot", str(snapshot_path), "--summary", str(summary_path),
                    "--commit", "abc", "--ref", "v1.0.0", "--fqbn", "esp32:test"]
            with patch.object(sys, "argv", args):
                report.main()
            snapshot = json.loads(snapshot_path.read_text())
            self.assertEqual(snapshot["board"], "board-a")
            self.assertEqual(snapshot["provenance"]["commit"], "abc")
            self.assertEqual(snapshot["fqbn"], "esp32:test")
            self.assertIn("MCP", summary_path.read_text())
            snapshot["board"] = "<script>|bad\nname"
            summary = report.markdown_summary({"reports": [snapshot]})
            self.assertNotIn("<script>", summary)
            self.assertIn("&#124;", summary)


class FirmwareSizeReportTest(unittest.TestCase):
    def test_retained_sections_only_and_no_double_counting(self):
        text = """
Discarded input sections
 .text.discarded 0x1000 0xffff /build/sketch/objs.a(mcp_tools.cpp.o)
Linker script and memory map
.flash.text 0x40000000 0x100
 .text.mcp_call
                0x40000000 0x20 /build/sketch/objs.a(mcp_tools.cpp.o)
                0x40000000 mcp_call()
 *fill*         0x40000020 0x4
 .text.alarm 0x40000024 0x10 /build/sketch/alarm_manager.cpp.o
 .text.shared 0x40000034 0x8 /build/libraries/objs.a(mcp_tools.cpp.o)
.flash.rodata 0x40200000 0x100
 .rodata._ZL31alarm_schedule_fragment_html_gz
                0x40200000 0x40 /build/sketch/objs.a(web_portal_pages.cpp.o)
 .rodata._ZL20mcp_fragment_html_gz 0x40200040 0x30 /build/sketch/objs.a(web_portal_pages.cpp.o)
 .rodata.portal_all_js_gz 0x40200070 0x20 /build/sketch/objs.a(web_portal_pages.cpp.o)
 .rodata.constant 0x40200090 0x4 /build/sketch/objs.a(alarm_manager.cpp.o)
.dram0.data 0x4ff00000 0x10 load address 0x40200100
 .data.setting 0x4ff00000 0x4 /build/sketch/objs.a(alarm_manager.cpp.o)
.dram0.bss 0x4ff00010 0x100
 .bss.alarm_state 0x4ff00010 0x80 /build/sketch/objs.a(alarm_manager.cpp.o)
 .bss.empty 0x4ff00090 0x0 /build/sketch/objs.a(alarm_manager.cpp.o)
.debug_info 0x0 0x100
 .debug_info 0x0 0x100 /build/sketch/objs.a(alarm_manager.cpp.o)
Cross Reference Table
 .text.fake 0x40001000 0xffff /build/sketch/objs.a(alarm_manager.cpp.o)
"""
        totals = report.summarize(report.parse_map(text))
        self.assertEqual(totals["MCP (HAS_MCP)"], dict(code=32, data=0, assets=48, static_ram=0))
        self.assertEqual(totals["Alarms (ALARM_ENABLED)"], dict(code=16, data=8, assets=64, static_ram=132))
        self.assertEqual(totals[report.SHARED], dict(code=8, data=0, assets=0, static_ram=0))
        self.assertEqual(totals["Web portal assets (shared)"]["assets"], 32)

    def test_idf_library_objects_and_rtc_code(self):
        text = """Linker script and memory map
.flash.text 0x40000000 0x100
 .text.driver 0x40000000 0x20 /sdk/libdriver.a(driver.c.obj)
.rtc.text 0x50100000 0x100
 .rtc.text 0x50100000 0x10 /build/sketch/objs.a(alarm_manager.cpp.o)
"""
        totals = report.summarize(report.parse_map(text))
        self.assertEqual(totals["Arduino / ESP-IDF core"]["code"], 32)
        self.assertEqual(totals["Alarms (ALARM_ENABLED)"]["code"], 16)
        self.assertEqual(totals["Alarms (ALARM_ENABLED)"]["data"], 0)

    def test_output_section_controls_static_ram_and_no_load_data(self):
        text = """Linker script and memory map
.dram0.data 0x4ff00000 0x100
 .rodata.constant 0x4ff00000 0x10 /build/sketch/objs.a(alarm_manager.cpp.o)
.flash.rodata 0x40200000 0x100
 .sdata2.constant 0x40200000 0x20 /build/sketch/objs.a(alarm_manager.cpp.o)
.rtc_noinit 0x50100000 0x100
 .rtc_noinit 0x50100000 0x8 /build/sketch/objs.a(alarm_manager.cpp.o)
.flash.rodata_noload 0x40200100 0x100
 .rodata.external 0x40200100 0x40 /build/sketch/objs.a(alarm_manager.cpp.o)
"""
        totals = report.summarize(report.parse_map(text))
        self.assertEqual(totals["Alarms (ALARM_ENABLED)"], dict(code=0, data=48, assets=0, static_ram=24))

    def test_unsupported_map_rejected(self):
        for text in ("", "Discarded input sections", "Linker script and memory map\n"):
            with self.subTest(text=text), self.assertRaises(ValueError):
                report.parse_map(text)

    def test_unknown_feature_is_shared(self):
        self.assertEqual(report.attribute(".text.alarm_helper", "/build/sketch/objs.a(actions.cpp.o)"),
                         ("Actions", False))
        self.assertEqual(report.attribute(".rodata.portal_all_js_gz", "/build/sketch/objs.a(web_portal.cpp.o)"),
                         ("Web portal assets (shared)", True))

    def test_subsystem_ownership_and_priority(self):
        cases = {
            "/build/libraries/lvgl/draw/objs.a(lv_draw.c.o)": "LVGL",
            "/sdk/libmbedtls.a(ssl.c.obj)": "TLS / cryptography (shared)",
            "/sdk/liblwip.a(tcp.c.obj)": "Wi-Fi / networking (shared)",
            "/sdk/libesp_driver_i2s.a(i2s.c.obj)": "Audio (HAS_AUDIO)",
            "/sdk/libfreertos.a(tasks.c.obj)": "Arduino / ESP-IDF core",
            "/sdk/libstdc++.a(new.o)": "C / C++ runtime",
            "/build/core/core.a(main.cpp.o)": "Arduino / ESP-IDF core",
            "/build/sketch/objs.a(web_portal_ota.cpp.o)": "OTA / firmware updates",
            "/build/sketch/objs.a(web_portal.cpp.o)": "Web portal (shared)",
            "/build/sketch/objs.a(web_mcp.cpp.o)": "MCP (HAS_MCP)",
            "/build/sketch/objs.a(alarm_binding.cpp.o)": "Alarms (ALARM_ENABLED)",
            "/build/sketch/objs.a(pad_binding.cpp.o)": "Bindings / expressions",
            "/build/sketch/objs.a(pad_config.cpp.o)": "Pads / screens (HAS_DISPLAY)",
            "/build/sketch/objs.a(custom_fonts.cpp.o)": "Fonts (HAS_CUSTOM_FONTS)",
            "/build/sketch/objs.a(native_extension.cpp.o)": "Native extensions (HAS_NATIVE_EXTENSIONS)",
            "/build/sketch/objs.a(ble_hid.cpp.o)": "BLE HID (HAS_BLE_HID)",
            "/build/sketch/objs.a(web_portal_ble.cpp.o)": "BLE HID (HAS_BLE_HID)",
            "/build/sketch/objs.a(usb_hid.cpp.o)": "USB HID (HAS_USB_HID)",
            "/build/sketch/objs.a(mouse_hid.cpp.o)": "USB HID (HAS_USB_HID)",
            "/build/sketch/objs.a(gamepad_hid.cpp.o)": "USB HID (HAS_USB_HID)",
            "/build/sketch/objs.a(keyboard_hid.cpp.o)": "Shared HID",
            "/build/sketch/objs.a(ble_telemetry.cpp.o)": "Bluetooth stack / telemetry (HAS_BLE_HID / HAS_BLE)",
            "/build/libraries/BLE/objs.a(BLEDevice.cpp.o)": "Bluetooth stack / telemetry (HAS_BLE_HID / HAS_BLE)",
            "/build/libraries/USB/objs.a(USBHIDKeyboard.cpp.o)": "USB HID (HAS_USB_HID)",
            "/sdk/libbt.a(bt.c.obj)": "Bluetooth stack / telemetry (HAS_BLE_HID / HAS_BLE)",
            "/sdk/libesp_hid.a(hid.c.obj)": "Shared HID",
            "/sdk/libarduino_tinyusb.a(tinyusb.c.obj)": "USB HID (HAS_USB_HID)",
            "/sdk/libusb.a(usb.c.obj)": "USB HID (HAS_USB_HID)",
            "/sdk/libunknown.a(mcp_tools.cpp.o)": report.SHARED,
        }
        for owner, expected in cases.items():
            with self.subTest(owner=owner):
                self.assertEqual(report.attribute(".text.symbol", owner), (expected, False))

    def test_hid_split_keeps_shared_assets_and_separate_flags(self):
        text = """Linker script and memory map
.flash.text 0x40000000 0x100
 .text.ble 0x40000000 0x20 /build/sketch/objs.a(ble_hid.cpp.o)
 .text.usb 0x40000020 0x30 /build/sketch/objs.a(usb_hid.cpp.o)
 .text.keyboard 0x40000050 0x10 /build/sketch/objs.a(keyboard_hid.cpp.o)
 .text.telemetry 0x40000060 0x8 /build/sketch/objs.a(ble_telemetry.cpp.o)
.flash.rodata 0x40200000 0x100
 .rodata.hid_fragment_html_gz 0x40200000 0x40 /build/sketch/objs.a(web_portal_pages.cpp.o)
"""
        totals = report.summarize(report.parse_map(text))
        self.assertEqual(totals["BLE HID (HAS_BLE_HID)"]["code"], 32)
        self.assertEqual(totals["USB HID (HAS_USB_HID)"]["code"], 48)
        self.assertEqual(totals["Shared HID"]["assets"], 64)
        self.assertEqual(totals["Bluetooth stack / telemetry (HAS_BLE_HID / HAS_BLE)"]["code"], 8)
        self.assertEqual(sum(report.flash_bytes(values) for values in totals.values()), 168)
        snapshot = report.compact_snapshot(totals, {"board_directory": "fixture"})
        flags = {row["name"]: row["flags"] for row in snapshot["subsystems"]}
        self.assertEqual(flags["BLE HID"], ["HAS_BLE_HID"])
        self.assertEqual(flags["USB HID"], ["HAS_USB_HID"])
        self.assertEqual(flags["Shared HID"], [])
        self.assertEqual(flags["Bluetooth stack / telemetry"], ["HAS_BLE_HID", "HAS_BLE"])
        bluetooth = next(row for row in snapshot["subsystems"] if row["name"] == "Bluetooth stack / telemetry")
        self.assertNotIn("flag_note", bluetooth)

    def test_linker_merged_pools_not_attributed_to_first_module(self):
        text = """Linker script and memory map
.flash.rodata 0x40200000 0x100
 .rodata.catalog.str1.4
                0x40200000 0x50 /build/sketch/objs.a(action_catalog.cpp.o)
                0x4 (size before relaxing)
 .rodata.other.str1.4 0x40200050 0x30 /build/sketch/objs.a(mcp_tools.cpp.o)
 .rodata.str1.4 0x40200050 0x20 /build/sketch/objs.a(alarm_manager.cpp.o)
 .rodata.real 0x40200050 0x4 /build/sketch/objs.a(alarm_manager.cpp.o)
 .srodata.cst4 0x40200054 0x10 /build/sketch/objs.a(mcp_tools.cpp.o)
                0x4 (size before relaxing)
"""
        totals = report.summarize(report.parse_map(text))
        self.assertEqual(totals["Linker-merged strings / constants (shared)"]["data"], 96)
        self.assertEqual(totals["Actions"]["data"], 0)
        self.assertEqual(totals["MCP (HAS_MCP)"]["data"], 0)
        self.assertEqual(totals["Alarms (ALARM_ENABLED)"]["data"], 4)
        self.assertEqual(sum(report.flash_bytes(values) for values in totals.values()), 100)

    def test_context_with_binary_and_app_slots(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "app.ino.map"
            path.write_text("fixture")
            path.with_suffix(".bin").write_bytes(bytes(768))
            entries = b"".join(struct.pack("<HBBII16sI", 0x50AA, 0, subtype, offset, size, label, 0)
                               for subtype, offset, size, label in ((0x10, 0x10000, 1024, b"app0"),
                                                                    (0x11, 0x20000, 2048, b"app1")))
            path.with_suffix(".partitions.bin").write_bytes(entries + b"\xff" * 32)
            context = report.build_context(path)
            self.assertEqual(context["application_binary_bytes"], 768)
            self.assertEqual(context["app_partitions"][0]["headroom_bytes"], 256)
            self.assertEqual(context["app_partitions"][0]["usage_percent"], 75)
            self.assertTrue(context["app_partitions"][0]["selected"])
            self.assertFalse(context["app_partitions"][1]["selected"])
            self.assertEqual([entry["label"] for entry in context["partitions"]], ["app0", "app1"])
            self.assertEqual(context["partition_extent_bytes"], 0x20000 + 2048)
            self.assertIsNone(context["flash_size_bytes"])
            self.assertEqual(context["warnings"], [])

    def test_full_flash_capacity_is_declared_not_inferred_from_partition_extent(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            path = root / "test-board" / "app.ino.map"
            path.parent.mkdir()
            path.write_text("fixture")
            path.with_suffix(".bin").write_bytes(bytes(768))
            path.with_suffix(".partitions.bin").write_bytes(
                struct.pack("<HBBII16sI", 0x50AA, 0, 0x10, 0x10000, 1024, b"app0", 0)
                + struct.pack("<HBBII16sI", 0x50AA, 1, 0x82, 0x20000, 2048, b"storage", 0)
            )
            metadata = root / "src" / "boards" / "test-board" / "metadata.json"
            metadata.parent.mkdir(parents=True)
            metadata.write_text(json.dumps({"flash_mb": 32}))
            with patch.object(report, "__file__", str(root / "tools" / "firmware_size_report.py")):
                context = report.build_context(path)
                self.assertEqual(context["flash_size_bytes"], 32 * 1024 * 1024)
                self.assertEqual(context["flash_size_source"], "board-metadata")
                self.assertEqual(context["partition_extent_bytes"], 0x20000 + 2048)
                self.assertEqual(context["partitions"][1]["type"], 1)
                self.assertEqual(context["app_partitions"][0]["headroom_bytes"], 256)
                metadata.write_text(json.dumps({"flash_mb": 0.0625}))
                self.assertTrue(any("extends beyond" in warning for warning in report.build_context(path)["warnings"]))
                for invalid in ("invalid", '{"flash_mb": Infinity}'):
                    metadata.write_text(invalid)
                    self.assertIsNone(report.build_context(path)["flash_size_bytes"])

    def test_context_missing_optional_artifacts(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "app.ino.map"
            path.write_text("fixture")
            context = report.build_context(path)
            self.assertIsNone(context["application_binary_bytes"])
            self.assertEqual(context["app_partitions"], [])
            self.assertEqual(len(context["warnings"]), 2)

    def test_context_invalid_partitions(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "app.ino.map"
            path.write_text("fixture")
            path.with_suffix(".partitions.bin").write_bytes(bytes(32))
            context = report.build_context(path)
            self.assertTrue(any("Invalid partition entry" in warning for warning in context["warnings"]))

    def test_context_stale_binary_and_oversized_image(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "app.ino.map"
            path.write_text("fixture")
            binary = path.with_suffix(".bin")
            binary.write_bytes(bytes(2048))
            os.utime(binary, (path.stat().st_mtime - 120, path.stat().st_mtime - 120))
            path.with_suffix(".partitions.bin").write_bytes(
                struct.pack("<HBBII16sI", 0x50AA, 0, 0x10, 0x10000, 1024, b"app0", 0)
            )
            context = report.build_context(path)
            self.assertEqual(context["app_partitions"][0]["headroom_bytes"], -1024)
            self.assertEqual(context["app_partitions"][0]["usage_percent"], 200)
            self.assertTrue(any("mismatched" in warning for warning in context["warnings"]))

    def test_table_percentage_and_totals(self):
        totals = report.summarize([report.Contribution("LVGL", ".text.draw", "fixture.o", code=1024),
                                   report.Contribution("Fonts (HAS_CUSTOM_FONTS)", ".rodata.font", "fixture.o", data=1024),
                                   report.Contribution("Alarms (ALARM_ENABLED)", ".bss.state", "fixture.o", static_ram=1024)])
        context = dict(board_directory="fixture", map_timestamp_utc="timestamp", application_binary_bytes=2200,
                       app_partitions=[], warnings=[])
        output = io.StringIO()
        with redirect_stdout(output):
            report.print_report(totals, Path("fixture.map"), context)
        self.assertIn("Attributed flash payload: 2.00 KiB", output.getvalue())
        self.assertEqual(output.getvalue().count("50.00%"), 2)
        self.assertIn("TOTAL", output.getvalue())
        self.assertNotIn("Camera (HAS_CAMERA)", output.getvalue())
        self.assertNotIn("Audio (HAS_AUDIO)", output.getvalue())
        self.assertIn("Alarms (ALARM_ENABLED)", output.getvalue())
        self.assertEqual(totals["Camera (HAS_CAMERA)"], dict(code=0, data=0, assets=0, static_ram=0))


class VisualExportTest(unittest.TestCase):
    def test_site_export_copies_assets_and_compact_real_and_demo_reports(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            path = root / "test-board" / "app.ino.map"
            path.parent.mkdir()
            path.write_text("""Linker script and memory map
.flash.text 0x40000000 0x100
 .text.mcp_call 0x40000000 0x20 /private/sketch/objs.a(mcp_tools.cpp.o)
""")
            path.with_suffix(".bin").write_bytes(bytes(128))
            path.with_suffix(".partitions.bin").write_bytes(
                struct.pack("<HBBII16sI", 0x50AA, 0, 0x10, 0x10000, 1024, b"app0", 0)
            )
            output = root / "preview"
            output.mkdir()
            marker = output / "keep.txt"
            marker.write_text("unrelated")
            report.export_site([path], output, include_demos=True)
            for asset in ("index.html", "report.css", "report.js"):
                self.assertEqual((output / asset).read_bytes(),
                                 (Path(report.__file__).parent / "firmware-size-view" / asset).read_bytes())
            dataset = json.loads((output / "reports.json").read_text())
            self.assertEqual(dataset["schema_version"], 1)
            self.assertIn("generated_utc", dataset)
            self.assertEqual(len(dataset["reports"]), 4)
            snapshot = dataset["reports"][0]
            self.assertEqual(snapshot["board"], "test-board")
            self.assertEqual(snapshot["context"]["application_binary_bytes"], 128)
            self.assertEqual(snapshot["context"]["app_partitions"][0]["headroom_bytes"], 896)
            self.assertEqual(snapshot["attributed_flash_bytes"], 32)
            self.assertEqual(len(snapshot["subsystems"]), 1)
            self.assertNotIn("/private", json.dumps(dataset))
            self.assertEqual(marker.read_text(), "unrelated")
            self.assertFalse((output / "app.ino.bin").exists())

    def test_site_export_can_run_without_firmware_artifacts(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "preview"
            report.export_site([], output, include_demos=True)
            dataset = json.loads((output / "reports.json").read_text())
            self.assertEqual(len(dataset["reports"]), 3)
            self.assertTrue(all(snapshot["source"] == "demo" for snapshot in dataset["reports"]))

    def test_site_export_rejects_empty_or_invalid_input(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            with self.assertRaises(ValueError):
                report.export_site([], root / "preview")
            path = root / "invalid.map"
            path.write_text("invalid")
            with self.assertRaises(ValueError):
                report.export_site([path], root / "preview")

    def test_compact_snapshot_hides_zero_rows_and_has_no_runner_paths(self):
        totals = report.summarize([report.Contribution("MCP (HAS_MCP)", ".text.run", "/private/sketch/test.o", code=4096),
                                   report.Contribution("Alarms (ALARM_ENABLED)", ".bss.state", "/private/test.o", static_ram=128)])
        context = dict(board_directory="test-board", map_timestamp_utc="timestamp", application_binary_bytes=5000,
                       app_partitions=[], warnings=[])
        snapshot = report.compact_snapshot(totals, context)
        self.assertEqual(snapshot["attributed_flash_bytes"], 4096)
        self.assertEqual(len(snapshot["subsystems"]), 2)
        self.assertEqual(snapshot["subsystems"][0]["flags"], ["HAS_MCP"])
        self.assertNotIn("/private", str(snapshot))
        self.assertEqual(snapshot["source"], "local-build")

    def test_demo_scenarios_are_distinct_and_labeled(self):
        snapshots = report.demo_snapshots()
        self.assertEqual(len(snapshots), 3)
        self.assertTrue(all(snapshot["source"] == "demo" for snapshot in snapshots))
        self.assertGreater(snapshots[0]["context"]["app_partitions"][0]["headroom_bytes"], 0)
        self.assertLess(snapshots[1]["context"]["app_partitions"][0]["headroom_bytes"], 0)
        self.assertIsNone(snapshots[2]["context"]["application_binary_bytes"])
        self.assertEqual([entry["label"] for entry in snapshots[0]["context"]["partitions"]],
                 ["app0", "app1", "storage"])
        self.assertEqual(snapshots[0]["context"]["flash_size_source"], "demo")


class FlagCoverageTest(unittest.TestCase):
    @staticmethod
    def declared_flags(root):
        defaults, unconditional = parse_board_config_defaults(root / "src" / "app" / "board_config.h")
        names = set(defaults) | set(unconditional)
        for path in (root / "src" / "boards").rglob("board_overrides.h"):
            names.update(parse_all_defines(path, follow_includes=True))
        return {name for name in names if report.FEATURE_FLAG_PATTERN.fullmatch(name)}

    @staticmethod
    def coverage_errors(root):
        declared = FlagCoverageTest.declared_flags(root)
        mapped = {flag for name in report.FEATURES for flag in report.subsystem_flags(name)}
        accounted = mapped | set(report.FLAG_SUBSYSTEMS) | set(report.EXCLUDED_FLAGS)
        errors = []
        for flag in sorted(declared - accounted):
            errors.append(f"Unclassified flag {flag}: update FEATURES, FLAG_SUBSYSTEMS, or EXCLUDED_FLAGS "
                          "in tools/firmware_size_report.py.")
        for flag in sorted(accounted - declared):
            errors.append(f"Stale classification {flag}: remove or rename its firmware-size report entry.")
        for flag, target in report.FLAG_SUBSYSTEMS.items():
            if target not in report.FEATURES:
                errors.append(f"Invalid subsystem for {flag}: {target}.")
        for flag, reason in report.EXCLUDED_FLAGS.items():
            if not isinstance(reason, str) or not reason.strip():
                errors.append(f"Excluded flag {flag} requires a reason.")
            if flag in mapped or flag in report.FLAG_SUBSYSTEMS:
                errors.append(f"Flag {flag} is both mapped and excluded.")
        return errors

    @staticmethod
    def write_fixture(root, flags):
        header = root / "src" / "app" / "board_config.h"
        header.parent.mkdir(parents=True, exist_ok=True)
        header.write_text("\n".join(f"#define {flag} false" for flag in sorted(flags)) + "\n")

    def test_repository_feature_flags_are_classified(self):
        errors = self.coverage_errors(Path(__file__).resolve().parents[1])
        self.assertEqual(errors, [], "\n".join(errors))

    def test_new_default_and_board_only_flags_fail_coverage(self):
        flags = self.declared_flags(Path(__file__).resolve().parents[1])
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.write_fixture(root, flags)
            self.assertEqual(self.coverage_errors(root), [])
            for flag in ("HAS_NEW_FEATURE", "IS_NEW_DEVICE"):
                self.write_fixture(root, flags | {flag})
                self.assertTrue(any(f"Unclassified flag {flag}" in error for error in self.coverage_errors(root)))
            self.write_fixture(root, flags)
            override = root / "src" / "boards" / "test-board" / "board_overrides.h"
            override.parent.mkdir(parents=True)
            override.write_text("#define HAS_BOARD_ONLY_FEATURE true\n")
            self.assertTrue(any("Unclassified flag HAS_BOARD_ONLY_FEATURE" in error
                                for error in self.coverage_errors(root)))

    def test_removed_or_renamed_flag_fails_coverage(self):
        flags = self.declared_flags(Path(__file__).resolve().parents[1])
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.write_fixture(root, (flags - {"HAS_MCP"}) | {"HAS_RENAMED_MCP"})
            errors = self.coverage_errors(root)
            self.assertTrue(any("Stale classification HAS_MCP" in error for error in errors))
            self.assertTrue(any("Unclassified flag HAS_RENAMED_MCP" in error for error in errors))

    def test_classifications_require_valid_targets_and_exclusion_reasons(self):
        root = Path(__file__).resolve().parents[1]
        with patch.dict(report.FLAG_SUBSYSTEMS, {"HAS_MUSIC_ANALYSIS": "Missing subsystem"}):
            self.assertTrue(any("Invalid subsystem for HAS_MUSIC_ANALYSIS" in error
                                for error in self.coverage_errors(root)))
        with patch.dict(report.EXCLUDED_FLAGS, {"HAS_PSRAM": ""}):
            self.assertTrue(any("Excluded flag HAS_PSRAM requires a reason" in error
                                for error in self.coverage_errors(root)))
        with patch.dict(report.EXCLUDED_FLAGS, {"HAS_MCP": "Conflicting classification"}):
            self.assertTrue(any("Flag HAS_MCP is both mapped and excluded" in error
                                for error in self.coverage_errors(root)))

    def test_mapped_flags_appear_in_compact_report(self):
        totals = report.summarize([
            report.Contribution("Music playback / analysis", ".text.music", "fixture.o", code=32),
            report.Contribution("Sensors / power", ".text.sensor", "fixture.o", code=16),
            report.Contribution("Device classes", ".text.device", "fixture.o", code=8),
        ])
        snapshot = report.compact_snapshot(totals, {"board_directory": "fixture"})
        flags = {row["name"]: row["flags"] for row in snapshot["subsystems"]}
        self.assertEqual(flags["Music playback / analysis"], ["HAS_MUSIC_ANALYSIS"])
        self.assertIn("HAS_SENSOR_AHT10", flags["Sensors / power"])
        self.assertIn("HAS_SENSOR_HX711", flags["Sensors / power"])
        self.assertIn("IS_COFFEE_SCALE", flags["Device classes"])
        self.assertNotIn("HAS_PSRAM", {flag for values in flags.values() for flag in values})


if __name__ == "__main__":
    unittest.main()