#!/usr/bin/env python3
"""Render an HTML or fragment file with template substitution + minification.

Replaces the inline python3 -c heredocs in tools/minify-web-assets.sh. The
two heredocs (shell HTML and fragment HTML) differ only in whether the
HEADER/NAV/FOOTER placeholders are substituted; --mode controls that.

Substitution sources:
  - Each {{KEY}} is replaced by the contents of $WEB_DIR/<file> per the
    TEMPLATES mapping below. Missing files substitute the empty string,
    matching the shell's `if [ -f ... ]; then VAR=$(cat ...); fi` pattern.
  - {{PROJECT_NAME}} and {{PROJECT_DISPLAY_NAME}} come from CLI args.

Minification matches the original heredoc exactly:
  - Strip HTML comments
  - Collapse whitespace runs to a single space
  - Remove whitespace between tags
  - Trim
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

# Placeholder key -> source filename under --web-dir. Order is preserved so
# the substitution sequence matches the original shell heredocs exactly.
TEMPLATES_SHELL_ONLY = [
    ("HEADER", "_header.html"),
    ("NAV", "_nav.html"),
    ("FOOTER", "_footer.html"),
]

TEMPLATES_SHARED = [
    ("BINDING_HELP", "_binding_help.html"),
    ("WIDGET_BAR_CHART", "_widget_bar_chart.html"),
    ("WIDGET_GAUGE", "_widget_gauge.html"),
    ("WIDGET_SPARKLINE", "_widget_sparkline.html"),
    ("WIDGET_TABLE", "_widget_table.html"),
    ("WIDGET_ROCKER", "_widget_rocker.html"),
    ("WIDGET_NUMERICROCKER", "_widget_numericrocker.html"),
    ("WIDGET_WAVEFORM", "_widget_waveform.html"),
    ("WIDGET_LIST", "_widget_list.html"),
    ("STYLE_HELP", "_style_help.html"),
    ("HEALTH_WIDGET", "_health_widget.html"),
    ("REBOOT_OVERLAY", "_reboot_overlay.html"),
]


def load_template(web_dir: Path, filename: str) -> str:
    path = web_dir / filename
    if path.is_file():
        return path.read_text()
    return ""


def render(input_path: Path, web_dir: Path, mode: str,
           project_name: str, project_display_name: str) -> str:
    html = input_path.read_text()

    if mode == "shell":
        keys = TEMPLATES_SHELL_ONLY + TEMPLATES_SHARED
    else:  # fragment
        keys = TEMPLATES_SHARED

    for key, filename in keys:
        html = html.replace("{{" + key + "}}", load_template(web_dir, filename))

    html = html.replace("{{PROJECT_NAME}}", project_name)
    html = html.replace("{{PROJECT_DISPLAY_NAME}}", project_display_name)

    # Minify: strip comments, collapse whitespace, remove inter-tag whitespace.
    html = re.sub(r"<!--.*?-->", "", html, flags=re.DOTALL)
    html = re.sub(r"\s+", " ", html)
    html = re.sub(r">\s+<", "><", html)
    return html.strip()


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--web-dir", required=True, type=Path,
                   help="Directory containing _*.html partial templates")
    p.add_argument("--input", required=True, type=Path,
                   help="HTML or fragment file to render")
    p.add_argument("--mode", required=True, choices=("shell", "fragment"),
                   help="shell: substitute HEADER/NAV/FOOTER too. "
                        "fragment: skip them.")
    p.add_argument("--project-name", required=True)
    p.add_argument("--project-display-name", required=True)
    args = p.parse_args()

    sys.stdout.write(render(args.input, args.web_dir, args.mode,
                            args.project_name, args.project_display_name))
    return 0


if __name__ == "__main__":
    sys.exit(main())
