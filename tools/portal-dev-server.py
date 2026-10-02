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
import json
import os
import sys
from _render_html_template import render
from urllib.parse import parse_qs, urlparse
from http.server import HTTPServer, SimpleHTTPRequestHandler
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
PROTO_DIR = SCRIPT_DIR / "portal-prototype"
MOCK_DIR = SCRIPT_DIR / "mock-data"
APP_WEB_DIR = SCRIPT_DIR.parent / "src" / "app" / "web"
EPAPER_WEB_DIR = SCRIPT_DIR.parent / "src" / "app" / "device_classes" / "epaper_frame" / "web"

CONTENT_TYPES = {
    ".html": "text/html; charset=utf-8",
    ".css": "text/css; charset=utf-8",
    ".js": "application/javascript; charset=utf-8",
    ".json": "application/json; charset=utf-8",
}


class PortalHandler(SimpleHTTPRequestHandler):
    """Route portal assets and emulate the device API locally."""

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

        # Prototype assets remain useful for fragments not yet migrated.
        elif path in ("/bootstrap.min.css", "/portal-custom.css", "/portal_nav.js"):
            self._serve_file(PROTO_DIR / path.lstrip("/"))

        # Mock nav API
        elif path == "/api/portal/nav":
            self._serve_json(self._navigation(query.get("profile", ["reterminal-e1003-frame"])[0]))

        # Device API fixtures. POSTs are accepted in do_POST below.
        elif path == "/api/config":
            self._serve_json(self.server.mock_config)
        elif path == "/api/info":
            self._serve_json(self._device_info())
        elif path == "/api/health":
            self._serve_json(self._health())
        elif path == "/api/health/history":
            self._serve_json(self._health_history())
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
                    if fragment == "hid":
                        html = render(frag, APP_WEB_DIR, "esp32-macropad", "ESP32 Macropad (Local Mock)",
                                      "dev-mock", self.server.mock_config["caps"]["ble_hid"])
                        self._serve_bytes(html.encode("utf-8"), "text/html; charset=utf-8")
                        return
                    self._serve_file(frag)
                    return
            self.send_error(404, f"Fragment not found: {fragment}")

        else:
            self.send_error(404, "Not found")

    def do_POST(self):
        path = urlparse(self.path).path
        length = int(self.headers.get("Content-Length", "0"))
        body = self.rfile.read(length) if length else b""

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

    def _serve_shell(self, query):
        shell = (APP_WEB_DIR / "shell.html").read_text(encoding="utf-8")
        replacements = {
            "{{PROJECT_DISPLAY_NAME}}": "ESP32 Macropad (Local Mock)",
            "{{FIRMWARE_VERSION}}": "dev-mock",
            "{{HEALTH_WIDGET}}": (APP_WEB_DIR / "_health_widget.html").read_text(encoding="utf-8"),
            "{{REBOOT_OVERLAY}}": (APP_WEB_DIR / "_reboot_overlay.html").read_text(encoding="utf-8"),
        }
        for marker, value in replacements.items():
            shell = shell.replace(marker, value)
        fragment = query.get("fragment", ["welcome"])[0]
        shell = shell.replace(
            '<script src="/portal.js?v=dev-mock"></script>',
            '<script>window.location.hash = ' + json.dumps("#" + fragment) + ';</script>\n'
            '<script src="/portal.js?v=dev-mock"></script>',
        )
        self._serve_bytes(shell.encode("utf-8"), "text/html; charset=utf-8")

    def _serve_production_bundle(self):
        # Keep this list explicit: it mirrors the always-on portal chunks and
        # avoids pulling board-specific editors into a lightweight dev page.
        files = [
            APP_WEB_DIR / "portal_core.js",
            APP_WEB_DIR / "portal_dropdown.js",
            APP_WEB_DIR / "portal_binding_validator.js",
            APP_WEB_DIR / "portal_config.js",
            APP_WEB_DIR / "portal_firmware.js",
            APP_WEB_DIR / "portal_health_sparkline.js",
            APP_WEB_DIR / "portal_health.js",
            APP_WEB_DIR / "portal_extensions.js",
            APP_WEB_DIR / "portal_nav.js",
            APP_WEB_DIR / "portal_fragment_init.js",
            APP_WEB_DIR / "portal.js",
        ]
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

    def _navigation(self, profile):
        nav = json.loads((MOCK_DIR / "nav.json").read_text(encoding="utf-8"))
        for category in nav["categories"]:
            for item in category["items"]:
                if item["id"] == "ble":
                    item["id"] = "hid"
                    item["display_name"] = "Keyboard & Mouse"
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

    @staticmethod
    def _device_info():
        return {"version": "1.14.0-dev", "device_name": "ESP32 Macropad (Local Mock)",
            "chip_model": "ESP32-P4", "chip_revision": 100, "chip_cores": 2,
            "cpu_freq": 360, "flash_chip_size": 16 * 1024 * 1024,
            "psram_size": 32 * 1024 * 1024, "device_class": "E-paper Frame",
            "ap_active": False, "has_mqtt": True,
            "has_touch": True, "has_usb_hid": True}

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

    def _serve_json(self, payload):
        self._serve_bytes(json.dumps(payload).encode("utf-8"), "application/json; charset=utf-8")

    def _serve_bytes(self, data, content_type):
        self.send_response(200)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-cache")
        self.end_headers()
        self.wfile.write(data)

    def log_message(self, format, *args):
        # Colorize status codes for readability
        sys.stderr.write(f"[portal-dev] {args[0]} {args[1]}\n")


def main():
    parser = argparse.ArgumentParser(description="Portal UI prototype dev server")
    parser.add_argument("--port", type=int, default=8080, help="Port (default: 8080)")
    args = parser.parse_args()

    # Verify prototype directory exists
    if not PROTO_DIR.is_dir():
        print(f"ERROR: Prototype directory not found: {PROTO_DIR}", file=sys.stderr)
        sys.exit(1)

    server = HTTPServer(("", args.port), PortalHandler)
    server.mock_config = {
        "device_name": "Kitchen Pad",
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
    print("Example: http://localhost:%d/?profile=reterminal-e1003-frame&fragment=epaper-image" % args.port)
    print("Press Ctrl+C to stop.\n")

    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nStopped.")
        server.server_close()


if __name__ == "__main__":
    main()
