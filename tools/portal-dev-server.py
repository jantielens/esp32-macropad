#!/usr/bin/env python3
"""Local, device-free development server for the web portal.

Serves the production portal shell, styles, JavaScript sources, and selected
fragments from src/app, while answering the browser's device API requests
with deterministic in-memory fixtures. Prototype fragments remain available
as a fallback for quick layout work.

Usage:
    python3 tools/portal-dev-server.py [--port PORT]
"""

import argparse
import copy
import time
import json
import os
import re
import sys
from _render_html_template import render
from urllib.parse import parse_qs, urlparse
from http.server import HTTPServer, SimpleHTTPRequestHandler
from http.cookies import SimpleCookie
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
PROTO_DIR = SCRIPT_DIR / "portal-prototype"
MOCK_DIR = SCRIPT_DIR / "mock-data"
APP_WEB_DIR = SCRIPT_DIR.parent / "src" / "app" / "web"
EPAPER_WEB_DIR = SCRIPT_DIR.parent / "src" / "app" / "device_classes" / "epaper_frame" / "web"
PROFILES = ("esp32-p4-lcd4b", "reterminal-e1003-frame")
SCENARIOS = ("normal", "load-error", "save-error", "invalid-bindings")


def remote_log_response_limit():
    source = (SCRIPT_DIR.parent / "src/app/remote_log.h").read_text(encoding="utf-8")
    match = re.search(r"^constexpr size_t REMOTE_LOG_RESPONSE_RECORDS\s*=\s*(\d+);", source, re.MULTILINE)
    if not match:
        raise RuntimeError("could not read REMOTE_LOG_RESPONSE_RECORDS")
    return int(match.group(1))


LOG_RESPONSE_RECORDS = remote_log_response_limit()


def reset_pad_fixtures(server, scenario="normal"):
    fixture = json.loads((MOCK_DIR / "pad-editor.json").read_text(encoding="utf-8"))
    server.pad_fixture = fixture
    server.mock_pads = fixture["pads"]
    server.mock_defaults = fixture["button_defaults"]
    server.scenario = scenario
    server.mock_screen = "pad_0"
    server.mock_icons = {}
    server.mock_pads["5"]["buttons"] = [
        {"col": col, "row": row, "label_center": f"{row * 8 + col + 1:02}",
         "bg_color": "#166b64" if (col + row) % 2 else "#273641"}
        for row in range(8) for col in range(8)
    ]
    if scenario == "invalid-bindings":
        server.mock_pads["0"]["buttons"][0]["label_center"] = "[pad:]"

CONTENT_TYPES = {
    ".html": "text/html; charset=utf-8",
    ".css": "text/css; charset=utf-8",
    ".js": "application/javascript; charset=utf-8",
    ".json": "application/json; charset=utf-8",
}


class PortalHandler(SimpleHTTPRequestHandler):
    """Route portal assets and emulate the device API locally."""

    def _profile(self):
        cookie = SimpleCookie(self.headers.get("Cookie", ""))
        selected = cookie.get("portal_mock_profile")
        profile = selected.value if selected else self.server.profile
        return profile if profile in PROFILES else self.server.profile

    def _page(self, query):
        value = query.get("page", [""])[0]
        if not value.isascii() or not value.isdecimal() or not 0 <= int(value) < 16:
            self._serve_json({"error": "Missing or invalid page parameter"}, 400)
            return None
        return str(int(value))

    def do_GET(self):
        parsed = urlparse(self.path)
        path = parsed.path
        query = parse_qs(parsed.query)

        # Root → shell.html
        if path == "/":
            self._serve_shell(query)

        # Production portal assets
        elif path == "/portal-all.css":
            self._serve_production_styles()
        elif path == "/portal.js":
            self._serve_production_bundle()
        elif path == "/epaper_init.js":
            self._serve_file(EPAPER_WEB_DIR / "epaper_init.js")
        elif path == "/portal-logs.js":
            self._serve_file(APP_WEB_DIR / "portal_logs.js")

        # Prototype assets remain useful for fragments not yet migrated.
        elif path in ("/bootstrap.min.css", "/portal-custom.css", "/portal_nav.js"):
            self._serve_file(PROTO_DIR / path.lstrip("/"))

        # Mock nav API
        elif path == "/api/portal/nav":
            self._serve_json(self._navigation(self._profile()))

        # Device API fixtures. POSTs are accepted in do_POST below.
        elif path == "/api/config":
            config = copy.deepcopy(self.server.mock_config)
            if self._profile() == "esp32-p4-lcd4b":
                config["caps"].update(ble=False, ble_hid=False, display=True, touch=True, ha_history=True)
            self._serve_json(config)
        elif path == "/api/info":
            self._serve_json(self._device_info("catalog" in query))
        elif path == "/api/pad":
            page = self._page(query)
            if page is not None:
                if self.server.scenario == "load-error":
                    self._serve_json({"error": "Mock pad load failure"}, 503)
                elif page in self.server.mock_pads:
                    self._serve_json(self.server.mock_pads[page])
                else:
                    self._serve_json({"error": "Pad not found"}, 404)
        elif path == "/api/component/button-defaults/config":
            self._serve_json(self.server.mock_defaults)
        elif path == "/api/bindings":
            self._serve_json(self.server.pad_fixture["binding_schema"])
        elif path == "/api/pad/blocks":
            self._serve_json(self.server.pad_fixture["blocks"])
        elif path == "/api/pad/button_sizes":
            try:
                cols = int(query.get("cols", ["0"])[0])
                rows = int(query.get("rows", ["0"])[0])
                if not 1 <= cols <= 8 or not 1 <= rows <= 8:
                    raise ValueError()
            except ValueError:
                self._serve_json({"error": "Invalid grid dimensions"}, 400)
                return
            defaults = self.server.mock_defaults
            gap = int(defaults.get("button_spacing_px", 6))
            margin = int(defaults.get("pixel_shift_distance_px", 4))
            width = 720 - 2 * margin - int(defaults.get("pad_inset_left_px", 0)) - int(defaults.get("pad_inset_right_px", 0))
            height = 720 - 2 * margin - int(defaults.get("pad_inset_top_px", 0)) - int(defaults.get("pad_inset_bottom_px", 0))
            self._serve_json({"display_w": 720, "display_h": 720,
                              "button_w": max(0, width - gap * (cols - 1)) // cols,
                              "button_h": max(0, height - gap * (rows - 1)) // rows,
                              "padding": 4, "gap": gap, "pixel_shift_margin": margin, "font_small_h": 18})
        elif path == "/api/sounds/list":
            self._serve_json(self.server.pad_fixture["sounds"])
        elif path == "/api/extensions":
            self._serve_json(self.server.pad_fixture["extensions"])
        elif path == "/api/images":
            self._serve_json({"directory": "/images", "files": []})
        elif path == "/api/health":
            self._serve_json(self._health())
        elif path == "/api/health/history":
            self._serve_json(self._health_history())
        elif path == "/api/logs":
            self._serve_logs(query)
        elif path == "/api/logs/crash":
            self._serve_json({"available": True, "size": 1024, "partition_size": 65536,
                              "task": "loopTask", "pc": "0x40012345",
                              "panic_reason": "Mock panic: <script> & text",
                              "elf_sha256": "0123456789abcdef" * 4,
                              "architecture": "riscv", "exception_cause": 7, "trap_value": "0x500d2000",
                              "current_reset_reason": 3, "current_elf_sha256": "fedcba9876543210" * 4,
                              "registers": {"MEPC": "0x40012345", "RA": "0x400cfc8c", "SP": "0x4ff41350",
                                            "MSTATUS": "0x00001880", "MTVEC": "0x4ff00003",
                                            "MCAUSE": "0x00000007", "MTVAL": "0x500d2000",
                                            **{f"A{index}": "0x00000000" for index in range(8)}}})
        elif path == "/api/logs/crash/download":
            self._serve_bytes(bytes(range(256)) * 4, "application/octet-stream")
        elif path == "/api/component/epaper-status/status":
            self._serve_json(self._epaper_status())

        # Fragment API — production fragments take precedence.
        elif path.startswith("/api/section/"):
            fragment = path[len("/api/section/"):]
            if not all(c.isalnum() or c in "-_" for c in fragment) or not fragment:
                self.send_error(400, "Invalid section ID")
                return
            candidates = [
                EPAPER_WEB_DIR / f"{fragment}.fragment.html",
                APP_WEB_DIR / f"{fragment}.fragment.html",
                PROTO_DIR / "fragments" / f"{fragment}.fragment.html",
            ]
            for frag in candidates:
                if frag.is_file():
                    html = render(frag, APP_WEB_DIR, "esp32-macropad", "ESP32 Macropad (Local Mock)",
                                  "dev-mock", self._profile() != "esp32-p4-lcd4b")
                    self._serve_bytes(html.encode("utf-8"), "text/html; charset=utf-8")
                    return
            self.send_error(404, f"Fragment not found: {fragment}")

        else:
            self.send_error(404, "Not found")

    def do_POST(self):
        parsed = urlparse(self.path)
        path = parsed.path
        query = parse_qs(parsed.query)
        length = int(self.headers.get("Content-Length", "0"))
        body = self.rfile.read(length) if length else b""

        if path in ("/api/pad", "/api/pad/resolve", "/api/component/button-defaults/config",
                    "/api/component/display/screen", "/__mock/reset"):
            try:
                data = json.loads(body.decode("utf-8")) if body else {}
                if not isinstance(data, dict):
                    raise ValueError()
            except (UnicodeDecodeError, ValueError):
                self._serve_json({"error": "Invalid JSON object"}, 400)
                return
            if path == "/__mock/reset":
                scenario = data.get("scenario", "normal")
                if scenario not in SCENARIOS:
                    self._serve_json({"error": "Unknown mock scenario"}, 400)
                    return
                reset_pad_fixtures(self.server, scenario)
            elif path == "/api/pad":
                page = self._page(query)
                if page is None:
                    return
                if self.server.scenario == "save-error":
                    self._serve_json({"error": "Mock pad save failure"}, 503)
                    return
                if not self._valid_pad(data):
                    self._serve_json({"error": "Invalid pad layout"}, 400)
                    return
                self.server.mock_pads[page] = data
            elif path == "/api/component/button-defaults/config":
                self.server.mock_defaults.update(data)
            elif path == "/api/component/display/screen":
                target = data.get("screen")
                if target not in {screen["id"] for screen in self._device_info()["available_screens"]}:
                    self._serve_json({"error": "Unknown screen"}, 400)
                    return
                self.server.mock_screen = target
            else:
                button = data.get("button", {})
                bindings = data.get("bindings", [])
                if not isinstance(bindings, list) or not all(isinstance(value, str) for value in bindings):
                    self._serve_json({"error": "Invalid bindings"}, 400)
                    return
                if not isinstance(button, dict) or not all(isinstance(value, str) for value in button.values()):
                    self._serve_json({"error": "Invalid button bindings"}, 400)
                    return
                page = str(data.get("screen", "pad_0")).removeprefix("pad_")
                values = self.server.pad_fixture["binding_values"]
                names = self.server.mock_pads.get(page, {}).get("bindings", {})

                def resolve_value(value):
                    source = names.get(value[5:-1], value) if value.startswith("[pad:") and value.endswith("]") else value
                    return values.get(source, "---" if "[" in source else source)

                self._serve_json({"resolved": [{"binding": value, "value": resolve_value(value)} for value in bindings],
                                  "button": {field: resolve_value(value) for field, value in button.items()}})
                return
            self._serve_json({"success": True})
            return

        if path == "/api/icons/install":
            self.server.mock_icons[query.get("id", [""])[0]] = body
            self._serve_json({"success": True})
            return

        if path == "/api/config":
            try:
                self.server.mock_config.update(json.loads(body.decode("utf-8")))
            except (UnicodeDecodeError, json.JSONDecodeError):
                self.send_error(400, "Invalid JSON")
                return
            self._serve_json({"success": True, "message": "Saved to local mock"})
        elif path == "/api/reboot":
            self.server.mock_config["keyboard_active_transport"] = self.server.mock_config["keyboard_transport"]
            self.server.mock_config["keyboard_status"] = "disabled" if self.server.mock_config["keyboard_transport"] == "none" else "ready"
            self._serve_json({"success": True, "message": "Mock reboot complete"})
        elif path in (
            "/api/component/epaper-status/refresh",
            "/api/component/epaper-image/show-url",
            "/api/component/epaper-image/clear-sd-cache",
        ):
            self._serve_json({"success": True, "result": "updated", "elapsed_ms": 640})
        else:
            self.send_error(404, "Not found")

    def do_PUT(self):
        path = urlparse(self.path).path
        if path not in ("/api/component/display/brightness", "/api/component/display/screen"):
            self.send_error(404, "Not found")
            return
        try:
            data = json.loads(self.rfile.read(int(self.headers.get("Content-Length", "0"))))
            if path == "/api/component/display/screen":
                target = data["screen"]
                if target not in {screen["id"] for screen in self._device_info()["available_screens"]}:
                    raise ValueError()
                self.server.mock_screen = target
                self._serve_json({"success": True})
                return
            brightness = data["brightness"]
            if type(brightness) is not int or not 0 <= brightness <= 100:
                raise ValueError()
        except (ValueError, KeyError, TypeError):
            self._serve_json({"error": "Invalid display setting"}, 400)
            return
        self.server.mock_config["backlight_brightness"] = brightness
        self._serve_json({"success": True})

    def do_DELETE(self):
        parsed = urlparse(self.path)
        if parsed.path not in ("/api/pad", "/api/icons/page"):
            self.send_error(404, "Not found")
            return
        page = self._page(parse_qs(parsed.query))
        if page is None:
            return
        if parsed.path == "/api/pad":
            self.server.mock_pads.pop(page, None)
        else:
            self.server.mock_icons = {key: value for key, value in self.server.mock_icons.items()
                                      if not key.startswith("pad_" + page + "_")}
        self._serve_json({"success": True})

    @staticmethod
    def _valid_pad(data):
        cols, rows = data.get("cols"), data.get("rows")
        if type(cols) is not int or type(rows) is not int or not 1 <= cols <= 8 or not 1 <= rows <= 8:
            return False
        if not isinstance(data.get("buttons"), list):
            return False
        occupied = set()
        for button in data["buttons"]:
            if not isinstance(button, dict):
                return False
            col, row = button.get("col"), button.get("row")
            width, height = button.get("col_span", 1), button.get("row_span", 1)
            if any(type(value) is not int for value in (col, row, width, height)):
                return False
            if min(col, row) < 0 or min(width, height) < 1 or col + width > cols or row + height > rows:
                return False
            positions = {(col + offset_col, row + offset_row)
                         for offset_col in range(width) for offset_row in range(height)}
            if occupied.intersection(positions):
                return False
            occupied.update(positions)
        return True

    def _serve_shell(self, query):
        profile = query.get("profile", [self.server.profile])[0]
        if profile not in PROFILES:
            self._serve_json({"error": "Unknown mock profile"}, 400)
            return
        self.profile_cookie = "portal_mock_profile=" + profile + "; Path=/; SameSite=Lax"
        shell = (APP_WEB_DIR / "shell.html").read_text(encoding="utf-8")
        replacements = {
            "{{PROJECT_DISPLAY_NAME}}": "ESP32 Macropad (Local Mock)",
            "{{FIRMWARE_VERSION}}": "dev-mock",
            "{{HEALTH_WIDGET}}": (APP_WEB_DIR / "_health_widget.html").read_text(encoding="utf-8"),
            "{{REBOOT_OVERLAY}}": (APP_WEB_DIR / "_reboot_overlay.html").read_text(encoding="utf-8"),
        }
        for marker, value in replacements.items():
            shell = shell.replace(marker, value)
        fragment = query.get("fragment", ["pad-editor" if profile == "esp32-p4-lcd4b" else "welcome"])[0]
        shell = shell.replace(
            '<script src="/portal.js?v=dev-mock"></script>',
            '<script>window.location.hash = ' + json.dumps("#" + fragment).replace("<", "\\u003c") + ';</script>\n'
            '<script src="/portal.js?v=dev-mock"></script>',
        )
        self._serve_bytes(shell.encode("utf-8"), "text/html; charset=utf-8")

    def _serve_production_bundle(self):
        files = []
        enabled = True
        flags = {"HAS_DISPLAY", "HAS_STORAGE_BROWSER", "HAS_SOUND_PLAYER"} if self._profile() == "esp32-p4-lcd4b" else {"IS_EPAPER_FRAME"}
        for line in (APP_WEB_DIR / "portal.js.bundle").read_text(encoding="utf-8").splitlines():
            line = line.strip()
            if line.startswith("# [chunk:"):
                chunk = line.removeprefix("# [chunk:").removesuffix("]").split()
                enabled = len(chunk) == 1 or chunk[1] in flags
            elif enabled and line and not line.startswith("#"):
                source = APP_WEB_DIR / line
                files.append(source if source.is_file() else EPAPER_WEB_DIR / line)
        bundle = "\n\n".join(path.read_text(encoding="utf-8") for path in files)
        self._serve_bytes(bundle.encode("utf-8"), "application/javascript; charset=utf-8")

    def _serve_production_styles(self):
        # portal-all.css is a build manifest in the source tree. Firmware
        # serves the generated bundle, so assemble the same core sources for
        # the local browser instead of returning the empty manifest marker.
        files = [
            APP_WEB_DIR / "bootstrap.min.css",
            APP_WEB_DIR / "portal-custom.css",
        ]
        styles = "\n\n".join(path.read_text(encoding="utf-8") for path in files)
        self._serve_bytes(styles.encode("utf-8"), "text/css; charset=utf-8")

    def _serve_logs(self, query):
        source = query.get("source", ["recent"])[0]
        try:
            limit = min(LOG_RESPONSE_RECORDS, max(1, int(query.get("limit", [str(LOG_RESPONSE_RECORDS)])[0])))
            after = int(query["after"][0]) if "after" in query else None
            if after is not None and not 0 <= after <= 0xffffffff:
                raise ValueError()
        except ValueError:
            self._serve_json({"error": "Invalid log cursor or limit"}, 400)
            return
        if source not in ("recent", "boot"):
            self._serve_json({"error": "Invalid log source"}, 400)
            return
        boot = source == "boot"
        newest = 8 if boot else 280 + int(time.monotonic() - self.server.logs_started)
        capacity = 128 if boot else 256
        oldest = max(1, newest - capacity + 1)
        reset = "boot_id" in query and query["boot_id"][0] != "123"
        missed = max(0, oldest - after - 1) if after is not None and not reset else 0
        start = oldest if after is None or reset else max(oldest, after + 1)
        records = [{"sequence": sequence,
                    "line": f"[{sequence * 100}ms] I {'SYS' if boot else 'Probe'}: " +
                            ("Boot diagnostic" if boot else "Recent diagnostic <script> & text") + "\n"}
                   for sequence in range(start, min(newest + 1, start + limit))]
        cursor = records[-1]["sequence"] if records else (after if after is not None else newest)
        self._serve_json({"available": True, "boot_id": 123, "capacity": capacity,
                          "oldest": oldest, "newest": newest, "next": cursor,
                          "missed": missed, "dropped": 0, "reset": reset,
                          "has_more": cursor < newest, "boot_complete": True,
                          "boot_truncated": False, "records": records})

    def _navigation(self, profile):
        nav = json.loads((MOCK_DIR / "nav.json").read_text(encoding="utf-8"))
        for category in nav["categories"]:
            if category["id"] == "device":
                category["items"].append({"id": "logs", "display_name": "Logs", "portal_script": "/portal-logs.js"})
            for item in category["items"]:
                if item["id"] == "ble":
                    item["id"] = "hid"
                    item["display_name"] = "Keyboard, Mouse & Gamepad"
        if profile == "esp32-p4-lcd4b":
            nav["primary"] = {"fragment": "pad-editor", "label": "Pad Editor", "icon": ""}
            nav["categories"] = [category for category in nav["categories"] if category["id"] != "sensors"]
            return nav
        nav["primary"] = {"fragment": "epaper-image", "label": "E-paper Frame", "icon": "🖼️"}
        nav["categories"].insert(1, {
            "id": "e-paper",
            "display_name": "E-paper Frame",
            "icon": "🖼️",
            "items": [
                {"id": "epaper-status", "display_name": "Frame Status", "portal_script": "/epaper_init.js"},
                {"id": "epaper-image", "display_name": "Images & Schedule", "portal_script": "/epaper_init.js"},
                {"id": "epaper-overlay", "display_name": "Status Overlay", "portal_script": "/epaper_init.js"},
                {"id": "epaper-vcom", "display_name": "Panel VCOM", "portal_script": "/epaper_init.js"},
            ],
        })
        if profile != "reterminal-e1003-frame":
            nav["primary"]["label"] = profile
        return nav

    def _device_info(self, include_catalog=False):
        info = {"version": "1.14.0-dev", "device_name": "ESP32 Macropad (Local Mock)",
            "chip_model": "ESP32-P4", "chip_revision": 100, "chip_cores": 2,
            "cpu_freq": 360, "flash_chip_size": 16 * 1024 * 1024,
            "psram_size": 32 * 1024 * 1024, "device_class": "E-paper Frame",
            "ap_active": False, "has_mqtt": True,
            "has_touch": True, "has_usb_hid": True}
        if self._profile() == "esp32-p4-lcd4b":
            info.update(device_class="Macropad", board="esp32-p4-lcd4b", has_display=True,
                        has_backlight=True, has_audio=True, has_sound_player=True,
                        has_native_extensions=True, has_camera=False, has_image_fetch=True,
                        has_image_library=True, has_ble=False, has_ble_hid=False,
                        max_pads=16, max_grid_cols=8, max_grid_rows=8,
                        display_coord_width=720, display_coord_height=720, display_blank_on_save=True,
                        available_screens=[{"id": "pad_" + str(page), "name": self.server.mock_pads.get(str(page), {}).get("name", "Pad " + str(page + 1))}
                                           for page in range(16)] + [{"id": "info", "name": "Device Info"}])
            if include_catalog:
                info["catalog"] = self.server.pad_fixture["catalog"]
                info["widget_catalog"] = self.server.pad_fixture["widget_catalog"]
        return info

    def _health(self):
        transport = self.server.mock_config["keyboard_active_transport"]
        return {"cpu_usage": 3, "free_heap": 251392, "free_psram": 29753344,
                "cpu_cores": 2, "uptime_seconds": 93752, "reset_reason": "Power-on",
            "cpu_temperature": 38.2, "flash_used": 41, "flash_total": 100,
                "filesystem_used": 18, "filesystem_total": 100, "ip_address": "127.0.0.1",
                "wifi_rssi": -42, "mqtt_connected": False, "mqtt_enabled": False,
                "keyboard_transport": transport, "keyboard_status": "disabled" if transport == "none" else "ready",
                "ble_status": "connected" if transport == "ble" else "disabled",
                "ble_name": self.server.mock_config["device_name"] + " BLE",
                "ble_bonded": transport == "ble", "ble_encrypted": transport == "ble"}

    @staticmethod
    def _health_history():
        return {"available": True, "period_ms": 5000,
            "cpu_usage": [2, 3, 4, 3, 3, 5, 3],
                "heap_internal_free": [251392, 251200, 251100, 251392],
                "psram_free": [29753344, 29750000, 29753344, 29752000]}

    @staticmethod
    def _epaper_status():
        return {"last_refresh_seconds_ago": 184, "refresh_count": 42,
                "last_result": "updated", "sidecar_http_status": 200,
                "battery_mv": 3920, "battery_pct": 76, "last_crc32": 305419896,
                "timing": {"wifi_rssi": -42, "total_active_ms": 8420,
                           "boot_to_wifi_ms": 2100, "crc_to_draw_ms": 3900,
                           "draw_to_mqtt_ms": 710}}

    def _serve_file(self, filepath: Path):
        try:
            data = filepath.read_bytes()
        except FileNotFoundError:
            self.send_error(404, f"File not found: {filepath.name}")
            return

        ext = filepath.suffix
        content_type = CONTENT_TYPES.get(ext, "application/octet-stream")

        self._serve_bytes(data, content_type)

    def _serve_json(self, payload, status=200):
        self._serve_bytes(json.dumps(payload).encode("utf-8"), "application/json; charset=utf-8", status)

    def _serve_bytes(self, data, content_type, status=200):
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-cache")
        if hasattr(self, "profile_cookie"):
            self.send_header("Set-Cookie", self.profile_cookie)
        self.end_headers()
        self.wfile.write(data)

    def log_message(self, format, *args):
        # Colorize status codes for readability
        sys.stderr.write(f"[portal-dev] {args[0]} {args[1]}\n")


def main():
    parser = argparse.ArgumentParser(description="Portal UI prototype dev server")
    parser.add_argument("--port", type=int, default=8080, help="Port (default: 8080)")
    parser.add_argument("--profile", choices=PROFILES, default="esp32-p4-lcd4b")
    parser.add_argument("--scenario", choices=SCENARIOS, default="normal")
    args = parser.parse_args()

    # Verify prototype directory exists
    if not PROTO_DIR.is_dir():
        print(f"ERROR: Prototype directory not found: {PROTO_DIR}", file=sys.stderr)
        sys.exit(1)

    server = HTTPServer(("127.0.0.1", args.port), PortalHandler)
    server.profile = args.profile
    server.logs_started = time.monotonic()
    reset_pad_fixtures(server, args.scenario)
    server.mock_config = {
        "device_name": "Kitchen Pad",
        "backlight_brightness": 80,
        "operating_mode": "always_on",
        "duty_cycle_wake_seconds": 120,
        "ble_burst_count": 3,
        "ble_adv_interval_ms": 100,
        "ble_tx_power_dbm": 9,
        "caps": {"ble": True, "mqtt": True, "ble_hid": True, "usb_hid": True},
        "keyboard_transport": "none",
        "keyboard_active_transport": "none",
        "keyboard_status": "disabled",
        "epaper_frame_rotation": 1,
        "epaper_frame_service_supported": True,
        "epaper_frame_offline_queue_supported": True,
        "epaper_frame_source_mode": "service",
        "epaper_frame_service_url": "https://frame.example.test",
        "epaper_frame_service_token_set": True,
        "epaper_frame_service_interval_seconds": 900,
        "epaper_frame_offline_refreshes_between_syncs": 3,
        "epaper_frame_sd_cache_supported": True,
        "epaper_frame_sd_cache_enabled": True,
        "epaper_frame_wake_log_enabled": False,
        "epaper_frame_crc32_enabled": True,
        "epaper_frame_frontlight_brightness": 24,
        "epaper_frame_frontlight_duration_s": 30,
        "epaper_frame_wake_budget_ms": 26900,
        "epaper_frame_wake_wifi_target_ms": 4000,
        "epaper_frame_wake_wifi_budget_ms": 5500,
        "epaper_frame_wake_fetch_target_ms": 2000,
        "epaper_frame_wake_fetch_budget_ms": 3500,
        "epaper_frame_wake_mqtt_target_ms": 750,
        "epaper_frame_wake_mqtt_budget_ms": 1500,
        "epaper_frame_wake_cutoff_retry_seconds": 0,
        "ep_sch_hrs": (1 << 24) - 1,
        "ep_sch_tz": 0,
        "ep_c0_url": "https://images.example.test/frame-a.jpg",
        "ep_c0_int": 900,
        "ep_c1_url": "https://images.example.test/frame-b.jpg",
        "ep_c1_int": 1800,
        "ep_c2_url": "",
        "ep_c3_url": "",
        "ep_c4_url": "",
    }
    print(f"Portal dev server running at http://localhost:{args.port}")
    print(f"Serving production assets from: {APP_WEB_DIR}")
    fragment = "pad-editor" if args.profile == "esp32-p4-lcd4b" else "epaper-image"
    print(f"Example: http://localhost:{args.port}/?profile={args.profile}&fragment={fragment}")
    print("Press Ctrl+C to stop.\n")

    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nStopped.")
        server.server_close()


if __name__ == "__main__":
    main()
