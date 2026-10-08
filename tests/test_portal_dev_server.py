import copy
import importlib.util
import json
from pathlib import Path
import re
import sys
import threading
import unittest
from urllib.error import HTTPError
from urllib.request import Request, urlopen
from urllib.parse import urlencode


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
SPEC = importlib.util.spec_from_file_location("portal_dev_server", ROOT / "tools/portal-dev-server.py")
PORTAL = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PORTAL)


class PortalDevServerTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.server = PORTAL.HTTPServer(("127.0.0.1", 0), PORTAL.PortalHandler)
        cls.server.profile = "esp32-p4-lcd4b"
        cls.server.logs_started = PORTAL.time.monotonic()
        cls.server.mock_config = {
            **PORTAL.SCREENSAVER_DEFAULTS,
            "caps": {"ble": True, "ble_hid": True, "usb_hid": True, "mqtt": True},
            "backlight_brightness": 80, "keyboard_transport": "none",
            "keyboard_active_transport": "none", "device_name": "Mock",
        }
        cls.thread = threading.Thread(target=cls.server.serve_forever, daemon=True)
        cls.thread.start()
        cls.base = f"http://127.0.0.1:{cls.server.server_port}"

    @classmethod
    def tearDownClass(cls):
        cls.server.shutdown()
        cls.server.server_close()
        cls.thread.join()

    def setUp(self):
        PORTAL.reset_pad_fixtures(self.server)

    def request(self, path, method="GET", body=None, profile="esp32-p4-lcd4b", raw=False):
        headers = {"Cookie": "portal_mock_profile=" + profile}
        data = json.dumps(body).encode() if body is not None else None
        request = Request(self.base + path, data=data, headers=headers, method=method)
        try:
            response = urlopen(request, timeout=5)
        except HTTPError as error:
            response = error
        with response:
            text = response.read().decode()
            return response.status, text if raw else json.loads(text)

    def test_production_assets_and_templates(self):
        status, fragment = self.request("/api/section/pad-editor", raw=True)
        self.assertEqual(status, 200)
        self.assertNotIn("{{", fragment)
        self.assertIn('id="pad-edit-gauge-min"', fragment)
        status, bundle = self.request("/portal.js", raw=True)
        self.assertEqual(status, 200)
        self.assertIn("async function padInit()", bundle)
        self.assertLess(bundle.index("function actionEditorHTML("), bundle.index("async function padInit()"))
        self.assertNotIn("portal_shutter", bundle)
        self.assertEqual(self.request("/api/bindings")[0], 200)

    def test_alarms(self):
        status, alarm = self.request("/api/component/alarms/config")
        self.assertEqual(status, 200)
        self.assertFalse(alarm["1"]["enabled"])
        self.assertEqual(alarm["lateness_minutes"], 360)
        for minutes in (0, 360, 10080):
            alarm["lateness_minutes"] = minutes
            self.assertEqual(self.request("/api/component/alarms/config", "POST", alarm)[0], 200)
            self.assertEqual(self.request("/api/component/alarms/config")[1]["lateness_minutes"], minutes)
            self.assertEqual(self.request("/api/component/alarms/status")[1]["lateness_minutes"], minutes)
        for minutes in (-1, 10081, 1.5, True, "360"):
            invalid = copy.deepcopy(alarm)
            invalid["lateness_minutes"] = minutes
            self.assertEqual(self.request("/api/component/alarms/config", "POST", invalid)[0], 400)
        alarm["lateness_minutes"] = 360
        alarm["1"].update(enabled=True, hour=8, on_ring=[{"type": "sound_alert", "sound_alert_kind": "tone_loop"}])
        self.assertEqual(self.request("/api/component/alarms/config", "POST", alarm)[0], 200)
        self.assertEqual(self.request("/api/component/alarms/config")[1], alarm)
        invalid = copy.deepcopy(alarm)
        invalid["1"]["on_ring"] = [{"type": "delay", "duration_ms": 10}]
        self.assertEqual(self.request("/api/component/alarms/config", "POST", invalid)[0], 400)
        self.request("/__mock/alarm", "POST", {"state": "ringing", "active_id": 1})
        self.assertEqual(self.request("/api/component/alarms/snooze", "POST", {})[0], 200)
        self.assertEqual(self.request("/api/component/alarms/status")[1]["state"], "snoozed")
        self.assertEqual(self.request("/api/component/alarms/cancel", "POST", {})[0], 200)
        self.assertEqual(self.request("/api/component/alarms/status")[1]["state"], "idle")
        alarm["1"]["weekdays"] = 0
        self.assertEqual(self.request("/api/component/alarms/config", "POST", alarm)[0], 200)
        once = self.request("/api/component/alarms/status")[1]
        self.assertTrue(once["enabled"])
        self.assertGreater(once["once_epoch"], PORTAL.MOCK_NOW_EPOCH)
        self.assertRegex(once["once_local"], r"2026-10-0[78] 08:00")
        self.assertEqual(self.request("/api/component/alarms/config", "POST", alarm)[0], 200)
        self.assertEqual(self.request("/api/component/alarms/status")[1]["once_epoch"], once["once_epoch"])
        self.request("/__mock/alarm", "POST", {"state": "ringing", "active_id": 1})
        self.assertFalse(self.request("/api/component/alarms/config")[1]["1"]["enabled"])
        self.assertEqual(self.request("/api/component/alarms/status")[1]["once_epoch"], 0)
        self.assertEqual(self.request("/api/component/alarms/snooze", "POST", {})[0], 200)
        self.assertEqual(self.request("/api/component/alarms/status")[1]["state"], "snoozed")
        self.assertEqual(self.request("/api/component/alarms/config", profile="reterminal-e1003-frame")[0], 404)
        self.assertEqual(self.request("/api/section/alarms", profile="reterminal-e1003-frame", raw=True)[0], 404)
        self.assertEqual(self.request("/portal_alarms.js", profile="reterminal-e1003-frame", raw=True)[0], 404)
        self.assertIn("init_alarms_fragment", self.request("/portal_alarms.js", raw=True)[1])
        self.assertIn('server->on("/portal_alarms.js", HTTP_GET, handlePortalAlarmsJS)',
                  (ROOT / "src/app/web_portal_routes.cpp").read_text())
        bundle = self.request("/portal.js", raw=True)[1]
        self.assertIn("if (!navigationReady) return;", bundle)
        self.assertNotIn("loadNavigationAssets(data)", bundle)
        self.assertLess(bundle.index("await loadItemAssets("),
            bundle.index("var initFn = window["))
        fragment = self.request("/api/section/alarms", raw=True)[1]
        self.assertNotIn('id="alarm-snooze"', fragment)
        self.assertNotIn('id="alarm-cancel"', fragment)
        self.assertNotIn('id="alarm-state"', fragment)
        self.assertIn('id="alarm-repeat-summary"', fragment)
        self.assertIn('id="alarm-once-target"', fragment)
        self.assertIn('id="alarm-lateness-minutes"', fragment)
        self.assertNotIn("setTimeout", self.request("/portal_alarms.js", raw=True)[1])

    def test_alarm_authoring_metadata(self):
        catalog = self.request("/api/info?catalog=1")[1]["catalog"]
        action = next(item for item in catalog if item["type"] == "alarm")
        header = (ROOT / "src/app/alarm_manager.h").read_text()
        commands = re.search(r'alarm_command_names\[\].*?\{(.*?)\}', header, re.S).group(1)
        self.assertEqual([item["id"] for item in action["commands"]], [item for item in re.findall(r'"([^"]*)"', commands) if item])
        fields = {field["name"]: field for field in action["editor_fields"]}
        self.assertTrue(fields["alarm_value"]["bindable"])
        self.assertEqual(fields["alarm_id"]["default"], "1")
        self.assertEqual([item["id"] for item in fields["alarm_day"]["options"]], list(range(7)))
        schema = self.request("/api/bindings")[1]
        alarm = next(item for item in schema["schemes"] if item["name"] == "alarm")
        source = (ROOT / "src/app/alarm_binding.cpp").read_text()
        keys = re.search(r'alarm_keys\[\].*?\{(.*?)\}', source, re.S).group(1)
        self.assertEqual(alarm["keys"], re.findall(r'"([^"]+)"', keys))

    def test_timezone(self):
        status, catalog = self.request("/api/component/timezone/catalog")
        self.assertEqual(status, 200)
        self.assertGreaterEqual(len(catalog["cities"]), 50)
        self.assertLessEqual(len(catalog["cities"]), 70)
        kathmandu = next(city for city in catalog["cities"] if city["name"] == "Asia/Kathmandu")
        before = self.request("/api/config")[1]
        status, preview = self.request("/api/component/timezone/preview?" + urlencode({"timezone": kathmandu["posix"]}))
        self.assertEqual(status, 200)
        self.assertEqual(preview["utc_offset"], "+0545")
        self.assertTrue(preview["ready"])
        self.assertEqual(self.request("/api/config")[1], before)
        self.assertEqual(self.request("/api/component/timezone/preview?timezone=")[0], 400)
        self.assertIn('id="timezone-city"', self.request("/api/section/timezone", raw=True)[1])
        self.assertNotIn('id="timezone"', self.request("/api/section/device-name", raw=True)[1])
        self.assertIn("init_timezone_fragment", self.request("/portal.js", raw=True)[1])
        nav = self.request("/api/portal/nav", profile="reterminal-e1003-frame")[1]
        self.assertTrue(any(item["id"] == "timezone" for category in nav["categories"] for item in category["items"]))

    def test_logs(self):
        status, data = self.request("/api/logs?limit=100")
        self.assertEqual(status, 200)
        self.assertEqual(len(data["records"]), 32)
        self.assertTrue(data["has_more"])
        _, incremental = self.request(f"/api/logs?after={data['next']}&boot_id=123")
        self.assertEqual(incremental["records"][0]["sequence"], data["next"] + 1)
        _, boot = self.request("/api/logs?source=boot")
        self.assertEqual(len(boot["records"]), 8)
        self.assertTrue(boot["boot_complete"])
        self.assertEqual(self.request("/api/logs?after=-1")[0], 400)
        self.assertEqual(self.request("/api/logs?source=invalid")[0], 400)
        self.assertIn("init_logs_fragment", self.request("/portal-logs.js", raw=True)[1])
        self.assertIn('id="logs-output"', self.request("/api/section/logs", raw=True)[1])
        categories = self.request("/api/portal/nav")[1]["categories"]
        device = next(category for category in categories if category["id"] == "device")
        self.assertTrue(any(item["id"] == "logs" for item in device["items"]))

    def test_crash_logs(self):
        status, crash = self.request("/api/logs/crash")
        self.assertEqual(status, 200)
        self.assertTrue(crash["available"])
        self.assertEqual(crash["size"], 1024)
        self.assertEqual(crash["task"], "loopTask")
        self.assertEqual(len(crash["elf_sha256"]), 64)
        self.assertEqual(crash["exception_cause"], 7)
        self.assertEqual(crash["trap_value"], "0x500d2000")
        self.assertEqual(crash["registers"]["RA"], "0x400cfc8c")
        self.assertEqual(crash["current_reset_reason"], 3)
        self.assertNotEqual(crash["elf_sha256"], crash["current_elf_sha256"])
        with urlopen(self.base + "/api/logs/crash/download", timeout=5) as response:
            self.assertEqual(response.status, 200)
            self.assertEqual(response.headers["Content-Type"], "application/octet-stream")
            self.assertEqual(response.read(), bytes(range(256)) * 4)
        fragment = self.request("/api/section/logs", raw=True)[1]
        self.assertIn('id="logs-crash-download"', fragment)
        self.assertIn('id="logs-crash-details"', fragment)
        self.assertIn('id="logs-crash-copy"', fragment)
        self.assertIn('id="logs-crash-registers"', fragment)
        self.assertIn('Exception PC', fragment)

    def test_profile_and_capabilities(self):
        _, info = self.request("/api/info?catalog=1")
        self.assertTrue(info["has_display"])
        self.assertEqual((info["display_coord_width"], info["display_coord_height"]), (720, 720))
        self.assertEqual(len(info["available_screens"]), 17)
        self.assertFalse(info["has_camera"])
        self.assertNotIn("catalog", self.request("/api/info")[1])
        self.assertFalse(self.request("/api/config")[1]["caps"]["ble_hid"])
        self.assertEqual(self.request("/api/portal/nav")[1]["primary"]["fragment"], "pad-editor")
        self.assertEqual(self.request("/api/portal/nav", profile="reterminal-e1003-frame")[1]["primary"]["fragment"], "epaper-image")
        self.assertEqual(self.request("/api/section/epaper-image", profile="reterminal-e1003-frame", raw=True)[0], 200)
        self.assertEqual(self.request("/?profile=unknown", raw=True)[0], 400)

    def test_layouts_and_template(self):
        for pad in self.server.mock_pads.values():
            self.assertTrue(PORTAL.PortalHandler._valid_pad(pad))
        self.assertEqual(len(self.server.mock_pads["5"]["buttons"]), 64)
        self.assertEqual(self.server.mock_pads["1"]["buttons"], [])
        self.assertEqual(self.request("/api/pad?page=4")[1]["template_pad"], 3)
        self.assertEqual(len(self.request("/api/pad?page=3")[1]["buttons"]), 2)
        self.assertEqual(self.request("/api/pad?page=15")[0], 404)
        self.assertEqual(self.request("/api/pad?page=00")[1], self.request("/api/pad?page=0")[1])
        for query in ("", "?page=-1", "?page=16", "?page=wrong"):
            self.assertEqual(self.request("/api/pad" + query)[0], 400)

    def test_screensaver_panel_policy_and_save(self):
        for profile, keep_panel in (("esp32-p4-lcd4b", False), ("jc3248w535", True)):
            config = self.request("/api/config", profile=profile)[1]
            self.assertEqual(config["screen_saver_keeps_panel_awake"], keep_panel)
            self.assertFalse(config["screen_saver_backlight_only"])
            self.assertTrue(config["caps"]["touch"])
            nav = self.request("/api/portal/nav", profile=profile)[1]
            self.assertTrue(any(item["id"] == "screensaver" for category in nav["categories"] for item in category["items"]))
            fragment = self.request("/api/section/screensaver", profile=profile, raw=True)[1]
            self.assertIn('id="screen_saver_enabled"', fragment)
            self.assertIn('id="screen_saver_fade_out_ms"', fragment)
            self.assertIn('id="screen_saver_fade_in_ms"', fragment)
            info = self.request("/api/info", profile=profile)[1]
            self.assertEqual(len(info["available_screens"]), 17)
            if profile == "jc3248w535":
                self.assertTrue(config["caps"]["usb_hid"])
                self.assertFalse(config["caps"]["ble_hid"])
                self.assertTrue(info["has_native_extensions"])
                self.assertEqual((info["display_coord_width"], info["display_coord_height"]), (480, 320))
            updates = {"screen_saver_enabled": False, "screen_saver_fade_out_ms": 1500,
                       "screen_saver_fade_in_ms": 750, "idle_screen_pad": "pad_1"}
            self.assertEqual(self.request("/api/config?no_reboot=1", "POST", updates, profile=profile)[0], 200)
            loaded = self.request("/api/config", profile=profile)[1]
            for key, value in updates.items():
                self.assertEqual(loaded[key], value)
            self.assertEqual(loaded["screen_saver_keeps_panel_awake"], keep_panel)

    def test_save_reload_delete_and_reset(self):
        pad = self.request("/api/pad?page=0")[1]
        pad.update(name="Edited pad", future_field="preserved")
        self.assertEqual(self.request("/api/pad?page=0", "POST", pad)[0], 200)
        self.assertEqual(self.request("/api/pad?page=0")[1], pad)
        self.assertEqual(self.request("/api/info")[1]["available_screens"][0]["name"], "Edited pad")
        invalid = copy.deepcopy(pad)
        invalid["buttons"].append(copy.deepcopy(invalid["buttons"][0]))
        self.assertEqual(self.request("/api/pad?page=0", "POST", invalid)[0], 400)
        self.assertEqual(self.request("/api/pad?page=0", "POST", [pad])[0], 400)
        self.assertEqual(self.request("/api/pad?page=0", "DELETE")[0], 200)
        self.assertEqual(self.request("/api/pad?page=0")[0], 404)
        self.assertEqual(self.request("/__mock/reset", "POST", {})[0], 200)
        self.assertEqual(self.request("/api/pad?page=0")[1]["name"], "Living Room")

    def test_binding_preview_shapes(self):
        _, result = self.request("/api/pad/resolve", "POST", {
            "screen": "pad_0", "bindings": ["[pad:temperature]", "[mqtt:unknown]", "literal"],
            "button": {"label_center": "[pad:temperature]"},
        })
        self.assertEqual([value["value"] for value in result["resolved"]], ["21.5 C", "---", "literal"])
        self.assertEqual(result["button"]["label_center"], "21.5 C")
        self.assertEqual(self.request("/api/pad/resolve", "POST", {"button": {"label_center": 5}})[0], 400)

    def test_display_icons_defaults_and_catalog_routes(self):
        self.assertEqual(self.request("/api/component/display/brightness", "PUT", {"brightness": 0})[0], 200)
        self.assertEqual(self.request("/api/config")[1]["backlight_brightness"], 0)
        self.assertEqual(self.request("/api/component/display/brightness", "PUT", {"brightness": 101})[0], 400)
        self.assertEqual(self.request("/api/component/display/screen", "PUT", {"screen": "pad_4"})[0], 200)
        self.assertEqual(self.server.mock_screen, "pad_4")
        self.assertEqual(self.request("/api/component/display/screen", "PUT", {"screen": "missing"})[0], 400)
        self.assertEqual(self.request("/api/icons/install?id=pad_0_0_0", "POST", {})[0], 200)
        self.request("/api/icons/install?id=pad_1_0_0", "POST", {})
        self.assertEqual(self.request("/api/icons/page?page=0", "DELETE")[0], 200)
        self.assertEqual(set(self.server.mock_icons), {"pad_1_0_0"})
        _, sizes = self.request("/api/pad/button_sizes?cols=4&rows=3")
        self.assertTrue(all(sizes[field] > 0 for field in ("button_w", "button_h", "padding", "gap", "font_small_h")))
        self.assertEqual((sizes["button_w"], sizes["button_h"], sizes["gap"], sizes["padding"]), (173, 233, 6, 4))
        self.assertEqual(self.request("/api/pad/button_sizes?cols=0&rows=3")[0], 400)
        self.assertEqual(self.request("/api/component/button-defaults/config", "POST", {"corner_radius": "4"})[0], 200)
        self.assertEqual(self.request("/api/component/button-defaults/config")[1]["corner_radius"], "4")
        for path in ("/api/pad/blocks", "/api/sounds/list", "/api/extensions", "/api/images"):
            self.assertEqual(self.request(path)[0], 200)

    def test_failure_scenarios(self):
        self.request("/__mock/reset", "POST", {"scenario": "load-error"})
        self.assertEqual(self.request("/api/pad?page=0")[0], 503)
        self.request("/__mock/reset", "POST", {"scenario": "save-error"})
        pad = self.request("/api/pad?page=0")[1]
        self.assertEqual(self.request("/api/pad?page=0", "POST", pad)[0], 503)
        self.request("/__mock/reset", "POST", {"scenario": "invalid-bindings"})
        self.assertEqual(self.request("/api/pad?page=0")[1]["buttons"][0]["label_center"], "[pad:]")
        self.assertEqual(self.request("/__mock/reset", "POST", {"scenario": "unknown"})[0], 400)

    def test_catalog_source_parity(self):
        actions_dir = ROOT / "src/app/actions"
        manifest = (actions_dir / "action_modules.inc").read_text()
        unavailable = {"ble_pair", "display_refresh", "camera_capture"}
        expected = set(re.findall(r'#include "([^"/]+)_action.cpp"', manifest)) - unavailable
        catalog = self.server.pad_fixture["catalog"]
        self.assertEqual({action["type"] for action in catalog}, expected)
        for action in catalog:
            source = (actions_dir / (action["type"] + "_action.cpp")).read_text()
            self.assertIn('action["label"] = ' + json.dumps(action["label"]), source)
            self.assertIn('action["group"] = ' + json.dumps(action["group"]), source)
        fields = ("name", "icon", "second_icon", "axis_field", "horizontal_icon", "vertical_icon", "default_axis")
        widgets = {widget["type"]: widget for widget in self.server.pad_fixture["widget_catalog"]}
        for path in (ROOT / "src/app/widgets").glob("*_widget.cpp"):
            match = re.search(r'static const WidgetPreview (\w+)_preview = \{([^}]+)\};', path.read_text())
            if not match or match[1] == "camera_preview":
                continue
            values = json.loads("[" + match[2].replace("nullptr", "null") + "]")
            expected = {"type": match[1]}
            expected.update({field: value for field, value in zip(fields, values) if value is not None})
            self.assertEqual(widgets.pop(match[1]), expected)
        self.assertEqual(widgets, {})

    def test_binding_schema_source_parity(self):
        schema = self.server.pad_fixture["binding_schema"]["schemes"]
        fields = ("min_params", "max_params", "widget_max_params", "format_param", "validation_mode", "free_form")
        for scheme in schema:
            filename = "mqtt_sub_store.cpp" if scheme["name"] == "mqtt" else scheme["name"] + "_binding.cpp"
            source = (ROOT / "src/app" / filename).read_text()
            match = re.search(r'binding_template_register\("' + scheme["name"] + r'"[^{}]+\{([^}]+)\}', source)
            self.assertIsNotNone(match)
            parts = match[1].split(",")[:6]
            parts[4] = "1" if "EXPRESSION" in parts[4] else "0"
            parts[5] = "true" if "true" in parts[5] else "false"
            self.assertEqual([scheme[field] for field in fields], json.loads("[" + ",".join(parts) + "]"))


if __name__ == "__main__":
    unittest.main()