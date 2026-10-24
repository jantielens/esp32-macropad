import base64
import copy
import importlib.util
import json
from html.parser import HTMLParser
from pathlib import Path
import re
import socket
import sys
import tempfile
import threading
import unittest
import zlib
from urllib.error import HTTPError
from urllib.request import Request, urlopen
from urllib.parse import urlencode


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
SPEC = importlib.util.spec_from_file_location("portal_dev_server", ROOT / "tools/portal-dev-server.py")
PORTAL = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PORTAL)

from widget_manifest import load_widgets, firmware_includes, portal_assets, validate_editors


class PortalDevServerTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.server = PORTAL.ThreadingHTTPServer(("127.0.0.1", 0), PORTAL.PortalHandler)
        cls.server.profile = "esp32-p4-lcd4b"
        cls.server.logs_started = PORTAL.time.monotonic()
        cls.server.mock_config = {
            **PORTAL.SCREENSAVER_DEFAULTS,
            "caps": {"ble": True, "ble_hid": True, "usb_hid": True, "mqtt": True},
            "mqtt_host": "broker.example.test",
            "ha_url": "http://ha.example.test", "ha_token_set": True,
            "backlight_brightness": 80, "keyboard_transport": "none",
            "keyboard_active_transport": "none", "device_name": "Mock",
            "wifi_failure_mode": "ap", "ap_password": "",
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
        self.server.mock_fonts = {}

    def request(self, path, method="GET", body=None, profile="esp32-p4-lcd4b", raw=False,
                content_type="application/json"):
        headers = {"Cookie": "portal_mock_profile=" + profile, "Content-Type": content_type}
        data = body if isinstance(body, bytes) else json.dumps(body).encode() if body is not None else None
        request = Request(self.base + path, data=data, headers=headers, method=method)
        try:
            response = urlopen(request, timeout=5)
        except HTTPError as error:
            response = error
        with response:
            if response.headers.get_content_type() == "image/png":
                return response.status, response.read()
            text = response.read().decode()
            return response.status, text if raw else json.loads(text)

    def test_production_assets_and_templates(self):
        status, shell = self.request("/", raw=True)
        self.assertEqual(status, 200)
        self.assertNotIn("{{", shell)
        self.assertEqual(shell.count('id="binding-help-overlay"'), 1)
        headless_shell = self.request("/?profile=reterminal-e1003-frame", raw=True)[1]
        self.assertNotIn('id="binding-help-overlay"', headless_shell)
        self.assertNotIn("{{", headless_shell)
        status, fragment = self.request("/api/section/pad-editor", raw=True)
        self.assertEqual(status, 200)
        self.assertNotIn("{{", fragment)
        self.assertIn('id="pad-edit-gauge-min"', fragment)
        self.assertNotIn('id="binding-help-overlay"', fragment)
        status, bundle = self.request("/portal.js", raw=True)
        self.assertEqual(status, 200)
        self.assertIn("async function padInit()", bundle)
        self.assertLess(bundle.index("function actionEditorHTML("), bundle.index("async function padInit()"))
        self.assertNotIn("portal_shutter", bundle)
        self.assertIn("function haBindingBuilderHTML(", bundle)
        for profile, display in (("jc3248w535", True), ("reterminal-e1003-frame", False)):
            other_bundle = self.request("/portal.js", profile=profile, raw=True)[1]
            self.assertEqual("function showBindingHelp(" in other_bundle, display)
            self.assertNotIn("function haBindingBuilderHTML(", other_bundle)
            self.assertEqual("function bindingSourcePickerHTML(" in other_bundle, display)
            self.assertEqual("const mqttBindingProvider =" in other_bundle, display)
            other_styles = self.request("/portal-all.css", profile=profile, raw=True)[1]
            self.assertEqual(".binding-docs-overlay" in other_styles, display)
            self.assertEqual(".binding-builder-step" in other_styles, display)
            self.assertEqual(".pad-button-builder-entry" in other_styles, display)
            self.assertEqual("const catalogButtonBuilderProvider =" in other_bundle, display)
            self.assertEqual("const mqttButtonBuilderProvider =" in other_bundle, display)
            for suffix in ("", "?docs=1"):
                other_schema = self.request("/api/bindings" + suffix, profile=profile)[1]
                self.assertNotIn("ha", {scheme["name"] for scheme in other_schema["schemes"]})
        status, schema = self.request("/api/bindings")
        self.assertEqual(status, 200)
        self.assertIn("ha", {scheme["name"] for scheme in schema["schemes"]})
        self.assertIn("mqtt", {scheme["name"] for scheme in schema["schemes"]})
        docs = self.request("/api/bindings?docs=1")[1]
        mqtt_docs = next(scheme for scheme in docs["schemes"] if scheme["name"] == "mqtt")
        self.assertNotIn("status", mqtt_docs)
        headless_docs = self.request("/api/bindings?docs=1", profile="reterminal-e1003-frame")[1]
        self.assertNotIn("status", next(scheme for scheme in headless_docs["schemes"] if scheme["name"] == "mqtt"))
        status, styles = self.request("/portal-all.css", raw=True)
        self.assertEqual(status, 200)
        headless_styles = self.request("/portal-all.css", profile="reterminal-e1003-frame", raw=True)[1]
        for filename in ("portal_widget_bar_chart.css", "portal_widget_gamepad_button.css"):
            widget_styles = (ROOT / "src/app/web" / filename).read_text().strip()
            self.assertIn(widget_styles, styles)
            self.assertNotIn(widget_styles, headless_styles)

    def test_idle_browser_connection_does_not_block_requests(self):
        with socket.create_connection(("127.0.0.1", self.server.server_port), timeout=5):
            status, config = self.request("/api/config")
            self.assertEqual(status, 200)
            self.assertEqual(config["device_name"], "Mock")

    def test_wifi_setup_config_fixture(self):
        status, config = self.request("/api/config")
        self.assertEqual(status, 200)
        self.assertEqual(config["wifi_failure_mode"], "ap")
        self.assertEqual(config["ap_password"], "")

        status, _ = self.request("/api/config?no_reboot=1", method="POST",
            body={"wifi_failure_mode": "offline", "ap_password": "share-me-123"})
        self.assertEqual(status, 200)
        saved = self.request("/api/config")[1]
        self.assertEqual(saved["wifi_failure_mode"], "offline")
        self.assertEqual(saved["ap_password"], "share-me-123")

        status, _ = self.request("/api/config?no_reboot=1", method="POST",
            body={"wifi_failure_mode": "ap", "ap_password": ""})
        self.assertEqual(status, 200)
        cleared = self.request("/api/config")[1]
        self.assertEqual(cleared["wifi_failure_mode"], "ap")
        self.assertEqual(cleared["ap_password"], "")

    def test_ha_lookup_jobs(self):
        config = self.request("/api/config")[1]
        self.assertTrue(config["caps"]["ha_live"])
        self.assertEqual(config["ha_url"], "http://ha.example.test")
        self.assertTrue(config["ha_token_set"])
        self.assertEqual(config["ha_token"], "")
        self.assertNotIn("capabilities", config)
        headless_config = self.request("/api/config", profile="reterminal-e1003-frame")[1]
        self.assertFalse(headless_config["caps"]["ha_live"])
        status, started = self.request("/api/ha/lookup?query=living")
        self.assertEqual(status, 202)
        status, matches = self.request("/api/ha/lookup?job=" + str(started["job"]))
        self.assertEqual(status, 200)
        self.assertEqual(len(matches["entities"]), 2)
        self.assertFalse(matches["more"])
        started = self.request("/api/ha/lookup?entity=sensor.living_room_temperature")[1]
        temperature = self.request("/api/ha/lookup?job=" + str(started["job"]))[1]
        self.assertEqual(temperature["state"], "21.37")
        self.assertEqual(temperature["unit"], "\u00b0C")
        started = self.request("/api/ha/lookup?entity=light.kitchen")[1]
        detail = self.request("/api/ha/lookup?job=" + str(started["job"]))[1]
        self.assertEqual(detail["state"], "on")
        self.assertIn({"path": "attributes.brightness", "value": "128"}, detail["paths"])
        self.assertEqual(self.request("/api/ha/lookup?job=0")[0], 404)
        started = self.request("/api/ha/lookup?query=nonadmin")[1]
        status, denied = self.request("/api/ha/lookup?job=" + str(started["job"]))
        self.assertEqual(status, 403)
        self.assertIn("administrator", denied["error"])
        self.assertEqual(self.request("/api/ha/lookup?entity=light.kitchen")[0], 202)
        self.assertEqual(self.request("/api/ha/lookup?query=ab")[0], 400)
        self.assertEqual(self.request("/api/ha/lookup?query=living", profile="reterminal-e1003-frame")[0], 404)
        self.server.scenario = "load-error"
        started = self.request("/api/ha/lookup?query=living")[1]
        self.assertEqual(self.request("/api/ha/lookup?job=" + str(started["job"]))[0], 503)

    def test_widget_manifest(self):
        widgets = load_widgets()
        self.assertEqual(portal_assets(widgets, "js").count("portal_widget_inputs.js"), 1)
        waveform = next(widget for widget in widgets if widget["type"] == "waveform")
        self.assertEqual(waveform["portal"]["js"], "portal_widget_waveform.js")
        firmware_only = [{key: value for key, value in waveform.items() if key != "portal"}]
        self.assertEqual(portal_assets(firmware_only, "js"), [])
        validate_editors(widgets)
        with self.assertRaisesRegex(ValueError, "registration mismatch"):
            validate_editors([{**widgets[0], "type": "mismatched_type"}])
        includes = firmware_includes(widgets)
        for kind, gates in {
            "mousepad": ["HAS_DISPLAY", "HAS_TOUCH", "HAS_USB_HID"],
            "camera_preview": ["HAS_DISPLAY", "HAS_CAMERA"],
            "external": ["HAS_DISPLAY", "HAS_NATIVE_EXTENSIONS"],
            "waveform": ["HAS_DISPLAY", "IS_SHUTTER_TESTER"],
        }.items():
            widget = next(widget for widget in widgets if widget["type"] == kind)
            self.assertEqual(widget["gates"], gates)
            self.assertIn(f"#if {' && '.join(gates)}\n#include \"{widget['source']}\"", includes)
        invalid = [
            widgets + [widgets[0]],
            [{**widgets[0], "gates": ["HAS_MISSPELLED_FLAG"]}],
            [{**widgets[0], "source": "widgets/missing_widget.cpp"}],
            [{**widgets[0], "portal": {**widgets[0]["portal"], "html": "_missing.html"}}],
            [{**widgets[0], "portal": {"label": "Missing", "js": "portal_widget_table.js"}}],
        ]
        with tempfile.TemporaryDirectory() as directory:
            manifest = Path(directory) / "manifest.json"
            for entries in invalid:
                with self.subTest(entries=entries):
                    manifest.write_text(json.dumps(entries))
                    with self.assertRaises(ValueError):
                        load_widgets(manifest)

    def test_manifest_selector_and_partials(self):
        class WidgetParser(HTMLParser):
            def __init__(self):
                super().__init__()
                self.ids = []
                self.types = []
                self.in_selector = False

            def handle_starttag(self, tag, attrs):
                attrs = dict(attrs)
                if "id" in attrs:
                    self.ids.append(attrs["id"])
                if tag == "select":
                    self.in_selector = attrs.get("id") == "pad-edit-widget-type"
                if tag == "option" and self.in_selector and attrs.get("value"):
                    self.types.append(attrs["value"])

            def handle_endtag(self, tag):
                if tag == "select":
                    self.in_selector = False

        parser = WidgetParser()
        parser.feed(self.request("/api/section/pad-editor", raw=True)[1])
        widgets = [widget for widget in load_widgets() if "portal" in widget]
        self.assertEqual(parser.types, [widget["type"] for widget in widgets])
        self.assertEqual(len(parser.ids), len(set(parser.ids)), "Duplicate mounted control IDs")
        for widget in widgets:
            partial = WidgetParser()
            partial.feed((ROOT / "src/app/web" / widget["portal"]["html"]).read_text())
            for identifier in partial.ids:
                self.assertIn(identifier, parser.ids)

    def test_audio_amplifier_config(self):
        previous = copy.deepcopy(self.server.mock_config)
        try:
            config = self.request("/api/config")[1]
            self.assertTrue(config["caps"]["audio_amplifier_mute"])
            self.assertTrue(config["audio_idle_mute"])
            self.assertEqual(config["audio_wake_delay_ms"], 120)
            self.assertTrue(self.request("/api/info")[1]["has_audio_amplifier_mute"])
            self.assertEqual(self.request("/api/config?no_reboot=1", method="POST",
                body={"audio_idle_mute": False, "audio_wake_delay_ms": "35"})[0], 200)
            saved = self.request("/api/config")[1]
            self.assertFalse(saved["audio_idle_mute"])
            self.assertEqual(int(saved["audio_wake_delay_ms"]), 35)
            for profile in ("jc3248w535", "crowpanel-4.2-interactive"):
                unsupported = self.request("/api/config", profile=profile)[1]
                self.assertFalse(unsupported["caps"]["audio_amplifier_mute"])
                self.assertNotIn("audio_idle_mute", unsupported)
                self.assertFalse(self.request("/api/info", profile=profile)[1]["has_audio_amplifier_mute"])
        finally:
            self.server.mock_config = previous

    def test_epaper_presentation_config(self):
        profile = "crowpanel-4.2-interactive"
        status, config = self.request("/api/config", profile=profile)
        self.assertEqual(status, 200)
        self.assertEqual(config["epaper_bw_conversion"], "threshold")
        self.assertEqual(config["epaper_bw_threshold"], 128)
        self.assertEqual(config["epaper_vcom_mv"], 0)
        self.assertEqual(config["epaper_default_vcom_mv"], 0)
        self.assertEqual(config["epaper_supported_render_modes"], ["bw"])
        nav = self.request("/api/portal/nav", profile=profile)[1]
        items = [item for category in nav["categories"] for item in category["items"]]
        presentation = next(item for item in items if item["id"] == "epaper-refresh")
        self.assertEqual(self.request(presentation["portal_script"], profile=profile, raw=True)[0], 200)
        self.assertEqual(self.request(presentation["portal_script"], raw=True)[0], 404)
        previous = copy.deepcopy(self.server.mock_config)
        try:
            self.assertEqual(self.request("/api/config?no_reboot=1", method="POST",
                body={"epaper_bw_conversion": "blue_noise", "epaper_bw_threshold": "96",
                      "epaper_vcom_mv": "1480"}, profile=profile)[0], 200)
            saved = self.request("/api/config", profile=profile)[1]
            self.assertEqual(saved["epaper_bw_conversion"], "blue_noise")
            self.assertEqual(int(saved["epaper_bw_threshold"]), 96)
            self.assertEqual(int(saved["epaper_vcom_mv"]), 1480)
            lcd = self.request("/api/config")[1]
            self.assertNotIn("epaper_bw_conversion", lcd)
            self.assertNotIn("epaper_bw_threshold", lcd)
            self.assertNotIn("epaper_vcom_mv", lcd)
            self.assertNotIn("epaper_default_vcom_mv", lcd)
        finally:
            self.server.mock_config = previous

    def test_fragment_presentation(self):
        class FragmentParser(HTMLParser):
            def __init__(self):
                super().__init__()
                self.nodes = []
                self.stack = []

            def handle_starttag(self, tag, attrs):
                attributes = dict(attrs)
                node = {"tag": tag, "attrs": attributes,
                        "classes": set(attributes.get("class", "").split()),
                        "parent": self.stack[-1] if self.stack else None}
                self.nodes.append(node)
                if tag not in {"area", "base", "br", "col", "embed", "hr", "img",
                               "input", "link", "meta", "param", "source", "track", "wbr"}:
                    self.stack.append(node)

            def handle_endtag(self, tag):
                for index in range(len(self.stack) - 1, -1, -1):
                    if self.stack[index]["tag"] == tag:
                        del self.stack[index:]
                        break

        def descendants(nodes, parent):
            for node in nodes:
                ancestor = node["parent"]
                while ancestor is not None:
                    if ancestor is parent:
                        yield node
                        break
                    ancestor = ancestor["parent"]

        fragments = list((ROOT / "src/app/web").glob("*.fragment.html"))
        fragments += list((ROOT / "src/app/device_classes").glob("*/web/*.fragment.html"))
        for fragment in fragments:
            with self.subTest(fragment=fragment.name):
                parser = FragmentParser()
                parser.feed(fragment.read_text())
                headers = [node for node in parser.nodes if "section-header" in node["classes"]]
                self.assertTrue(headers)
                for header in headers:
                    headings = [node for node in descendants(parser.nodes, header) if node["tag"] == "h2"]
                    self.assertTrue(headings)
                    for heading in headings:
                        icons = [node for node in descendants(parser.nodes, heading)
                                 if "portal-icon" in node["classes"]]
                        self.assertEqual(len(icons), 1)
                        self.assertEqual(icons[0]["attrs"].get("aria-hidden"), "true")
                panels = [node for node in parser.nodes
                          if node["classes"] & {"card", "portal-section", "pad-workspace"}]
                self.assertTrue(panels, "Every fragment needs a content section")
                if fragment.name == "pad-editor.fragment.html":
                    continue
                for panel in panels:
                    siblings = [other for other in panels if other["parent"] is panel["parent"]]
                    children = list(descendants(parser.nodes, panel))
                    if len(siblings) < 2 or not children:
                        continue
                    self.assertTrue(any(node["tag"] in {"h2", "h3", "h4", "h5", "h6", "summary"}
                                        or "card-header" in node["classes"] for node in children),
                                    "Sibling sections need a title")

    def test_recipe_catalog(self):
        status, catalog = self.request("/api/recipes/catalog")
        self.assertEqual(status, 200)
        alarm = next(recipe for recipe in catalog["recipes"] if recipe["id"] == "alarm-clock")
        self.assertFalse(alarm["provision"]["components"]["alarms"]["1"]["enabled"])
        self.assertEqual(alarm["post_install"]["links"][0]["fragment"], "alarm-schedule")
        info = self.request("/api/info")[1]
        self.assertTrue(info["has_alarm"])
        self.assertTrue(info["has_sound_player"])
        self.assertTrue(info["has_ttf_fonts"])
        self.assertFalse(self.request("/api/info", profile="reterminal-e1003-frame")[1]["has_ttf_fonts"])
        self.assertFalse(self.request("/api/info", profile="jc3248w535")[1]["has_sound_player"])
        self.assertFalse(self.request("/api/info", profile="reterminal-e1003-frame")[1]["has_alarm"])
        nav = self.request("/api/portal/nav")[1]
        pads = next(category for category in nav["categories"] if category["id"] == "pads")
        self.assertIn("recipes", [item["id"] for item in pads["items"]])
        self.assertEqual(self.request("/api/section/recipes", raw=True)[0], 200)
        self.assertEqual(self.request("/api/recipes/catalog", "POST", {"schema": 2})[0], 400)
        self.assertEqual(self.request("/api/recipes/catalog")[1], catalog)
        empty = {"schema": 1, "catalog_version": "test", "recipes": []}
        self.assertEqual(self.request("/api/recipes/catalog", "POST", empty)[0], 200)
        self.assertEqual(self.request("/api/recipes/catalog")[1], empty)
        self.assertEqual(self.request("/api/recipes/catalog", "POST", {**empty, "padding": "x" * 65536})[0], 413)

    def test_font_storage_workflow(self):
        source = (ROOT / "tests/fixtures/BebasNeue.inc").read_text()
        font = bytes(int(value, 16) for value in re.findall(r"0x([0-9a-fA-F]{2})", source))
        self.assertEqual(self.request("/api/fonts/list"), (200, []))
        self.assertEqual(self.request("/api/fonts/upload?name=test-font", "POST", font)[0], 200)
        self.assertEqual(self.request("/api/fonts/list")[1], [{"name": "test-font", "size_bytes": len(font)}])
        self.assertEqual(self.request("/api/fonts/upload?name=test-font", "POST", font)[0], 409)
        self.assertEqual(self.request("/api/fonts/upload?name=Bad.ttf", "POST", font)[0], 400)
        self.assertEqual(self.request("/api/fonts/upload?name=invalid", "POST", b"OTTO")[0], 400)
        self.assertEqual(self.request("/api/fonts/list", profile="reterminal-e1003-frame")[0], 404)
        self.assertEqual(self.request("/api/fonts?name=test-font", "DELETE")[0], 200)
        self.assertEqual(self.request("/api/fonts?name=test-font", "DELETE")[0], 404)
        self.assertEqual(self.request("/api/fonts/list"), (200, []))

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
        alarm["1"].update(enabled=True, hour=8, on_ring=[{"type": "alarm_tone"}])
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
        for fragment_id in ("alarm-schedule", "alarm-behavior"):
            self.assertEqual(self.request("/api/section/" + fragment_id, profile="reterminal-e1003-frame", raw=True)[0], 404)
        self.assertEqual(self.request("/portal_alarms.js", profile="reterminal-e1003-frame", raw=True)[0], 404)
        self.assertIn("init_alarm_schedule_fragment", self.request("/portal_alarms.js", raw=True)[1])
        self.assertIn("init_alarm_behavior_fragment", self.request("/portal_alarms.js", raw=True)[1])
        self.assertIn('server->on("/portal_alarms.js", HTTP_GET, handlePortalAlarmsJS)',
                  (ROOT / "src/app/web_portal_routes.cpp").read_text())
        bundle = self.request("/portal.js", raw=True)[1]
        self.assertIn("if (!navigationReady) return;", bundle)
        self.assertNotIn("loadNavigationAssets(data)", bundle)
        self.assertLess(bundle.index("await loadItemAssets("),
            bundle.index("var initFn = window["))
        fragment = self.request("/api/section/alarm-schedule", raw=True)[1]
        self.assertNotIn('id="alarm-snooze"', fragment)
        self.assertNotIn('id="alarm-cancel"', fragment)
        self.assertNotIn('id="alarm-state"', fragment)
        self.assertIn('id="alarm-repeat-summary"', fragment)
        self.assertIn('id="alarm-once-target"', fragment)
        self.assertNotIn('id="alarm-lateness-minutes"', fragment)
        self.assertNotIn('id="alarm-snooze-minutes"', fragment)
        behavior = self.request("/api/section/alarm-behavior", raw=True)[1]
        self.assertIn('id="alarm-lateness-minutes"', behavior)
        self.assertNotIn('id="alarm-time"', behavior)
        self.assertNotIn("setTimeout", self.request("/portal_alarms.js", raw=True)[1])

    def test_alarm_sections(self):
        categories = self.request("/api/portal/nav")[1]["categories"]
        category_ids = [category["id"] for category in categories]
        self.assertEqual(category_ids[category_ids.index("actions") + 1], "alarm")
        alarm_category = next(category for category in categories if category["id"] == "alarm")
        self.assertEqual([item["display_name"] for item in alarm_category["items"]], ["Schedule", "Behavior"])
        config = self.request("/api/component/alarms/config")[1]
        schedule = {"enabled": True, "hour": 10, "minute": 20, "weekdays": 127}
        self.assertEqual(self.request("/api/component/alarms/schedule", "POST", schedule)[0], 200)
        current = self.request("/api/component/alarms/config")[1]
        for key in ("snooze_minutes", "auto_dismiss_minutes", "on_ring", "on_stop"):
            self.assertEqual(current["1"][key], config["1"][key])
        self.assertEqual(current["lateness_minutes"], config["lateness_minutes"])
        behavior = {"lateness_minutes": 12, "snooze_minutes": 4, "auto_dismiss_minutes": 15,
                    "on_ring": [{"type": "alarm_tone"}], "on_stop": []}
        self.assertEqual(self.request("/api/component/alarms/behavior", "POST", behavior)[0], 200)
        current = self.request("/api/component/alarms/config")[1]
        for key, value in schedule.items():
            self.assertEqual(current["1"][key], value)
        self.assertEqual(current["lateness_minutes"], 12)
        self.assertEqual(current["1"]["snooze_minutes"], 4)
        self.assertEqual(self.request("/api/component/alarms/schedule", "POST", {**schedule, "on_ring": []})[0], 400)
        self.assertEqual(self.request("/api/component/alarms/behavior", "POST", {**behavior, "enabled": False})[0], 400)

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

    def test_boot_settings_and_logo_fixtures(self):
        previous = copy.deepcopy(self.server.mock_config)
        try:
            config = self.request("/api/config")[1]
            self.assertTrue(config["caps"]["boot_appearance"])
            self.assertTrue(config["caps"]["boot_logo"])
            self.assertEqual(config["boot_appearance_font_size"], 0)
            self.assertEqual(config["boot_logo_slot"], 255)
            fragment = self.request("/api/section/boot-actions", raw=True)[1]
            self.assertNotIn('id="boot-appearance-settings"', fragment)
            self.assertIn('onclick="saveBootActions()"', fragment)
            fragment = self.request("/api/section/system-screens", raw=True)[1]
            self.assertIn('id="boot-appearance-settings"', fragment)
            self.assertIn('id="boot-logo-settings"', fragment)
            self.assertIn('onclick="saveSystemScreenAppearance()"', fragment)
            self.assertNotIn('id="boot-action-editors"', fragment)
            self.assertIn('Reset to device default', fragment)
            self.assertIn('onclick="deleteBootLogo()"', fragment)
            self.assertIn('baseline (non-progressive) JPEG', fragment)

            self.assertEqual(self.request("/api/component/boot-actions/config")[1], {"actions": []})
            actions = [{"type": "notify", "message": "Local mock"}]
            self.assertEqual(self.request("/api/component/boot-actions/config", "POST",
                                          {"actions": actions})[0], 200)
            self.assertEqual(self.request("/api/component/boot-actions/config")[1], {"actions": actions})

            png = base64.b64decode(
                "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+/p2cAAAAASUVORK5CYII=")
            status, uploaded = self.request("/api/boot-logo/upload", "POST", png)
            self.assertEqual(status, 200)
            self.assertEqual(uploaded, {"success": True, "slot": 0})
            logo_status = self.request("/api/boot-logo")[1]
            self.assertEqual(logo_status["active_slot"], 255)
            self.assertTrue(logo_status["slot0_available"])
            with urlopen(self.base + "/api/boot-logo/thumbnail?slot=0", timeout=5) as response:
                self.assertEqual(response.headers["Content-Type"], "image/png")
                self.assertEqual(response.read(), png)

            self.assertEqual(self.request("/api/config?no_reboot=1", "POST",
                                          {"boot_logo_slot": 0})[0], 200)
            self.assertEqual(self.request("/api/boot-logo")[1]["active_slot"], 0)
            status, second_upload = self.request("/api/boot-logo/upload", "POST", png)
            self.assertEqual(status, 200)
            self.assertEqual(second_upload, {"success": True, "slot": 1})
            invalid_status, invalid_upload = self.request("/api/boot-logo/upload", "POST", b"not an image")
            self.assertEqual(invalid_status, 400)
            self.assertIn("baseline (non-progressive) JPEG", invalid_upload["message"])
            self.assertEqual(self.request("/api/boot-logo", "DELETE")[0], 409)
            self.assertEqual(self.request("/api/config?no_reboot=1", "POST",
                                          {"boot_logo_slot": 255})[0], 200)
            self.assertEqual(self.request("/api/boot-logo", "DELETE"), (200, {"success": True}))
            deleted_status = self.request("/api/boot-logo")[1]
            self.assertEqual(deleted_status["active_slot"], 255)
            self.assertFalse(deleted_status["slot0_available"])
            self.assertFalse(deleted_status["slot1_available"])
            self.assertEqual(self.request("/api/boot-logo/thumbnail?slot=0")[0], 404)
            headless = self.request("/api/config", profile="reterminal-e1003-frame")[1]
            self.assertFalse(headless["caps"]["boot_appearance"])
            self.assertFalse(headless["caps"]["boot_logo"])
            self.assertEqual(self.request("/api/boot-logo", profile="reterminal-e1003-frame")[0], 404)
        finally:
            self.server.mock_config = previous

    def test_layouts_and_template(self):
        for pad in self.server.mock_pads.values():
            self.assertTrue(PORTAL.PortalHandler._valid_pad(pad))
        self.assertEqual(len(self.server.mock_pads["5"]["buttons"]), 64)
        self.assertEqual(self.server.mock_pads["1"]["buttons"], [])
        self.assertEqual(self.request("/api/pad?page=4")[1]["template_pad"], 3)
        self.assertEqual(len(self.request("/api/pad?page=3")[1]["buttons"]), 3)
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
        self.assertIn(0, self.request("/api/info")[1]["configured_pads"])
        invalid = copy.deepcopy(pad)
        invalid["buttons"].append(copy.deepcopy(invalid["buttons"][0]))
        self.assertEqual(self.request("/api/pad?page=0", "POST", invalid)[0], 400)
        self.assertEqual(self.request("/api/pad?page=0", "POST", [pad])[0], 400)
        self.assertEqual(self.request("/api/pad?page=0", "DELETE")[0], 200)
        self.assertNotIn(0, self.request("/api/info")[1]["configured_pads"])
        self.assertEqual(self.request("/api/pad?page=0")[0], 404)
        self.assertEqual(self.request("/__mock/reset", "POST", {})[0], 200)
        self.assertEqual(self.request("/api/pad?page=0")[1]["name"], "Living Room")

    def test_icon_dependency_context(self):
        self.server.mock_pads = {
            "0": {"cols": 2, "rows": 2, "buttons": [{"col": 0, "row": 0}]},
            "1": {"template_pad": 0, "cols": 3, "rows": 2,
                  "column_widths_px": [40, 0, 0], "row_heights_px": [0, 60],
                  "buttons": [{"col": 1, "row": 0, "col_span": 2, "row_span": 1,
                               "label_top": "Private", "bg_image_password": "Secret",
                               "actions": [{"type": "reboot"}]}],
                  "overlay_buttons": [{"col": 0, "row": 0}]},
            "2": {"cols": 1, "rows": 1, "buttons": []},
        }
        status, context = self.request("/api/pad/icon_context?page=0")
        self.assertEqual(status, 200)
        self.assertEqual(context, {"dependents": [{
            "page": 1, "cols": 3, "rows": 2, "button_count": 2,
            "column_widths_px": [40, 0, 0], "row_heights_px": [0, 60],
            "buttons": [{"col": 1, "row": 0, "col_span": 2, "row_span": 1}],
        }]})
        self.assertEqual(self.request("/api/pad/icon_context?page=15")[1], {"dependents": []})
        self.server.mock_pads["1"]["template_pad"] = 2
        self.assertEqual(self.request("/api/pad/icon_context?page=0")[1], {"dependents": []})
        self.assertEqual(self.request("/api/pad/icon_context?page=2")[1]["dependents"][0]["page"], 1)
        for query in ("", "?page=-1", "?page=16", "?page=invalid"):
            self.assertEqual(self.request("/api/pad/icon_context" + query)[0], 400)

    def test_binding_preview_shapes(self):
        _, result = self.request("/api/pad/resolve", "POST", {
            "screen": "pad_0", "bindings": ["[pad:temperature]", "[mqtt:unknown]", "literal"],
            "button": {"label_center": "[pad:temperature]"},
        })
        self.assertEqual([value["value"] for value in result["resolved"]], ["21.5 C", "---", "literal"])
        self.assertEqual(result["button"]["label_center"], "21.5 C")
        self.assertEqual(self.request("/api/pad/resolve", "POST", {"button": {"label_center": 5}})[0], 400)
        _, local = self.request("/api/pad/resolve", "POST", {
            "bindings": ["[health:cpu]", "[health:rssi]", "[health:wifi_ssid]"],
        }, profile="jc3248w535")
        self.assertEqual([value["value"] for value in local["resolved"]], ["23", "-55", "?"])

    def test_mqtt_inspection_fixtures_and_limits(self):
        status, waiting = self.request("/api/mqtt/inspect", "POST", {"topic": "mock/waiting"})
        self.assertEqual(status, 202)
        self.assertEqual(waiting["status"], "queued")
        status, result = self.request("/api/mqtt/inspect?handle=" + str(waiting["handle"]))
        self.assertEqual(status, 200)
        self.assertEqual(result["status"], "waiting")
        self.assertFalse(result["received"])

        for name, sample in (("empty", ""), ("zero", "0"), ("placeholder", "---"), ("error", "ERR:mock")):
            status, queued = self.request("/api/mqtt/inspect", "POST", {"topic": "mock/" + name})
            self.assertEqual(status, 202)
            status, result = self.request("/api/mqtt/inspect?handle=" + str(queued["handle"]))
            self.assertEqual(status, 200)
            self.assertTrue(result["received"])
            self.assertEqual(result["sample"], sample)

        _, queued = self.request("/api/mqtt/inspect", "POST", {"topic": "mock/received"})
        received = self.request("/api/mqtt/inspect?handle=" + str(queued["handle"]))[1]
        self.assertEqual(received["status"], "received")
        self.assertIn('"temperature":21.37', received["sample"])

        _, queued = self.request("/api/mqtt/inspect", "POST", {"topic": "mock/malformed"})
        malformed = self.request("/api/mqtt/inspect?handle=" + str(queued["handle"]))[1]
        self.assertEqual(malformed["sample"], '{"temperature":')

        _, queued = self.request("/api/mqtt/inspect", "POST", {"topic": "mock/truncated"})
        truncated = self.request("/api/mqtt/inspect?handle=" + str(queued["handle"]))[1]
        self.assertTrue(truncated["truncated"])
        self.assertEqual(len(truncated["sample"]), 2047)

        _, queued = self.request("/api/mqtt/inspect", "POST", {"topic": "mock/failed"})
        failed = self.request("/api/mqtt/inspect?handle=" + str(queued["handle"]))[1]
        self.assertEqual((failed["status"], failed["error"]), ("failed", "subscribe_failed"))
        _, queued = self.request("/api/mqtt/inspect", "POST", {"topic": "mock/timeout"})
        timeout = self.request("/api/mqtt/inspect?handle=" + str(queued["handle"]))[1]
        self.assertEqual((timeout["status"], timeout["error"]), ("failed", "timeout"))

        _, expired = self.request("/api/mqtt/inspect", "POST", {"topic": "mock/expired"})
        self.assertEqual(self.request("/api/mqtt/inspect?handle=" + str(expired["handle"]))[0], 410)
        self.assertEqual(self.request("/api/mqtt/inspect?handle=987654")[0], 404)
        _, releasable = self.request("/api/mqtt/inspect", "POST", {"topic": "mock/received"})
        release_path = "/api/mqtt/inspect?handle=" + str(releasable["handle"])
        self.assertEqual(self.request(release_path, "DELETE")[0], 202)
        released = self.request(release_path)[1]
        self.assertEqual(released["status"], "released")
        self.assertFalse(released["received"])
        self.assertEqual(self.request(release_path, "DELETE")[0], 202)
        self.assertEqual(self.request("/api/mqtt/inspect?handle=987654", "DELETE")[0], 404)
        self.assertEqual(self.request("/api/mqtt/inspect", "POST", {"topic": "mock/disconnected"})[0], 503)
        self.assertEqual(self.request("/api/mqtt/inspect", "POST", {"topic": "mock/capacity"})[0], 429)
        self.assertEqual(self.request("/api/mqtt/inspect", "POST", {"topic": "sensor/#"})[0], 400)
        self.assertEqual(self.request("/api/mqtt/inspect", "POST", {"topic": "bad\ud800"})[0], 400)
        self.assertEqual(self.request("/api/mqtt/inspect", "POST", {"topic": "x" * 128})[0], 400)
        self.assertEqual(self.request("/api/mqtt/inspect", "POST", {"topic": "x" * 800})[0], 400)
        self.assertEqual(self.request("/api/mqtt/inspect", "POST", b"{")[0], 400)
        self.assertEqual(self.request("/api/mqtt/inspect", "POST",
                                      b'{"topic":"' + b"x" * 820 + b'"}')[0], 400)
        self.assertEqual(self.request("/api/mqtt/inspect", "POST",
                                      {"topic": "mock/received"},
                                      profile="reterminal-e1003-frame")[0], 202)

        previous = self.server.mock_config["mqtt_host"]
        try:
            self.server.mock_config["mqtt_host"] = ""
            self.assertEqual(self.request("/api/mqtt/inspect", "POST",
                                          {"topic": "mock/received"})[0], 503)
        finally:
            self.server.mock_config["mqtt_host"] = previous

    def test_display_icons_defaults_and_catalog_routes(self):
        self.assertEqual(self.request("/api/component/display/brightness", "PUT", {"brightness": 0})[0], 200)
        self.assertEqual(self.request("/api/config")[1]["backlight_brightness"], 0)
        self.assertEqual(self.request("/api/component/display/brightness", "PUT", {"brightness": 101})[0], 400)
        self.assertEqual(self.request("/api/component/display/screen", "PUT", {"screen": "pad_4"})[0], 200)
        self.assertEqual(self.server.mock_screen, "pad_4")
        self.assertEqual(self.request("/api/component/display/screen", "PUT", {"screen": "missing"})[0], 400)
        source = {"catalog_id": "material-symbols:home", "kind": "mono",
              "media_type": "image/svg+xml", "data": "<svg></svg>"}
        self.assertEqual(self.request("/api/icons/source", "POST", {**source, "kind": "color"})[0], 400)
        self.assertEqual(self.request("/api/icons/source", "POST",
                          {**source, "data": "x" * (64 * 1024)})[0], 413)
        self.assertEqual(self.request("/api/icons/source", "POST", source)[0], 201)
        source_id = self.request("/api/icons/installed")[1]
        self.assertEqual(source_id["icons"], [])
        uploaded = self.request("/api/icons/source?id=s_000000000000000000000001")[1]
        self.assertEqual(uploaded, source)
        self.assertEqual(self.request("/api/icons/source?id=bad")[0], 400)
        self.assertEqual(self.request("/api/icons/source?id=s_000000000000000000000002")[0], 404)
        png = base64.b64decode("iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+aD1sAAAAASUVORK5CYII=")
        install_url = "/api/icons/install?id=pad_0_main_0_0&source_id=s_000000000000000000000001&kind=1"
        self.assertEqual(self.request(install_url, "POST", png)[0], 400)
        for render_key in ("00000000", "not-hex"):
            self.assertEqual(self.request(install_url + "&render_key=" + render_key, "POST", png)[0], 400)
        install_url += "&render_key=4c31e060"
        self.assertEqual(self.request(install_url, "POST", png)[0], 200)
        second_source = {"catalog_id": "noto:smiling-face", "kind": "color",
                 "media_type": "image/svg+xml", "data": "<svg></svg>"}
        self.assertEqual(self.request("/api/icons/source", "POST", second_source)[0], 201)
        second_install = "/api/icons/install?id=pad_1_main_0_0&source_id=s_000000000000000000000002&kind=0&render_key=12345678"
        self.assertEqual(self.request(second_install, "POST", png)[0], 200)
        info = self.request("/api/info")[1]
        self.assertEqual(info["icon_max_png_size"], 512 * 1024)
        self.assertEqual(info["icon_max_source_json_size"], 704 * 1024)
        chunk = b"tEXt" + b"padding\x00" + b"x" * (100 * 1024)
        chunk = (len(chunk) - 4).to_bytes(4, "big") + chunk + zlib.crc32(chunk).to_bytes(4, "big")
        local_png = png[:-12] + chunk + png[-12:]
        self.assertGreater(len(local_png), 65535)
        self.assertEqual(self.request("/api/icons/source", "POST", local_png, content_type="image/png")[0], 201)
        self.assertEqual(self.request("/api/icons/source?id=s_000000000000000000000003")[1], local_png)
        invalid_png = bytearray(local_png)
        invalid_png[16:20] = (721).to_bytes(4, "big")
        self.assertEqual(self.request("/api/icons/source", "POST", bytes(invalid_png), content_type="image/png")[0], 400)
        self.assertEqual(self.request("/api/icons/source", "POST", b"\x00" * (512 * 1024 + 1), content_type="image/png")[0], 413)
        self.assertEqual(self.request("/api/icons/source", "POST", local_png[:-1], content_type="image/png")[0], 400)
        self.assertEqual(self.request("/api/icons/source", "POST", b"not png", content_type="image/jpeg")[0], 415)
        self.assertEqual(self.request("/api/icons/source", "POST", {
            "kind": "color", "media_type": "image/png", "data": base64.b64encode(png).decode("ascii")})[0], 400)
        self.assertEqual(len(self.server.mock_icon_sources), 3)
        self.assertEqual(self.request("/api/icons/installed")[1]["icons"],
                 ["pad_0_main_0_0", "pad_1_main_0_0"])
        metadata = self.request("/api/icons/installed?pages=0")[1]["artifacts"]
        self.assertEqual(metadata, {"pad_0_main_0_0": {
            "source_id": "s_000000000000000000000001", "kind": 1,
            "width": 1, "height": 1, "render_key": "4c31e060",
        }})
        self.assertEqual(len(self.request("/api/icons/installed?pages=0,1")[1]["artifacts"]), 2)
        self.assertEqual(self.request("/api/icons/installed?pages=15")[1]["artifacts"], {})
        for pages in ("", "16", "-1", "0,,1"):
            self.assertEqual(self.request("/api/icons/installed?pages=" + pages)[0], 400)
        self.server.mock_icons["pad_0_main_0_0"] = png[:8]
        self.assertEqual(self.request("/api/icons/installed?pages=0")[1]["artifacts"], {})
        self.server.mock_icons["pad_0_main_0_0"] = png
        self.assertEqual(self.request("/api/icons/file?name=../unsafe.png", "DELETE")[0], 400)
        self.assertEqual(self.request("/api/icons/file?name=pad_0_main_0_0.png", "DELETE")[0], 200)
        self.assertEqual(self.request("/api/icons/installed?pages=0")[1]["artifacts"], {})
        self.assertEqual(self.request("/api/icons/page?page=0", "DELETE")[0], 200)
        self.assertEqual(set(self.server.mock_icons), {"pad_1_main_0_0"})
        self.assertEqual(set(self.server.mock_icon_metadata), {"pad_1_main_0_0"})
        self.assertEqual(self.request("/api/icons/page?page=1", "DELETE")[0], 200)
        self.assertEqual(self.request("/api/icons/installed?pages=1")[1]["artifacts"], {})
        _, sizes = self.request("/api/pad/button_sizes?cols=4&rows=3")
        self.assertTrue(all(sizes[field] > 0 for field in ("button_w", "button_h", "padding", "gap", "font_small_h")))
        self.assertEqual((sizes["button_w"], sizes["button_h"], sizes["gap"], sizes["padding"]), (173, 233, 6, 4))
        self.assertEqual(self.request("/api/pad/button_sizes?cols=0&rows=3")[0], 400)
        self.assertEqual(self.request("/api/component/button-defaults/config", "POST", {"corner_radius": "4"})[0], 200)
        self.assertEqual(self.request("/api/component/button-defaults/config")[1]["corner_radius"], "4")
        defaults = self.request("/api/component/button-defaults/config")[1]
        self.assertEqual(defaults["firmware_defaults"]["bg_color"], "#333333")
        self.assertEqual(defaults["firmware_defaults"]["pixel_shift_distance_px"], 4)
        self.assertNotIn("bg_color", defaults)
        for path in ("/api/pad/blocks", "/api/sounds/list", "/api/extensions", "/api/images"):
            self.assertEqual(self.request(path)[0], 200)

    def test_dialog_appearance_test_is_not_saved(self):
        before = self.request("/api/config")[1]
        for kind in ("notification", "confirmation"):
            payload = {"type": kind, "appearance": {
                "background_opacity": 0, "background_pattern": "diagonal",
                "background_pattern_color": "#226D78", "pattern_spacing": 12,
                "pattern_stroke_width": 2}}
            self.assertEqual(self.request("/api/component/dialog-appearance/test", "POST", payload)[0], 200)
            self.assertEqual(self.server.mock_dialog_test, payload)
            self.assertEqual(self.request("/api/config")[1], before)
        self.assertEqual(self.request("/api/component/dialog-appearance/test", "POST", {"type": "unknown"})[0], 400)

    def test_epaper_appearance_defaults(self):
        profile = "crowpanel-4.2-interactive"
        self.assertEqual(self.request("/api/component/button-defaults/config", "POST", {}, profile=profile)[0], 200)
        defaults = self.request("/api/component/button-defaults/config", profile=profile)[1]
        self.assertEqual(set(defaults), {"firmware_defaults", "typography_status"})
        self.assertEqual(defaults["firmware_defaults"]["bg_color"], "#FFFFFF")
        self.assertEqual(defaults["firmware_defaults"]["fg_color"], "#000000")
        self.assertEqual(defaults["firmware_defaults"]["pixel_shift_distance_px"], 0)
        self.assertEqual(self.request("/api/pad/button_sizes?cols=4&rows=3", profile=profile)[1]["pixel_shift_margin"], 0)
        bundle = self.request("/portal.js", profile=profile, raw=True)[1]
        self.assertIn("function padLoadButtonDefaults(defs)", bundle)
        self.assertEqual(self.request("/api/info", profile=profile)[1]["display_coord_width"], 400)

    def test_typography_defaults(self):
        path = "/api/component/button-defaults/config"
        settings = {"default_font": "missing-font", "base_text_size": 60}
        self.assertEqual(self.request(path, "POST", settings)[0], 200)
        saved = self.request(path)[1]
        self.assertEqual(saved["default_font"], "missing-font")
        self.assertEqual(saved["base_text_size"], 60)
        self.assertTrue(saved["typography_status"]["reboot_required"])
        self.assertEqual(saved["typography_status"]["active_font"], "@builtin")
        self.assertEqual(self.request("/api/reboot", "POST", {})[0], 200)
        active = self.request(path)[1]["typography_status"]
        self.assertEqual(active["active_font"], "missing-font")
        self.assertEqual(active["active_base_text_size"], 60)
        self.assertFalse(active["reboot_required"])
        for settings in ({"base_text_size": -1}, {"base_text_size": 65536}, {"base_text_size": 1.5},
                         {"base_text_size": True}, {"default_font": "../font"}):
            self.assertEqual(self.request(path, "POST", settings)[0], 400)
        self.assertEqual(self.request(path)[1]["default_font"], "missing-font")

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
            if action["type"] in {"notify", "confirm"}:
                source += (actions_dir / "dialog_action.h").read_text()
                self.assertIn(json.dumps(action["label"]), source)
            else:
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
            source = (ROOT / "src/app" / (scheme["name"] + "_binding.cpp")).read_text()
            match = re.search(r'binding_template_register\("' + scheme["name"] + r'"[^{}]+\{([^}]+)\}', source)
            self.assertIsNotNone(match)
            parts = match[1].split(",")[:6]
            parts[4] = "1" if "EXPRESSION" in parts[4] else "0"
            parts[5] = "true" if "true" in parts[5] else "false"
            self.assertEqual([scheme[field] for field in fields], json.loads("[" + ",".join(parts) + "]"))


if __name__ == "__main__":
    unittest.main()