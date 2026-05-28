#!/usr/bin/env python3
"""Portal live-dev server.

Serves the portal UI directly from src/app/web/ (no build step, instant
refresh on file save) and proxies all /api/* requests to a real device.
The shell.html {{PROJECT_DISPLAY_NAME}} placeholder is substituted at
serve time so the page title looks reasonable in the browser.

Usage:
    python3 tools/portal-dev-server.py --device http://192.168.1.42
    python3 tools/portal-dev-server.py --device http://192.168.1.42 --port 8080

Workflow:
    1. Start this server.
    2. Open http://localhost:8080 in a browser.
    3. Edit any file in src/app/web/ and hit refresh — changes appear instantly.
    4. When happy, run:
         ./tools/minify-web-assets.sh && ./build.sh <board>
       to embed the changes into firmware.
"""

import argparse
import sys
import urllib.error
import urllib.request
from http.server import BaseHTTPRequestHandler, HTTPServer
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
WEB_DIR = SCRIPT_DIR.parent / "src" / "app" / "web"

CONTENT_TYPES = {
    ".html": "text/html; charset=utf-8",
    ".css":  "text/css; charset=utf-8",
    ".js":   "application/javascript; charset=utf-8",
    ".json": "application/json; charset=utf-8",
    ".ico":  "image/x-icon",
    ".png":  "image/png",
}

BUNDLE_MANIFEST = WEB_DIR / "portal.js.bundle"


def build_portal_js() -> bytes:
    """Concatenate all JS modules listed in portal.js.bundle, in order."""
    parts = []
    for line in BUNDLE_MANIFEST.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        path = WEB_DIR / line
        try:
            parts.append(f"// --- {line} ---\n")
            parts.append(path.read_text(encoding="utf-8"))
            parts.append("\n")
        except FileNotFoundError:
            sys.stderr.write(f"[portal-dev] WARNING: bundle file not found: {path}\n")
    return "".join(parts).encode("utf-8")

# Substituted into shell.html so the title isn't "{{PROJECT_DISPLAY_NAME}}"
DEV_PROJECT_NAME = "ESP32 Macropad [DEV]"

# Device request timeout in seconds
PROXY_TIMEOUT = 8


class PortalDevHandler(BaseHTTPRequestHandler):

    # device_base is set on the class by main() before the server starts
    device_base: str = ""

    # ------------------------------------------------------------------
    # Request dispatch
    # ------------------------------------------------------------------

    def do_GET(self):
        self._dispatch("GET")

    def do_POST(self):
        self._dispatch("POST")

    def do_PUT(self):
        self._dispatch("PUT")

    def do_DELETE(self):
        self._dispatch("DELETE")

    def do_PATCH(self):
        self._dispatch("PATCH")

    def _dispatch(self, method: str):
        path = self.path.split("?")[0]

        # Fragment API → serve raw source files directly (device serves gzip)
        if path.startswith("/api/section/"):
            self._serve_fragment(path[len("/api/section/"):])
            return

        # All other /api/* → proxy to device
        if path.startswith("/api/"):
            self._proxy(method)
            return

        if method != "GET":
            self.send_error(405)
            return

        # Root → shell.html
        if path == "/" or path == "/index.html":
            self._serve_shell()
            return

        # portal.js → on-the-fly concatenation of all bundle modules
        if path == "/portal.js":
            self._serve_portal_js()
            return

        # Fragment API → serve raw source files directly (device serves gzip)
        if path.startswith("/api/section/"):
            self._serve_fragment(path[len("/api/section/"):])
            return

        # Any other path → look up in src/app/web/
        # Prevent directory traversal: only allow safe characters
        safe = path.lstrip("/")
        if ".." in safe or safe.startswith("/"):
            self.send_error(400, "Bad path")
            return

        candidate = WEB_DIR / safe
        if candidate.is_file():
            self._serve_file(candidate)
        else:
            self.send_error(404, f"Not found: {safe}")

    # ------------------------------------------------------------------
    # Static file serving
    # ------------------------------------------------------------------

    def _serve_shell(self):
        shell = WEB_DIR / "shell.html"
        health_widget = WEB_DIR / "_health_widget.html"
        try:
            data = shell.read_text(encoding="utf-8")
        except FileNotFoundError:
            self.send_error(404, "shell.html not found")
            return
        data = data.replace("{{PROJECT_DISPLAY_NAME}}", DEV_PROJECT_NAME)
        hw = health_widget.read_text(encoding="utf-8") if health_widget.is_file() else ""
        data = data.replace("{{HEALTH_WIDGET}}", hw)
        encoded = data.encode("utf-8")
        self.send_response(200)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Content-Length", str(len(encoded)))
        self.send_header("Cache-Control", "no-cache")
        self.end_headers()
        self.wfile.write(encoded)

    def _serve_portal_js(self):
        data = build_portal_js()
        self.send_response(200)
        self.send_header("Content-Type", "application/javascript; charset=utf-8")
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-cache")
        self.end_headers()
        self.wfile.write(data)

    def _serve_fragment(self, category: str):
        # Sanitize: only alphanumeric + hyphen allowed in category names
        if not category or not all(c.isalnum() or c == "-" for c in category):
            self.send_error(400, "Invalid section ID")
            return
        frag = WEB_DIR / f"{category}.fragment.html"
        if frag.is_file():
            self._serve_file(frag)
        else:
            self.send_error(404, f"Fragment not found: {category}")

    def _serve_file(self, filepath: Path):
        try:
            data = filepath.read_bytes()
        except FileNotFoundError:
            self.send_error(404, f"File not found: {filepath.name}")
            return
        content_type = CONTENT_TYPES.get(filepath.suffix, "application/octet-stream")
        self.send_response(200)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-cache")
        self.end_headers()
        self.wfile.write(data)

    # ------------------------------------------------------------------
    # API proxy
    # ------------------------------------------------------------------

    def _proxy(self, method: str):
        target = self.device_base.rstrip("/") + self.path

        # Read request body for methods that carry one
        body = None
        content_length = int(self.headers.get("Content-Length", 0) or 0)
        if content_length > 0:
            body = self.rfile.read(content_length)

        # Forward a minimal set of headers; skip hop-by-hop / host headers
        forward_headers = {}
        for h in ("Content-Type", "Accept", "Authorization"):
            v = self.headers.get(h)
            if v:
                forward_headers[h] = v

        req = urllib.request.Request(
            target,
            data=body,
            headers=forward_headers,
            method=method,
        )

        try:
            with urllib.request.urlopen(req, timeout=PROXY_TIMEOUT) as resp:
                resp_body = resp.read()
                self.send_response(resp.status)
                # Forward content-type from device
                ct = resp.headers.get("Content-Type", "application/octet-stream")
                self.send_header("Content-Type", ct)
                self.send_header("Content-Length", str(len(resp_body)))
                self.send_header("Cache-Control", "no-cache")
                # Allow browser JS to reach the proxied API
                self.send_header("Access-Control-Allow-Origin", "*")
                self.end_headers()
                self.wfile.write(resp_body)

        except urllib.error.HTTPError as e:
            resp_body = e.read()
            self.send_response(e.code)
            ct = e.headers.get("Content-Type", "application/json")
            self.send_header("Content-Type", ct)
            self.send_header("Content-Length", str(len(resp_body)))
            self.end_headers()
            self.wfile.write(resp_body)

        except (urllib.error.URLError, OSError) as e:
            msg = f"Device unreachable: {e}".encode()
            self.send_response(502)
            self.send_header("Content-Type", "text/plain")
            self.send_header("Content-Length", str(len(msg)))
            self.end_headers()
            self.wfile.write(msg)

    # ------------------------------------------------------------------
    # Logging
    # ------------------------------------------------------------------

    def log_message(self, fmt, *args):
        status = args[1] if len(args) > 1 else "?"
        color = "\033[32m" if str(status).startswith("2") else "\033[33m" if str(status).startswith("3") else "\033[31m"
        reset = "\033[0m"
        sys.stderr.write(f"[portal-dev] {color}{args[0]}{reset} {status}\n")


def main():
    parser = argparse.ArgumentParser(
        description="Portal live-dev server — serves src/app/web/ and proxies /api/* to device",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    parser.add_argument(
        "--device", required=True,
        help="Device base URL, e.g. http://192.168.1.42",
    )
    parser.add_argument("--port", type=int, default=8080, help="Local port (default: 8080)")
    args = parser.parse_args()

    if not WEB_DIR.is_dir():
        print(f"ERROR: Web directory not found: {WEB_DIR}", file=sys.stderr)
        sys.exit(1)

    PortalDevHandler.device_base = args.device.rstrip("/")

    print(f"Portal live-dev server")
    print(f"  UI source : {WEB_DIR}")
    print(f"  API proxy : {PortalDevHandler.device_base}")
    print(f"  Open      : http://localhost:{args.port}")
    print(f"  Refresh browser after editing any file in src/app/web/")
    print(f"  Ctrl+C to stop\n")

    server = HTTPServer(("", args.port), PortalDevHandler)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nStopped.")
        server.server_close()


if __name__ == "__main__":
    main()
