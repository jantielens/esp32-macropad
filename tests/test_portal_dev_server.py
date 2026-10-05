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
        cls.server.mock_config = {
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