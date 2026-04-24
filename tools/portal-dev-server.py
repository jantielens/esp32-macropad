#!/usr/bin/env python3
"""Local dev server for the portal UI prototype.

Serves shell HTML, CSS, JS, mock nav JSON, and placeholder fragments
from tools/portal-prototype/ so the layout can be iterated in a browser
without deploying to an ESP32.

Usage:
    python3 tools/portal-dev-server.py [--port PORT]
"""

import argparse
import json
import os
import sys
from http.server import HTTPServer, SimpleHTTPRequestHandler
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
PROTO_DIR = SCRIPT_DIR / "portal-prototype"
MOCK_DIR = SCRIPT_DIR / "mock-data"

CONTENT_TYPES = {
    ".html": "text/html; charset=utf-8",
    ".css": "text/css; charset=utf-8",
    ".js": "application/javascript; charset=utf-8",
    ".json": "application/json; charset=utf-8",
}


class PortalHandler(SimpleHTTPRequestHandler):
    """Route requests to the prototype directory and mock data."""

    def do_GET(self):
        path = self.path.split("?")[0]  # strip query string

        # Root → shell.html
        if path == "/":
            self._serve_file(PROTO_DIR / "shell.html")

        # Static assets from prototype dir
        elif path in ("/bootstrap.min.css", "/portal-custom.css", "/portal_nav.js"):
            self._serve_file(PROTO_DIR / path.lstrip("/"))

        # Mock nav API
        elif path == "/api/portal/nav":
            self._serve_file(MOCK_DIR / "nav.json")

        # Fragment API — /api/section/{category}
        elif path.startswith("/api/section/"):
            category = path[len("/api/section/"):]
            # Sanitize: allow only alphanumeric + hyphen
            if not all(c.isalnum() or c == "-" for c in category) or not category:
                self.send_error(400, "Invalid section ID")
                return
            frag = PROTO_DIR / "fragments" / f"{category}.fragment.html"
            if frag.is_file():
                self._serve_file(frag)
            else:
                self.send_error(404, f"Fragment not found: {category}")

        else:
            self.send_error(404, "Not found")

    def _serve_file(self, filepath: Path):
        try:
            data = filepath.read_bytes()
        except FileNotFoundError:
            self.send_error(404, f"File not found: {filepath.name}")
            return

        ext = filepath.suffix
        content_type = CONTENT_TYPES.get(ext, "application/octet-stream")

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
    print(f"Portal dev server running at http://localhost:{args.port}")
    print(f"Serving from: {PROTO_DIR}")
    print("Press Ctrl+C to stop.\n")

    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nStopped.")
        server.server_close()


if __name__ == "__main__":
    main()
