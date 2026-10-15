#!/usr/bin/env python3
"""Render an HTML file with template substitution + minification.

Used by tools/minify-web-assets.sh for both full HTML pages and fragment
files. Both consume the same set of partial templates and the same
project-name substitutions.

Substitution sources:
  - Each {{KEY}} is replaced by the contents of $WEB_DIR/<file> per the
    TEMPLATES mapping below. Missing files substitute the empty string.
  - {{PROJECT_NAME}}, {{PROJECT_DISPLAY_NAME}}, and {{FIRMWARE_VERSION}} come
    from CLI args.

Minification:
  - Strip HTML comments
  - Collapse whitespace runs to a single space
  - Remove whitespace between tags
  - Trim
"""

from __future__ import annotations

import argparse
from html import escape
import re
import sys
from pathlib import Path

from widget_manifest import load_widgets, portal_assets

# Placeholder key -> source filename under --web-dir.
TEMPLATES = [
    ("BINDING_HELP", "_binding_help.html"),
    ("STYLE_HELP", "_style_help.html"),
    ("HEALTH_WIDGET", "_health_widget.html"),
    ("REBOOT_OVERLAY", "_reboot_overlay.html"),
    ("HID_BLE", "_hid_ble.html"),
]


def load_template(web_dir: Path, filename: str) -> str:
    path = web_dir / filename
    if path.is_file():
        return path.read_text()
    return ""


def render(input_path: Path, web_dir: Path,
           project_name: str, project_display_name: str,
           firmware_version: str, has_ble_hid: bool = True) -> str:
    html = input_path.read_text()

    if "{{WIDGET_OPTIONS}}" in html or "{{WIDGET_EDITORS}}" in html:
        widgets = [widget for widget in load_widgets() if "portal" in widget]
        html = html.replace("{{WIDGET_OPTIONS}}", "".join(
            f'<option value="{widget["type"]}" id="pad-edit-{widget["type"]}-widget-option">'
            f'{escape(widget["portal"]["label"])}</option>' for widget in widgets))
        html = html.replace("{{WIDGET_EDITORS}}", "".join(
            (web_dir / asset).read_text() for asset in portal_assets(widgets, "html")))

    for key, filename in TEMPLATES:
        template = "" if key == "HID_BLE" and not has_ble_hid else load_template(web_dir, filename)
        html = html.replace("{{" + key + "}}", template)

    html = html.replace("{{PROJECT_NAME}}", project_name)
    html = html.replace("{{PROJECT_DISPLAY_NAME}}", project_display_name)
    html = html.replace("{{FIRMWARE_VERSION}}", firmware_version)

    html = re.sub(r"<!--.*?-->", "", html, flags=re.DOTALL)
    html = re.sub(r"\s+", " ", html)
    html = re.sub(r">\s+<", "><", html)
    return html.strip()


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--web-dir", required=True, type=Path,
                   help="Directory containing _*.html partial templates")
    p.add_argument("--input", required=True, type=Path,
                   help="HTML file to render")
    p.add_argument("--project-name", required=True)
    p.add_argument("--project-display-name", required=True)
    p.add_argument("--firmware-version", required=True)
    p.add_argument("--without-ble-hid", action="store_true")
    args = p.parse_args()

    sys.stdout.write(render(args.input, args.web_dir,
                            args.project_name, args.project_display_name,
                            args.firmware_version, not args.without_ble_hid))
    return 0


if __name__ == "__main__":
    sys.exit(main())
