#!/usr/bin/env python3
"""Behavioral coverage for release selection and offline firmware reports."""

import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import firmware_history as history


def release(tag, size, **overrides):
    result = {"tag_name": tag, "published_at": f"2026-01-{int(tag[-1]):02}T00:00:00Z",
              "draft": False, "prerelease": False,
              "html_url": f"https://github.com/example/repo/releases/tag/{tag}",
              "assets": [{"name": f"esp32-macropad-board-{tag}.bin", "size": size}]}
    result.update(overrides)
    return result


class FirmwareHistoryTest(unittest.TestCase):
    def test_exact_board_app_selection_order_and_missing_releases(self):
        first = release("v1", 1024)
        first["assets"].extend([
            {"name": "esp32-macropad-board-v1-merged.bin", "size": 999999},
            {"name": "esp32-macropad-board-voice-v1.bin", "size": 999999},
            {"name": "esp32-macropad-board-v1-bootloader.bin", "size": 999999}])
        points, skipped = history.release_points([
            release("v5", 5000, draft=True), release("v4", 4000, prerelease=True),
            release("v3", 512), release("v2", 2000, assets=[]), first], "board")
        self.assertEqual([point["tag"] for point in points], ["v1", "v3"])
        self.assertEqual(skipped, ["v2"])
        self.assertIsNone(points[0]["delta_bytes"])
        self.assertEqual(points[1]["delta_bytes"], -512)
        self.assertEqual(points[1]["delta_percent"], -50)
        self.assertEqual(len(history.release_points([release("v4", 10, prerelease=True)], "board", True)[0]), 1)

    def test_missing_and_duplicate_assets_fail(self):
        with self.assertRaisesRegex(ValueError, "No app-only"):
            history.release_points([release("v1", 1)], "other")
        duplicate = release("v1", 1)
        duplicate["assets"] *= 2
        with self.assertRaisesRegex(ValueError, "ambiguous"):
            history.release_points([duplicate], "board")

    def test_release_highlights_cover_sections_and_wrapped_bullets(self):
        notes = ("### Added\n* Add **layers** to pads. More detail.\n"
                 "* Add a toggle\n  switch widget.\n* Third feature.\n"
                 "### Changed\n* Upgrade LVGL from 9.5.0 to 9.6.0. Keep acceleration disabled.\n"
                 "### Fixed\n* Fix [downloads](https://example.com).\n")
        highlights = history.release_highlights(notes)
        self.assertEqual(highlights, [
            {"category": "Added", "text": "Add layers to pads."},
            {"category": "Added", "text": "Add a toggle switch widget."},
            {"category": "Changed", "text": "Upgrade LVGL from 9.5.0 to 9.6.0."},
            {"category": "Fixed", "text": "Fix downloads."}])
        point = history.release_points([release("v1", 1024, body=notes)], "board")[0][0]
        self.assertEqual(point["release_notes"], notes)
        self.assertEqual(history.release_highlights(None), [])

    def test_snapshot_identity_and_binary_match(self):
        point = history.release_points([release("v1", 1024)], "board")[0][0]
        dataset = {"schema_version": 1, "provenance": {"ref": "v1"}, "reports": [
            {"board": "board", "source": "local-build", "context": {"application_binary_bytes": 1024}, "subsystems": []}]}
        history.attach_footprint(point, dataset, "board")
        self.assertIs(point["footprint"], dataset["reports"][0])
        dataset["reports"][0]["context"]["application_binary_bytes"] = 2048
        with self.assertRaisesRegex(ValueError, "size does not match"):
            history.attach_footprint(point, dataset, "board")
        dataset["provenance"]["ref"] = "v0"
        with self.assertRaisesRegex(ValueError, "provenance"):
            history.attach_footprint(point, dataset, "board")

    def test_self_contained_output_and_offline_roundtrip(self):
        points = history.release_points([release("v1", 1024)], "board")[0]
        points[0]["release_notes"] = "### Added\n* Keep </script><img src=x onerror=alert(1)> as text."
        dataset = {"schema_version": 1, "repo": "example/repo", "board": "</script><img>",
                   "generated_utc": "2026-01-01", "include_prereleases": False,
                   "points": points, "skipped_releases": [], "warnings": []}
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            history.write_report(dataset, root)
            page = (root / "index.html").read_text()
            self.assertIn("&lt;/script&gt;&lt;img&gt;", page)
            self.assertNotIn("<script src=", page)
            self.assertNotIn("<img src=x", page)
            self.assertNotIn("<link ", page)
            self.assertEqual(page.count("<svg "), 2)
            self.assertIn("No matching footprint", page)
            self.assertEqual(json.loads((root / "history.json").read_text()), dataset)
            self.assertEqual(history.main(["--input", str(root / "history.json"), "--output", str(root / "offline")]), 0)
            self.assertEqual((root / "offline/index.html").read_text(), page)


if __name__ == "__main__":
    unittest.main()